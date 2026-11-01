@tool
class_name HandviewSessionController
extends RefCounted

# Editor side session logic: device list, USB launch (SPEC section 5), phone
# session events and Play / Pause / Step / Stop control (SPEC 6.4). Every adb call runs on a
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
const METADATA_SECTION: String = "handview"
const METADATA_SERIAL: String = "serial"

const ENV_LINK_PORT: String = "HANDVIEW_LINK_PORT"
const ENV_LINK_TOKEN: String = "HANDVIEW_LINK_TOKEN"
const ENV_HIDE_WINDOW: String = "HANDVIEW_HIDE_WINDOW"
const ENV_LOOPBACK_FILE: String = "HANDVIEW_LOOPBACK_FILE"
const ENV_LOOPBACK_AUTOSTART: String = "HANDVIEW_LOOPBACK_AUTOSTART"

var devices: Array = []
var adb_found: bool = true
var selected_serial: String = ""
var status: String = ""
var encoder_text: String = ""
var phone_info: Dictionary = {}
# Set by the plugin; pauses and steps the game. Null disables Pause and Step.
var debugger: Debugger = null

var _native_ok: bool = false
# Native HandviewEditorSession. Untyped so the script parses without the library.
var _session: Object = null
var _encoder: int = -1
var _embed: EmbedToggle = EmbedToggle.new()

var _tasks: Array[int] = []
var _generation: int = 0
var _busy: bool = false
var _refreshing: bool = false
var _refresh_timer: float = 0.0
var _phone_connected: bool = false
# True from the launch until the phone connects: a disconnect then is the old session being replaced.
var _awaiting_phone: bool = false
var _active_serial: String = ""
var _active_port: int = -1
var _link_token_hex: String = ""
var _pending_install: Dictionary = {}
# Seconds since a successful launch while waiting for the phone. Negative = not running.
var _launch_timer: float = -1.0
var _is_shutdown: bool = false

var _editor_state: int = Logic.STATE_STOPPED
var _playing: bool = false
var _hide_unset_pending: bool = false
var _embed_timer: float = 0.0


func _init() -> void:
	_native_ok = ClassDB.class_exists("HandviewEditorSession")
	if _native_ok:
		_session = ClassDB.instantiate("HandviewEditorSession")
	if _session == null:
		_native_ok = false


# True when the loopback test hook should start a session automatically.
func loopback_requested() -> bool:
	return OS.has_environment(ENV_LOOPBACK_FILE) and OS.get_environment(ENV_LOOPBACK_AUTOSTART) == "1"


# Called once by the plugin after the dock is connected.
func start() -> void:
	if not _native_ok:
		_set_status("The native library is not available on this platform.")
		return
	_encoder = int(_session.call("probe_encoder"))
	encoder_text = Logic.encoder_line(_encoder)
	selected_serial = _remembered_serial()
	_spawn(_task_cleanup_stale.bind(_find_adb()))
	refresh_devices()
	state_changed.emit()
	# Test hook for automated end-to-end runs with tools/mock-client.
	if OS.has_environment(ENV_LOOPBACK_FILE) and OS.get_environment(ENV_LOOPBACK_AUTOSTART) == "1":
		start_session_for(Logic.LOOPBACK_SERIAL)


# Stops everything and clears what this controller changed. Blocks briefly (exit path).
func shutdown() -> void:
	_is_shutdown = true
	_generation += 1
	_launch_timer = -1.0
	_awaiting_phone = false
	_clear_pending_install()
	_embed.end_launch()
	OS.unset_environment(ENV_HIDE_WINDOW)
	_set_link_env(false)
	if _native_ok and bool(_session.call("is_running")):
		_session.call("stop")
	if not _active_serial.is_empty() and _active_serial != Logic.LOOPBACK_SERIAL:
		var path: String = _find_adb()
		if not path.is_empty():
			Adb.new(path).reverse_remove(_active_serial, _active_port)
	_active_serial = ""
	for id: int in _tasks:
		WorkerThreadPool.wait_for_task_completion(id)
	_tasks.clear()


func is_native_available() -> bool:
	return _native_ok


func is_busy() -> bool:
	return _busy


func is_session_active() -> bool:
	return _native_ok and bool(_session.call("is_running"))


func is_phone_connected() -> bool:
	return _phone_connected


func selected_state() -> String:
	for device: Dictionary in devices:
		if str(device["serial"]) == selected_serial:
			return str(device["state"])
	return ""


func select_serial(serial: String) -> void:
	selected_serial = serial
	var settings: EditorSettings = EditorInterface.get_editor_settings()
	if settings != null:
		settings.set_project_metadata(METADATA_SECTION, METADATA_SERIAL, serial)
	state_changed.emit()


func _remembered_serial() -> String:
	var settings: EditorSettings = EditorInterface.get_editor_settings()
	if settings == null:
		return ""
	return str(settings.get_project_metadata(METADATA_SECTION, METADATA_SERIAL, ""))


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
	if _launch_timer >= 0.0:
		_launch_timer += delta
		if Logic.launch_wait_expired(_awaiting_phone, _launch_timer):
			_on_launch_timeout()
	if _hide_unset_pending:
		_hide_unset_pending = false
		OS.unset_environment(ENV_HIDE_WINDOW)
	if not is_session_active():
		_refresh_timer += delta
		if _refresh_timer >= REFRESH_INTERVAL_SEC:
			refresh_devices()
	if _embed.is_changed():
		_embed_timer -= delta
		if _embed_timer <= 0.0:
			_embed.end_launch()
	if is_session_active():
		for event: Dictionary in _session.call("poll"):
			_handle_event(event)
			if not is_session_active():
				break
	_sync_playing()


# ---- Device list -----------------------------------------------------------

func refresh_devices() -> void:
	_refresh_timer = 0.0
	if _refreshing or not _native_ok:
		return
	_refreshing = true
	_spawn(_task_refresh.bind(_find_adb(), _generation))


func _task_refresh(adb_path: String, gen: int) -> void:
	var list: Array = []
	if not adb_path.is_empty():
		list = Adb.new(adb_path).list_devices()
	_on_devices.call_deferred(list, not adb_path.is_empty(), gen)


func _on_devices(list: Array, found: bool, gen: int) -> void:
	if _is_shutdown:
		return
	_refreshing = false
	if gen != _generation:
		return
	adb_found = found
	devices = Logic.merge_devices(list, OS.has_environment(ENV_LOOPBACK_FILE))
	var known: bool = false
	for device: Dictionary in devices:
		if str(device["serial"]) == selected_serial:
			known = true
	if not known:
		var index: int = Logic.preselect_index(devices, _remembered_serial())
		selected_serial = str(devices[index]["serial"]) if index >= 0 else ""
	devices_changed.emit()
	state_changed.emit()


func _task_cleanup_stale(adb_path: String) -> void:
	if adb_path.is_empty():
		return
	var adb: Adb = Adb.new(adb_path)
	for device: Dictionary in adb.list_devices():
		if str(device["state"]) == "device":
			adb.remove_stale_reverses(str(device["serial"]))


# ---- Start and stop --------------------------------------------------------

func start_session_for(serial: String) -> void:
	if _busy or not _native_ok:
		return
	if serial == Logic.LOOPBACK_SERIAL:
		_start_loopback()
		return
	var adb_path: String = _find_adb()
	if adb_path.is_empty():
		adb_found = false
		_set_status(Logic.SDK_HINT)
		return
	if is_session_active() and _active_serial != serial:
		stop_session()
	selected_serial = serial
	_busy = true
	_generation += 1
	var reuse_port: int = int(_session.call("get_port")) if is_session_active() else -1
	var apk_global: String = ""
	if FileAccess.file_exists(APK_PATH):
		apk_global = ProjectSettings.globalize_path(APK_PATH)
	_set_status("Preparing the phone...")
	_spawn(_task_prepare.bind(adb_path, serial, apk_global, _bundled_version(), reuse_port, false, _generation))


func stop_session() -> void:
	_generation += 1
	_busy = false
	_awaiting_phone = false
	_launch_timer = -1.0
	_clear_pending_install()
	var serial: String = _active_serial
	var port: int = _active_port
	if _native_ok and bool(_session.call("is_running")):
		_session.call("stop")
	_end_phone_session(serial, port)
	_set_status(Logic.session_ended_message(false))


# Called by the dock after the uninstall ConfirmationDialog.
func answer_uninstall(accepted: bool) -> void:
	if _pending_install.is_empty() or _is_shutdown:
		return
	var request: Dictionary = _pending_install
	_pending_install = {}
	if int(request["gen"]) != _generation:
		return
	if not accepted:
		_busy = false
		_set_status("Install cancelled. Uninstall the Handview app from the phone to continue.")
		return
	_set_status("Replacing the Handview app...")
	_spawn(_task_prepare.bind(request["adb_path"], request["serial"], request["apk"], request["bundled"], request["port"], true, _generation))


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


# Worker: device state, app install, port choice.
func _task_prepare(adb_path: String, serial: String, apk_global: String, bundled: int, reuse_port: int, uninstall_first: bool, gen: int) -> void:
	var adb: Adb = Adb.new(adb_path)
	var result: Dictionary = {
		"gen": gen, "ok": false, "message": "", "confirm": false, "output": "",
		"serial": serial, "port": reuse_port, "adb_path": adb_path, "apk": apk_global, "bundled": bundled,
	}
	var state: String = ""
	for device: Dictionary in adb.list_devices():
		if str(device["serial"]) == serial:
			state = str(device["state"])
	if state != "device":
		result["message"] = Logic.device_hint(state, true)
		_on_prepared.call_deferred(result)
		return
	var need_install: bool = false
	if uninstall_first:
		adb.uninstall(serial)
		need_install = true
	else:
		var installed: int = adb.app_version_code(serial)
		if installed < 0:
			if apk_global.is_empty():
				result["message"] = "The Handview app is not installed and this addon has no bundled APK."
				_on_prepared.call_deferred(result)
				return
			need_install = true
		elif not apk_global.is_empty() and bundled >= 0 and installed < bundled:
			need_install = true
	if need_install:
		_on_install_started.call_deferred(gen)
		var installed_result: Dictionary = adb.install(serial, apk_global)
		if not bool(installed_result["ok"]):
			var output: String = str(installed_result["output"])
			if bool(installed_result["signature_mismatch"]) and not uninstall_first:
				result["confirm"] = true
				result["output"] = output
			else:
				result["message"] = "Installing the Handview app failed. adb output:\n" + output
			_on_prepared.call_deferred(result)
			return
	if reuse_port < 0:
		var port: int = adb.pick_free_port(serial)
		if port < 0:
			result["message"] = "No free port between %d and %d." % [Adb.PORT_FIRST, Adb.PORT_LAST]
			_on_prepared.call_deferred(result)
			return
		result["port"] = port
	result["ok"] = true
	_on_prepared.call_deferred(result)


func _on_install_started(gen: int) -> void:
	if _is_shutdown or gen != _generation:
		return
	_set_status("Installing the Handview app...")


func _on_prepared(result: Dictionary) -> void:
	if _is_shutdown or int(result["gen"]) != _generation:
		return
	if bool(result["confirm"]):
		_pending_install = result
		_set_status("The installed Handview app was signed with a different key.")
		uninstall_requested.emit(str(result["serial"]), str(result["output"]))
		return
	if not bool(result["ok"]):
		_busy = false
		_set_status(str(result["message"]))
		return
	_begin_native_session(str(result["serial"]), int(result["port"]), str(result["adb_path"]))


# Main thread: listener, tokens and stream info, then the adb launch.
func _begin_native_session(serial: String, port: int, adb_path: String) -> void:
	var token: String = _prepare_listener(port)
	if token.is_empty():
		_busy = false
		_set_status("Could not listen on port %d." % port)
		return
	_active_serial = serial
	_active_port = port
	_awaiting_phone = true
	_set_status("Starting the Handview app...")
	_spawn(_task_launch.bind(adb_path, serial, port, token, _generation))


# Starts or reuses the listener and arms a new one-time token. Returns its hex, "" on failure.
func _prepare_listener(port: int) -> String:
	var same_port: bool = is_session_active() and int(_session.call("get_port")) == port
	if not same_port:
		if is_session_active():
			_session.call("stop")
		if not bool(_session.call("start", port)):
			return ""
		var link_token: PackedByteArray = Crypto.new().generate_random_bytes(32)
		_session.call("set_link_token", link_token)
		_link_token_hex = link_token.hex_encode()
	var width: int = int(ProjectSettings.get_setting("display/window/size/viewport_width", 1152))
	var height: int = int(ProjectSettings.get_setting("display/window/size/viewport_height", 648))
	var bitrate: int = Logic.default_bitrate_kbps(width, height, STREAM_FPS)
	_session.call("set_stream_info", Logic.announced_encoder(_encoder), width, height, STREAM_FPS, bitrate)
	var token: PackedByteArray = Crypto.new().generate_random_bytes(32)
	_session.call("set_pending_token", token)
	return token.hex_encode()


# Worker: adb reverse, then the app launch.
func _task_launch(adb_path: String, serial: String, port: int, token_hex: String, gen: int) -> void:
	var adb: Adb = Adb.new(adb_path)
	var result: Dictionary = {"gen": gen, "ok": false, "message": "", "serial": serial, "port": port, "adb_path": adb_path}
	if not adb.reverse(serial, port):
		result["message"] = "adb reverse failed for port %d." % port
		_on_launched.call_deferred(result)
		return
	var launched: Dictionary = adb.launch(serial, token_hex, port)
	var output: String = str(launched["output"])
	if Logic.launch_failed(int(launched["exit_code"]), output):
		adb.reverse_remove(serial, port)
		result["message"] = "Starting the Handview app failed. adb output:\n" + output
		_on_launched.call_deferred(result)
		return
	result["ok"] = true
	_on_launched.call_deferred(result)


func _on_launched(result: Dictionary) -> void:
	if _is_shutdown:
		return
	if int(result["gen"]) != _generation:
		# Stopped while launching: make sure no reverse is left behind.
		if bool(result["ok"]):
			_spawn(_task_remove_reverse.bind(str(result["adb_path"]), str(result["serial"]), int(result["port"])))
		return
	_busy = false
	if bool(result["ok"]):
		# The phone may have connected already while this result was queued.
		if _awaiting_phone:
			_launch_timer = 0.0
			_set_status("Waiting for the phone to connect...")
		return
	if is_session_active():
		_session.call("stop")
	_awaiting_phone = false
	_launch_timer = -1.0
	_active_serial = ""
	_active_port = -1
	_set_status(str(result["message"]))


# The launched app never connected: release what the launch took.
func _on_launch_timeout() -> void:
	var serial: String = _active_serial
	var port: int = _active_port
	_launch_timer = -1.0
	_awaiting_phone = false
	if Logic.should_stop_listener_on_timeout(_phone_connected) and is_session_active():
		_session.call("stop")
	_active_serial = ""
	_active_port = -1
	if not serial.is_empty():
		_spawn(_task_remove_reverse.bind(_find_adb(), serial, port))
	_set_status(Logic.LAUNCH_TIMEOUT_MESSAGE)


func _start_loopback() -> void:
	if is_session_active() and _active_serial != Logic.LOOPBACK_SERIAL:
		stop_session()
	var port: int = int(_session.call("get_port")) if is_session_active() else -1
	if port < 0:
		for candidate: int in range(Adb.PORT_FIRST, Adb.PORT_LAST + 1):
			if Adb.is_port_free(candidate):
				port = candidate
				break
	if port < 0:
		_set_status("No free port between %d and %d." % [Adb.PORT_FIRST, Adb.PORT_LAST])
		return
	var token: String = _prepare_listener(port)
	if token.is_empty():
		_set_status("Could not listen on port %d." % port)
		return
	var file: FileAccess = FileAccess.open(OS.get_environment(ENV_LOOPBACK_FILE), FileAccess.WRITE)
	if file == null:
		_session.call("stop")
		_set_status("Could not write the loopback file.")
		return
	file.store_string(Logic.loopback_json(port, token))
	file.close()
	_active_serial = Logic.LOOPBACK_SERIAL
	_active_port = port
	_awaiting_phone = true
	selected_serial = Logic.LOOPBACK_SERIAL
	_set_status("Waiting for the loopback client...")


func _task_remove_reverse(adb_path: String, serial: String, port: int) -> void:
	if adb_path.is_empty() or serial.is_empty() or serial == Logic.LOOPBACK_SERIAL:
		return
	Adb.new(adb_path).reverse_remove(serial, port)


# Clears env and the reverse after a session ended (listener handling is up to the caller).
func _end_phone_session(serial: String, port: int) -> void:
	_phone_connected = false
	phone_info = {}
	_set_link_env(false)
	_active_serial = ""
	_active_port = -1
	if not serial.is_empty():
		_spawn(_task_remove_reverse.bind(_find_adb(), serial, port))


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
			_awaiting_phone = false
			_launch_timer = -1.0
			_phone_connected = true
			phone_info = event
			_set_link_env(true)
			_session.call("send_editor_state", _editor_state)
			_set_status("Connected to %s" % str(event.get("model", "")))
		"disconnected":
			if _awaiting_phone:
				_phone_connected = false
				phone_info = {}
				state_changed.emit()
				return
			_end_phone_session(_active_serial, _active_port)
			_session.call("stop")
			_set_status(Logic.session_ended_message(bool(event.get("timed_out", false))))
		"command":
			_handle_command(int(event.get("cmd", 0)))
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
	if action == Logic.ACTION_PLAY:
		_do_play()
	elif action == Logic.ACTION_STOP:
		EditorInterface.stop_playing_scene()
		_playing = false
		_set_editor_state(Logic.STATE_STOPPED, false)
		_embed.end_launch()
		_session.call("send_command_result", cmd, decision["result"], _editor_state)
		_session.call("send_stream_paused", PAUSE_REASON_NOT_PLAYING)
		_session.call("send_editor_state", _editor_state)
		return
	elif cmd == Logic.CMD_PLAY and _editor_state == Logic.STATE_STOPPED and not _can_play():
		_set_status("No main scene is set in the project.")
	_session.call("send_command_result", cmd, decision["result"], _editor_state)


func _do_play() -> void:
	OS.set_environment(ENV_HIDE_WINDOW, "1")
	if _embed.begin_launch() and _embed.is_changed():
		_embed_timer = EMBED_RESTORE_DELAY_SEC
	EditorInterface.play_main_scene()
	_hide_unset_pending = true
	_playing = true
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
		_embed.end_launch()
	if _phone_connected:
		if bool(transition["stream_paused"]):
			_session.call("send_stream_paused", 0)
		_session.call("send_editor_state", _editor_state)
	state_changed.emit()
