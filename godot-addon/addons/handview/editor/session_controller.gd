@tool
class_name HandviewSessionController
extends RefCounted

# Editor side session logic (DECISIONS D18). While the editor runs, the phone
# listener stays open and every authorized device gets the Handview app,
# an adb reverse for the session port and an offer with a one-time token
# (SPEC section 5), so the app on the phone can join without a button press in
# the editor. Commands follow SPEC 6.4. Every adb call runs on a
# WorkerThreadPool task and reports back through call_deferred, so the editor
# main thread never waits for adb. The plugin calls process() once per frame.

signal devices_changed
signal state_changed
signal uninstall_requested(serial: String, output: String)
signal uninstall_cancelled

const Adb := preload("adb.gd")
const Logic := preload("session_logic.gd")
const EmbedToggle := preload("embed_toggle.gd")
const Debugger := preload("debugger.gd")

const APK_PATH: String = "res://addons/handview/android/handview.apk"
const VERSION_PATH: String = "res://addons/handview/android/handview.version"
const REFRESH_INTERVAL_SEC: float = 3.0
const EMBED_RESTORE_DELAY_SEC: float = 1.5
const STREAM_FPS: int = 60
const PAUSE_REASON_NOT_PLAYING: int = 0
const PAUSE_REASON_PAUSED: int = 1
# Size of the project icon sent with offers; small enough for one adb shell command.
const OFFER_ICON_SIZE: int = 64

const ENV_LINK_PORT: String = "HANDVIEW_LINK_PORT"
const ENV_LINK_TOKEN: String = "HANDVIEW_LINK_TOKEN"
const ENV_HIDE_WINDOW: String = "HANDVIEW_HIDE_WINDOW"
const ENV_LOOPBACK_FILE: String = "HANDVIEW_LOOPBACK_FILE"
const ENV_LOOPBACK_AUTOSTART: String = "HANDVIEW_LOOPBACK_AUTOSTART"
const METADATA_SECTION: String = "handview"
const METADATA_WIFI: String = "wifi"
const BEACON_INTERVAL_SEC: float = 2.0

var devices: Array = []
var adb_found: bool = true
var status: String = ""
var encoder_text: String = ""
var phone_info: Dictionary = {}
# Set by the plugin; pauses and steps the game. Null disables Pause and Step.
var debugger: Debugger = null
# Wi-Fi sessions (DECISIONS D22): the listener takes LAN connections, a beacon
# announces the editor, and phones join with the QR code or the PIN.
var wifi_enabled: bool = false
var wifi_pin: String = ""
var wifi_addresses: PackedStringArray = PackedStringArray()
var wifi_qr: Image = null
var _beacon: PacketPeerUDP = null
var _beacon_timer: float = 0.0

var _native_ok: bool = false
# Native HandviewEditorSession. Untyped so the script parses without the library.
var _session: Object = null
var _encoder: int = -1
var _embed: EmbedToggle = EmbedToggle.new()

var _tasks: Array[int] = []
var _refreshing: bool = false
var _refresh_timer: float = 0.0
var _is_shutdown: bool = false
var _port: int = -1
var _token_hex: String = ""
var _link_token_hex: String = ""
var _project_name_hex: String = ""
var _project_icon_b64: String = ""
# serial -> token the device was last offered.
var _armed: Dictionary = {}
# serial -> true while its prepare task runs.
var _arming: Dictionary = {}
# serial -> message of the last failed attempt; retried on Refresh or replug.
var _failed: Dictionary = {}
var _pending_install: Dictionary = {}
var _phone_connected: bool = false

var _editor_state: int = Logic.STATE_STOPPED
var _playing: bool = false
# True when the running game was started from the phone (hidden window).
var _phone_play: bool = false
var _hide_unset_pending: bool = false
var _embed_timer: float = 0.0


func _init() -> void:
	_ensure_native()


# The library can load after the plugin was created (an addon copied into an
# open editor), so this is checked again before anything needs it.
func _ensure_native() -> bool:
	if _native_ok:
		return true
	if not ClassDB.class_exists("HandviewEditorSession"):
		return false
	_session = ClassDB.instantiate("HandviewEditorSession")
	_native_ok = _session != null
	if _native_ok:
		_encoder = int(_session.call("probe_encoder"))
		encoder_text = Logic.encoder_line(_encoder)
	return _native_ok


# True when the loopback test hook should start a session automatically.
func loopback_requested() -> bool:
	return OS.has_environment(ENV_LOOPBACK_FILE) and OS.get_environment(ENV_LOOPBACK_AUTOSTART) == "1"


# Called once by the plugin after the dock is connected.
func start() -> void:
	var project_name: String = str(ProjectSettings.get_setting("application/config/name", ""))
	_project_name_hex = Logic.offer_name_hex(project_name)
	_project_icon_b64 = _load_project_icon()
	wifi_enabled = _remembered_wifi()
	if _ensure_native():
		_open_listener()
	else:
		_set_status(Logic.NATIVE_MISSING_MESSAGE)
	# After the listener, so the cleanup sees this editor's port in use.
	_spawn(_task_cleanup_stale.bind(_find_adb()))
	refresh_devices()
	if loopback_requested() and is_listening():
		_write_loopback_file()


# Stops everything and clears what this controller changed. Blocks briefly (exit path).
func shutdown() -> void:
	_is_shutdown = true
	_clear_pending_install()
	_embed.end_launch()
	OS.unset_environment(ENV_HIDE_WINDOW)
	_set_link_env(false)
	_close_beacon()
	if is_listening():
		_session.call("stop")
	for id: int in _tasks:
		WorkerThreadPool.wait_for_task_completion(id)
	_tasks.clear()
	var path: String = _find_adb()
	if not path.is_empty() and _port > 0:
		var adb: Adb = Adb.new(path)
		for serial: String in _armed.keys():
			adb.reverse_remove(serial, _port)
	_armed.clear()


func is_native_available() -> bool:
	return _ensure_native()


func is_listening() -> bool:
	return _native_ok and bool(_session.call("is_running"))


func get_port() -> int:
	return _port


func is_phone_connected() -> bool:
	return _phone_connected


# Short state of one device for the dock.
func device_status(serial: String) -> String:
	if serial == Logic.LOOPBACK_SERIAL:
		return "Loopback"
	for device: Dictionary in devices:
		if str(device["serial"]) == serial and str(device["state"]) != "device":
			return Logic.device_hint(str(device["state"]), true)
	if _failed.has(serial):
		return str(_failed[serial])
	if _arming.has(serial):
		return "Preparing the phone..."
	if _armed.has(serial):
		return "Ready. Open Handview on the phone."
	return ""


func _set_status(text: String) -> void:
	status = text
	state_changed.emit()


func _find_adb() -> String:
	return Adb.find_adb(Adb.sdk_path_from_editor_settings())


func _spawn(task: Callable) -> void:
	if _is_shutdown:
		return
	_tasks.append(WorkerThreadPool.add_task(task, false, "Handview adb"))


func _reap_tasks() -> void:
	var index: int = _tasks.size() - 1
	while index >= 0:
		if WorkerThreadPool.is_task_completed(_tasks[index]):
			WorkerThreadPool.wait_for_task_completion(_tasks[index])
			_tasks.remove_at(index)
		index -= 1


# ---- Per frame -------------------------------------------------------------

func process(delta: float) -> void:
	_reap_tasks()
	if _hide_unset_pending:
		_hide_unset_pending = false
		OS.unset_environment(ENV_HIDE_WINDOW)
	_refresh_timer += delta
	if _refresh_timer >= REFRESH_INTERVAL_SEC:
		refresh_devices()
	if _embed.is_changed():
		_embed_timer -= delta
		if _embed_timer <= 0.0:
			_embed.end_launch()
	if is_listening():
		for event: Dictionary in _session.call("poll"):
			_handle_event(event)
	_sync_playing()
	if wifi_enabled and is_listening():
		_beacon_timer -= delta
		if _beacon_timer <= 0.0:
			_beacon_timer = BEACON_INTERVAL_SEC
			_send_beacon()


# ---- Listener and tokens ---------------------------------------------------

# The project icon as a small base64 PNG for the app's device list, "" if none.
func _load_project_icon() -> String:
	var path: String = str(ProjectSettings.get_setting("application/config/icon", ""))
	if path.is_empty() or not ResourceLoader.exists(path):
		return ""
	var texture: Texture2D = load(path) as Texture2D
	if texture == null:
		return ""
	var image: Image = texture.get_image()
	if image == null or image.is_empty():
		return ""
	if image.is_compressed() and image.decompress() != OK:
		return ""
	image.resize(OFFER_ICON_SIZE, OFFER_ICON_SIZE, Image.INTERPOLATE_LANCZOS)
	return Marshalls.raw_to_base64(image.save_png_to_buffer())


# Opens the phone listener on the first free port and arms the first token.
func _open_listener() -> void:
	var port: int = -1
	for candidate: int in range(Adb.PORT_FIRST, Adb.PORT_LAST + 1):
		if Adb.is_port_free(candidate):
			port = candidate
			break
	if port < 0:
		_set_status("No free port between %d and %d." % [Adb.PORT_FIRST, Adb.PORT_LAST])
		return
	var lan: bool = wifi_enabled and is_wifi_supported()
	if not bool(_session.call("start", port, lan) if lan else _session.call("start", port)):
		_set_status("Could not listen on port %d." % port)
		return
	_port = port
	var link_token: PackedByteArray = Crypto.new().generate_random_bytes(32)
	_session.call("set_link_token", link_token)
	_link_token_hex = link_token.hex_encode()
	var width: int = int(ProjectSettings.get_setting("display/window/size/viewport_width", 1152))
	var height: int = int(ProjectSettings.get_setting("display/window/size/viewport_height", 648))
	var bitrate: int = Logic.default_bitrate_kbps(width, height, STREAM_FPS)
	_session.call("set_stream_info", Logic.announced_encoder(_encoder), width, height, STREAM_FPS, bitrate)
	_new_token()
	_set_status("Waiting for a phone.")


# Arms a fresh one-time token and offers it to every prepared device.
func _new_token() -> void:
	var token: PackedByteArray = Crypto.new().generate_random_bytes(32)
	_session.call("set_pending_token", token)
	_token_hex = token.hex_encode()
	var path: String = _find_adb()
	for serial: String in _armed.keys():
		_spawn(_task_offer.bind(path, serial, _port, _token_hex, _project_name_hex, _project_icon_b64))
	_update_wifi()


# ---- Wi-Fi -----------------------------------------------------------------

func set_wifi_enabled(enabled: bool) -> void:
	if enabled == wifi_enabled:
		return
	wifi_enabled = enabled
	var settings: EditorSettings = EditorInterface.get_editor_settings()
	if settings != null:
		settings.set_project_metadata(METADATA_SECTION, METADATA_WIFI, enabled)
	# The listener binds again, on every interface or on loopback only. A
	# connected phone drops and joins again.
	if is_listening():
		_session.call("stop")
		_port = -1
		_armed.clear()
		_open_listener()
	if not enabled:
		_close_beacon()
	_update_wifi()


func _remembered_wifi() -> bool:
	var settings: EditorSettings = EditorInterface.get_editor_settings()
	return settings != null and bool(settings.get_project_metadata(METADATA_SECTION, METADATA_WIFI, false))


# True when the loaded native library has the Wi-Fi calls. Scripts reload on
# their own after an update, the library only when the editor restarts.
func is_wifi_supported() -> bool:
	return _native_ok and _session.has_method("set_pending_pin") and _session.has_method("make_qr")


# A new PIN and QR code for the current token, or none with Wi-Fi off.
func _update_wifi() -> void:
	if not is_wifi_supported():
		wifi_pin = ""
		wifi_qr = null
		wifi_addresses = PackedStringArray()
		state_changed.emit()
		return
	if not wifi_enabled or not is_listening():
		wifi_pin = ""
		wifi_qr = null
		wifi_addresses = PackedStringArray()
		if is_listening():
			_session.call("set_pending_pin", PackedByteArray())
		state_changed.emit()
		return
	wifi_pin = Logic.new_pin()
	_session.call("set_pending_pin", Logic.pin_token(wifi_pin))
	wifi_addresses = Logic.lan_addresses(IP.get_local_addresses())
	wifi_qr = null
	if loopback_requested():
		_write_loopback_file()
	if not wifi_addresses.is_empty():
		var qr: Dictionary = _session.call("make_qr", Logic.wifi_link(wifi_addresses[0], _port, _token_hex, _project_name_hex))
		wifi_qr = Logic.qr_image(qr)
	state_changed.emit()


func _send_beacon() -> void:
	if _beacon == null:
		_beacon = PacketPeerUDP.new()
		_beacon.set_broadcast_enabled(true)
		_beacon.set_dest_address("255.255.255.255", Logic.BEACON_PORT)
	_beacon.put_packet(Logic.beacon_json(str(ProjectSettings.get_setting("application/config/name", "")), _port, wifi_addresses).to_utf8_buffer())


func _close_beacon() -> void:
	if _beacon != null:
		_beacon.close()
		_beacon = null


func _write_loopback_file() -> void:
	var file: FileAccess = FileAccess.open(OS.get_environment(ENV_LOOPBACK_FILE), FileAccess.WRITE)
	if file == null:
		_set_status("Could not write the loopback file.")
		return
	file.store_string(Logic.loopback_json(_port, _token_hex, wifi_pin))
	file.close()


# ---- Device list -----------------------------------------------------------

func refresh_devices() -> void:
	_refresh_timer = 0.0
	# The library may have loaded late, or every port was taken before.
	if not is_listening() and _ensure_native():
		_open_listener()
	if _refreshing:
		return
	_refreshing = true
	_spawn(_task_refresh.bind(_find_adb(), _port, _armed.keys()))


# The Refresh button also retries devices whose preparation failed.
func retry_failed() -> void:
	_failed.clear()
	refresh_devices()


func _task_refresh(adb_path: String, port: int, armed: Array) -> void:
	var list: Array = []
	# serial -> reversed ports, for prepared devices only.
	var reverses: Dictionary = {}
	if not adb_path.is_empty():
		var adb: Adb = Adb.new(adb_path)
		list = adb.list_devices()
		if port > 0:
			for serial: String in armed:
				reverses[serial] = adb.reverse_list(serial)
	_on_devices.call_deferred(list, not adb_path.is_empty(), reverses)


func _on_devices(list: Array, found: bool, reverses: Dictionary) -> void:
	if _is_shutdown:
		return
	_refreshing = false
	adb_found = found
	devices = Logic.merge_devices(list, OS.has_environment(ENV_LOOPBACK_FILE))
	var ready: Array = Logic.ready_serials(list)
	for serial: String in Logic.armed_to_drop(_armed.keys(), ready, reverses, _port):
		_armed.erase(serial)
	for serial: String in _failed.keys():
		if not ready.has(serial):
			_failed.erase(serial)
	if is_listening() and _pending_install.is_empty():
		for serial: String in Logic.serials_to_arm(ready, _armed.keys(), _arming.keys(), _failed.keys()):
			_arm(serial, false)
	devices_changed.emit()
	state_changed.emit()


func _task_cleanup_stale(adb_path: String) -> void:
	if adb_path.is_empty():
		return
	var adb: Adb = Adb.new(adb_path)
	for device: Dictionary in adb.list_devices():
		if str(device["state"]) == "device":
			adb.remove_stale_reverses(str(device["serial"]))


# ---- Preparing a device ----------------------------------------------------

func _arm(serial: String, uninstall_first: bool) -> void:
	var adb_path: String = _find_adb()
	if adb_path.is_empty():
		return
	_arming[serial] = true
	var apk_global: String = ProjectSettings.globalize_path(APK_PATH) if FileAccess.file_exists(APK_PATH) else ""
	_spawn(_task_arm.bind(adb_path, serial, apk_global, _bundled_version(), _port, _token_hex, _project_name_hex, _project_icon_b64, uninstall_first))


# Called by the dock after the uninstall ConfirmationDialog.
func answer_uninstall(accepted: bool) -> void:
	if _pending_install.is_empty() or _is_shutdown:
		return
	var serial: String = str(_pending_install["serial"])
	_pending_install = {}
	if not accepted:
		_failed[serial] = "Install cancelled. Uninstall the Handview app from the phone, then press Refresh."
		state_changed.emit()
		return
	_arm(serial, true)


# Drops an open uninstall question and tells the dock to close its dialog.
func _clear_pending_install() -> void:
	if _pending_install.is_empty():
		return
	_pending_install = {}
	uninstall_cancelled.emit()


func _bundled_version() -> int:
	if not FileAccess.file_exists(VERSION_PATH):
		return -1
	var text: String = FileAccess.get_file_as_string(VERSION_PATH).strip_edges()
	return text.to_int() if text.is_valid_int() else -1


# Worker: app install or update, adb reverse, then the offer.
func _task_arm(adb_path: String, serial: String, apk_global: String, bundled: int, port: int, token_hex: String, name_hex: String, icon_b64: String, uninstall_first: bool) -> void:
	var adb: Adb = Adb.new(adb_path)
	var result: Dictionary = {"ok": false, "message": "", "confirm": false, "output": "", "serial": serial, "token": token_hex}
	var need_install: bool = false
	if uninstall_first:
		adb.uninstall(serial)
		need_install = true
	else:
		var installed: int = adb.app_version_code(serial)
		if installed < 0:
			if apk_global.is_empty():
				result["message"] = "The Handview app is not installed and this addon has no bundled APK."
				_on_armed.call_deferred(result)
				return
			need_install = true
		elif not apk_global.is_empty() and bundled >= 0 and installed < bundled:
			need_install = true
	if need_install:
		var installed_result: Dictionary = adb.install(serial, apk_global)
		if not bool(installed_result["ok"]):
			var output: String = str(installed_result["output"])
			if bool(installed_result["signature_mismatch"]) and not uninstall_first:
				result["confirm"] = true
				result["output"] = output
			else:
				result["message"] = "Installing the Handview app failed. adb output:\n" + output
			_on_armed.call_deferred(result)
			return
	if not adb.reverse(serial, port):
		result["message"] = "adb reverse failed for port %d." % port
		_on_armed.call_deferred(result)
		return
	var offered: Dictionary = adb.offer(serial, token_hex, port, name_hex, icon_b64)
	if int(offered["exit_code"]) != 0:
		result["message"] = "Could not reach the Handview app. adb output:\n" + str(offered["output"])
		_on_armed.call_deferred(result)
		return
	result["ok"] = true
	_on_armed.call_deferred(result)


func _on_armed(result: Dictionary) -> void:
	if _is_shutdown:
		return
	var serial: String = str(result["serial"])
	_arming.erase(serial)
	if bool(result["confirm"]):
		_pending_install = result
		_failed[serial] = "The installed Handview app was signed with a different key."
		uninstall_requested.emit(serial, str(result["output"]))
		state_changed.emit()
		return
	if not bool(result["ok"]):
		_failed[serial] = str(result["message"])
		state_changed.emit()
		return
	_armed[serial] = str(result["token"])
	# A phone used the token while this device was being prepared.
	if str(result["token"]) != _token_hex:
		_spawn(_task_offer.bind(_find_adb(), serial, _port, _token_hex, _project_name_hex, _project_icon_b64))
	state_changed.emit()


func _task_offer(adb_path: String, serial: String, port: int, token_hex: String, name_hex: String, icon_b64: String) -> void:
	if adb_path.is_empty():
		return
	Adb.new(adb_path).offer(serial, token_hex, port, name_hex, icon_b64)


func _set_link_env(enabled: bool) -> void:
	if enabled and _native_ok:
		OS.set_environment(ENV_LINK_PORT, str(int(_session.call("get_link_port"))))
		OS.set_environment(ENV_LINK_TOKEN, _link_token_hex)
	else:
		OS.unset_environment(ENV_LINK_PORT)
		OS.unset_environment(ENV_LINK_TOKEN)


# ---- Session events --------------------------------------------------------

func _handle_event(event: Dictionary) -> void:
	match str(event.get("type", "")):
		"connected":
			_phone_connected = true
			phone_info = event
			_set_link_env(true)
			_session.call("send_editor_state", _editor_state)
			# The token is used up; arm the next one for a later reconnect.
			_new_token()
			if loopback_requested():
				_write_loopback_file()
			_set_status("Connected to %s" % str(event.get("model", "")))
		"disconnected":
			_phone_connected = false
			phone_info = {}
			_set_link_env(false)
			# A game started from the phone has a hidden window; nobody could see or stop it.
			if _playing and _phone_play:
				EditorInterface.stop_playing_scene()
				_playing = false
				_phone_play = false
				_editor_state = Logic.STATE_STOPPED
				_embed.end_launch()
			_set_status(Logic.session_ended_message(bool(event.get("timed_out", false))))
		"command":
			_handle_command(int(event.get("cmd", 0)))
		"pin_locked":
			# Someone kept guessing: a new PIN replaces the old one.
			_update_wifi()
			_set_status("Too many wrong PINs. The PIN changed.")
		"game_disconnected":
			if Logic.game_disconnect_is_stop(EditorInterface.is_playing_scene()):
				_sync_playing()


func _can_play() -> bool:
	return not str(ProjectSettings.get_setting("application/run/main_scene", "")).is_empty()


func _handle_command(cmd: int) -> void:
	var can_pause: bool = debugger != null and debugger.has_active_session()
	var decision: Dictionary = Logic.decide_command(cmd, _editor_state, _can_play(), can_pause)
	var action: String = str(decision["action"])
	if action == Logic.ACTION_PAUSE or action == Logic.ACTION_RESUME or action == Logic.ACTION_STEP:
		var sent: bool = debugger.next_frame() if action == Logic.ACTION_STEP else debugger.set_suspended(action == Logic.ACTION_PAUSE)
		if not sent:
			_session.call("send_command_result", cmd, Logic.RESULT_IGNORED, _editor_state)
			return
		var previous: int = _editor_state
		_set_editor_state(Logic.state_after(action, _editor_state), false)
		_session.call("send_command_result", cmd, decision["result"], _editor_state)
		if action == Logic.ACTION_PAUSE:
			_session.call("send_stream_paused", PAUSE_REASON_PAUSED)
		if _editor_state != previous:
			_session.call("send_editor_state", _editor_state)
		state_changed.emit()
		return
	if action == Logic.ACTION_RESTART:
		# Stop and start again in the same frame, so the phone never sees the
		# play end; the new run loads the current scripts and scenes.
		EditorInterface.stop_playing_scene()
		_embed.end_launch()
		_do_play()
		_session.call("send_command_result", cmd, decision["result"], _editor_state)
		_session.call("send_editor_state", _editor_state)
		state_changed.emit()
		return
	if action == Logic.ACTION_PLAY:
		_do_play()
	elif action == Logic.ACTION_STOP:
		EditorInterface.stop_playing_scene()
		_playing = false
		_phone_play = false
		_set_editor_state(Logic.STATE_STOPPED, false)
		_embed.end_launch()
		_session.call("send_command_result", cmd, decision["result"], _editor_state)
		_session.call("send_stream_paused", PAUSE_REASON_NOT_PLAYING)
		_session.call("send_editor_state", _editor_state)
		return
	elif cmd == Logic.CMD_PLAY and _editor_state == Logic.STATE_STOPPED and not _can_play():
		_set_status("No main scene is set in the project.")
	_session.call("send_command_result", cmd, decision["result"], _editor_state)


# Play from the phone: no embedding and a hidden window (D6, D13).
func _do_play() -> void:
	OS.set_environment(ENV_HIDE_WINDOW, "1")
	if _embed.begin_launch() and _embed.is_changed():
		_embed_timer = EMBED_RESTORE_DELAY_SEC
	EditorInterface.play_main_scene()
	_hide_unset_pending = true
	_playing = true
	_phone_play = true
	_set_editor_state(Logic.STATE_PLAYING, false)


func _set_editor_state(state: int, announce: bool) -> void:
	_editor_state = state
	if announce and _phone_connected:
		_session.call("send_editor_state", state)


# Follows EditorInterface.is_playing_scene() and tells the phone about changes.
func _sync_playing() -> void:
	var transition: Dictionary = Logic.playing_transition(_playing, EditorInterface.is_playing_scene())
	if transition.is_empty():
		return
	_playing = not _playing
	_editor_state = int(transition["state"])
	if not _playing:
		_phone_play = false
		_embed.end_launch()
	if _phone_connected:
		if bool(transition["stream_paused"]):
			_session.call("send_stream_paused", PAUSE_REASON_NOT_PLAYING)
		_session.call("send_editor_state", _editor_state)
	state_changed.emit()
