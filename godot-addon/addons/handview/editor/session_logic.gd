@tool
class_name HandviewSessionLogic
extends RefCounted

# Pure decisions of the editor side. No editor, engine or process state is
# touched here so everything can be unit-tested headless.

const CMD_PLAY: int = 1
const CMD_PAUSE: int = 2
const CMD_STEP: int = 3
const CMD_STOP: int = 4

const STATE_STOPPED: int = 0
const STATE_PLAYING: int = 1
const STATE_PAUSED: int = 2
const STATE_COMPILING: int = 3
const STATE_RELOADING: int = 4

const RESULT_OK: int = 0
const RESULT_IGNORED: int = 1
const RESULT_BUSY: int = 2

const ACTION_NONE: String = "none"
const ACTION_PLAY: String = "play"
const ACTION_STOP: String = "stop"
const ACTION_PAUSE: String = "pause"
const ACTION_RESUME: String = "resume"
const ACTION_STEP: String = "step"

const EMBED_LABEL: String = "Embed Game on Next Play"
const LOOPBACK_SERIAL: String = "loopback:test"
const LOOPBACK_LABEL: String = "Loopback (Test)"

const BITRATE_MIN_KBPS: int = 2000
const BITRATE_MAX_KBPS: int = 20000
const DEFAULT_FPS: int = 60

const LAUNCH_TIMEOUT_SEC: float = 20.0
const LAUNCH_TIMEOUT_MESSAGE: String = "The phone did not connect. Check the phone screen and press Start Session again."

const NATIVE_MISSING_MESSAGE: String = "The native library is not loaded. Restart the editor once after installing Handview."

const SDK_HINT: String = "Set the Android SDK path in Editor Settings > Export > Android, or add adb to PATH."


# Which CommandResult and which editor action a phone command gets in a state (SPEC 6.4, D14).
# Pause toggles; Pause and Step need the editor debugger (can_pause).
static func decide_command(cmd: int, state: int, can_play: bool = true, can_pause: bool = true) -> Dictionary:
	if cmd < CMD_PLAY or cmd > CMD_STOP:
		return {"result": RESULT_IGNORED, "action": ACTION_NONE}
	if state == STATE_COMPILING or state == STATE_RELOADING:
		return {"result": RESULT_BUSY, "action": ACTION_NONE}
	if cmd == CMD_PLAY and state == STATE_STOPPED and can_play:
		return {"result": RESULT_OK, "action": ACTION_PLAY}
	if cmd == CMD_STOP and (state == STATE_PLAYING or state == STATE_PAUSED):
		return {"result": RESULT_OK, "action": ACTION_STOP}
	if cmd == CMD_PAUSE and state == STATE_PLAYING and can_pause:
		return {"result": RESULT_OK, "action": ACTION_PAUSE}
	if cmd == CMD_PAUSE and state == STATE_PAUSED and can_pause:
		return {"result": RESULT_OK, "action": ACTION_RESUME}
	if cmd == CMD_STEP and state == STATE_PAUSED and can_pause:
		return {"result": RESULT_OK, "action": ACTION_STEP}
	return {"result": RESULT_IGNORED, "action": ACTION_NONE}


# Editor state to report after an action ran.
static func state_after(action: String, state: int) -> int:
	if action == ACTION_PLAY:
		return STATE_PLAYING
	if action == ACTION_STOP:
		return STATE_STOPPED
	if action == ACTION_PAUSE or action == ACTION_STEP:
		return STATE_PAUSED
	if action == ACTION_RESUME:
		return STATE_PLAYING
	return state


# What to send when is_playing_scene() changes. Empty when nothing changed.
static func playing_transition(was_playing: bool, now_playing: bool) -> Dictionary:
	if was_playing == now_playing:
		return {}
	if now_playing:
		return {"state": STATE_PLAYING, "stream_paused": false}
	return {"state": STATE_STOPPED, "stream_paused": true}


# A game link drop counts as a stop only when the editor no longer plays the scene.
static func game_disconnect_is_stop(is_playing: bool) -> bool:
	return not is_playing


static func default_bitrate_kbps(width: int, height: int, fps: int) -> int:
	return clampi(width * height * fps / 10000, BITRATE_MIN_KBPS, BITRATE_MAX_KBPS)


# H.264 encoder to announce. -1 (none found) falls back to 1 so the phone still
# connects; the game reports the real error.
static func announced_encoder(probed: int) -> int:
	return 1 if probed < 0 else probed


static func encoder_line(probed: int) -> String:
	match probed:
		0:
			return "Encoder: NVENC"
		1:
			return "Encoder: Media Foundation (hardware)"
		2:
			return "Encoder: Media Foundation (software)"
	return "No H.264 encoder found"


# Menu texts the "Embed Game on Next Play" item can have: English plus translations.
static func embed_candidates(extra: PackedStringArray) -> PackedStringArray:
	var result: PackedStringArray = PackedStringArray([EMBED_LABEL])
	for text: String in extra:
		var clean: String = text.strip_edges()
		if not clean.is_empty() and not result.has(clean):
			result.append(clean)
	return result


static func embed_text_matches(item_text: String, candidates: PackedStringArray) -> bool:
	var clean: String = item_text.strip_edges().replace("&", "")
	for candidate: String in candidates:
		if clean == candidate.replace("&", ""):
			return true
	return false


# Index of the first menu text that matches, -1 if none.
static func find_embed_index(texts: PackedStringArray, candidates: PackedStringArray) -> int:
	for i: int in texts.size():
		if embed_text_matches(texts[i], candidates):
			return i
	return -1


static func device_hint(state: String, adb_found: bool) -> String:
	if not adb_found:
		return SDK_HINT
	match state:
		"device":
			return ""
		"unauthorized":
			return "Accept the USB debugging prompt on the phone."
		"offline":
			return "Reconnect the USB cable."
		"no permissions":
			return "Check the USB driver for this device."
	if state.is_empty():
		return "The device is not connected."
	return "The device is not ready (%s)." % state


static func can_start(state: String) -> bool:
	return state == "device"


static func device_label(device: Dictionary) -> String:
	var serial: String = str(device.get("serial", ""))
	if serial == LOOPBACK_SERIAL:
		return LOOPBACK_LABEL
	var model: String = str(device.get("model", "")).replace("_", " ")
	var state: String = str(device.get("state", ""))
	var name: String = serial if model.is_empty() else "%s (%s)" % [model, serial]
	if state == "device":
		return name
	return "%s, %s" % [name, state]


# Adds the loopback test entry (D-test hook) when enabled.
static func merge_devices(devices: Array, include_loopback: bool) -> Array:
	var result: Array = devices.duplicate()
	if include_loopback:
		result.append({
			"serial": LOOPBACK_SERIAL,
			"state": "device",
			"model": "",
			"product": "",
			"transport_id": "",
		})
	return result


# Index to preselect: the remembered serial, else the only device, else -1.
static func preselect_index(devices: Array, remembered: String) -> int:
	for i: int in devices.size():
		if str(devices[i].get("serial", "")) == remembered and not remembered.is_empty():
			return i
	if devices.size() == 1:
		return 0
	return -1


# True when the launched app had LAUNCH_TIMEOUT_SEC to connect and did not.
static func launch_wait_expired(awaiting_phone: bool, elapsed_sec: float) -> bool:
	return awaiting_phone and elapsed_sec >= LAUNCH_TIMEOUT_SEC


# After a launch timeout the listener stops unless a phone is still connected to it.
static func should_stop_listener_on_timeout(phone_connected: bool) -> bool:
	return not phone_connected


# `am start` can exit 0 and still print an error (bad activity, device not found).
static func launch_failed(exit_code: int, output: String) -> bool:
	return exit_code != 0 or output.to_lower().contains("error")


static func session_ended_message(timed_out: bool) -> String:
	if timed_out:
		return "Session ended: the phone stopped responding."
	return "Session ended."


static func loopback_json(port: int, token_hex: String) -> String:
	return JSON.stringify({"port": port, "token": token_hex})


static func phone_line(info: Dictionary) -> String:
	if info.is_empty():
		return ""
	return "Phone: %s, %dx%d" % [str(info.get("model", "")), int(info.get("screen_w", 0)), int(info.get("screen_h", 0))]
