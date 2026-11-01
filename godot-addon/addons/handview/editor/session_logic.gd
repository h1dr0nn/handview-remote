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


# Serials adb reports as usable ("device").
static func ready_serials(devices: Array) -> Array:
	var result: Array = []
	for device: Dictionary in devices:
		if str(device.get("state", "")) == "device":
			result.append(str(device.get("serial", "")))
	return result


# Prepared devices to prepare again: unplugged or unauthorized ones, and ones
# whose reverse for `port` is gone (an adb server restart drops all of them).
# `reverses` maps a serial to its reversed ports; serials missing from it were
# not checked and are kept.
static func armed_to_drop(armed: Array, ready: Array, reverses: Dictionary, port: int) -> Array:
	var result: Array = []
	for serial: String in armed:
		if not ready.has(serial) or (reverses.has(serial) and not (reverses[serial] as PackedInt32Array).has(port)):
			result.append(serial)
	return result


# Usable devices that are neither prepared, being prepared, nor failed.
static func serials_to_arm(ready: Array, armed: Array, arming: Array, failed: Array) -> Array:
	var result: Array = []
	for serial: String in ready:
		if not armed.has(serial) and not arming.has(serial) and not failed.has(serial):
			result.append(serial)
	return result


# ---- Wi-Fi (DECISIONS D22) -------------------------------------------------

const BEACON_PORT: int = 38510
const QR_QUIET_ZONE: int = 4
const QR_SCALE: int = 6


static func new_pin() -> String:
	var bytes: PackedByteArray = Crypto.new().generate_random_bytes(4)
	return "%06d" % (bytes.decode_u32(0) % 1000000)


# The token a phone derives from the PIN: SHA-256 of "handview-pin:<pin>".
static func pin_token(pin: String) -> PackedByteArray:
	var hashing: HashingContext = HashingContext.new()
	hashing.start(HashingContext.HASH_SHA256)
	hashing.update(("handview-pin:" + pin).to_utf8_buffer())
	return hashing.finish()


# Private IPv4 addresses of this PC, the ones a phone on the LAN can reach.
static func lan_addresses(addresses: PackedStringArray) -> PackedStringArray:
	var result: PackedStringArray = PackedStringArray()
	for address: String in addresses:
		var parts: PackedStringArray = address.split(".")
		if parts.size() != 4:
			continue
		var a: int = parts[0].to_int()
		var b: int = parts[1].to_int()
		if a == 10 or (a == 192 and b == 168) or (a == 172 and b >= 16 and b <= 31):
			result.append(address)
	result.sort()
	return result


# What the QR code holds: everything the app needs to join over Wi-Fi.
static func wifi_link(address: String, port: int, token_hex: String, name_hex: String) -> String:
	return "handview://wifi?h=%s&p=%d&t=%s&n=%s" % [address, port, token_hex, name_hex]


static func beacon_json(project_name: String, port: int, addresses: PackedStringArray) -> String:
	return JSON.stringify({"handview": 1, "name": project_name.left(48), "port": port, "hosts": Array(addresses)})


# make_qr() modules to a black on white image with a quiet zone, scaled up.
static func qr_image(qr: Dictionary) -> Image:
	var size: int = int(qr.get("size", 0))
	var data: PackedByteArray = qr.get("data", PackedByteArray())
	if size <= 0 or data.size() != size * size:
		return null
	var side: int = size + QR_QUIET_ZONE * 2
	var image: Image = Image.create_empty(side, side, false, Image.FORMAT_L8)
	image.fill(Color.WHITE)
	for y: int in size:
		for x: int in size:
			if data[y * size + x] == 1:
				image.set_pixel(x + QR_QUIET_ZONE, y + QR_QUIET_ZONE, Color.BLACK)
	image.resize(side * QR_SCALE, side * QR_SCALE, Image.INTERPOLATE_NEAREST)
	return image


# Project name for the offer, hex encoded UTF-8 so adb shell quoting cannot break it.
static func offer_name_hex(project_name: String) -> String:
	var name: String = project_name.strip_edges()
	if name.is_empty():
		name = "Godot"
	return name.left(48).to_utf8_buffer().hex_encode()


static func session_ended_message(timed_out: bool) -> String:
	if timed_out:
		return "The phone stopped responding. It can join again from the Handview app."
	return "The phone left. It can join again from the Handview app."


static func loopback_json(port: int, token_hex: String, pin: String = "") -> String:
	var data: Dictionary = {"port": port, "token": token_hex}
	if not pin.is_empty():
		data["pin"] = pin
	return JSON.stringify(data)


static func phone_line(info: Dictionary) -> String:
	if info.is_empty():
		return ""
	return "Phone: %s, %dx%d" % [str(info.get("model", "")), int(info.get("screen_w", 0)), int(info.get("screen_h", 0))]
