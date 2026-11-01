@tool
class_name HandviewAdb
extends RefCounted

# Thin wrapper over the adb executable. Every call here BLOCKS until adb exits
# (OS.execute). Callers must run them on a WorkerThreadPool task, never on the
# editor main thread. Every device-specific call takes the serial explicitly
# and passes it as `-s <serial>`.

const Parse := preload("adb_parse.gd")

const PACKAGE: String = "io.github.h1dr0nn.handview"
const ACTIVITY: String = "io.github.h1dr0nn.handview/.MainActivity"
const PORT_FIRST: int = 38500
const PORT_LAST: int = 38509
const SDK_SETTING: String = "export/android/android_sdk_path"

var adb_path: String = ""


func _init(path: String = "") -> void:
	adb_path = path


# Looks for adb in <sdk_path>/platform-tools, then in PATH. Returns "" if not found.
static func find_adb(sdk_path: String) -> String:
	var names: PackedStringArray = ["adb.exe", "adb"]
	if not sdk_path.strip_edges().is_empty():
		var tools_dir: String = sdk_path.strip_edges().path_join("platform-tools")
		for exe: String in names:
			var candidate: String = tools_dir.path_join(exe)
			if FileAccess.file_exists(candidate):
				return candidate
	var separator: String = ";" if OS.get_name() == "Windows" else ":"
	for raw_dir: String in OS.get_environment("PATH").split(separator, false):
		var dir: String = raw_dir.strip_edges().trim_prefix("\"").trim_suffix("\"")
		if dir.is_empty():
			continue
		for exe: String in names:
			if exe == "adb.exe" and OS.get_name() != "Windows":
				continue
			var candidate: String = dir.path_join(exe)
			if FileAccess.file_exists(candidate):
				return candidate
	return ""


# Android SDK path from Editor Settings, or "" outside the editor.
static func sdk_path_from_editor_settings() -> String:
	if not Engine.is_editor_hint():
		return ""
	var settings: EditorSettings = EditorInterface.get_editor_settings()
	if settings == null or not settings.has_setting(SDK_SETTING):
		return ""
	return str(settings.get_setting(SDK_SETTING))


# 32 random bytes as 64 lowercase hex chars (decision D9).
static func new_token_hex() -> String:
	return Crypto.new().generate_random_bytes(32).hex_encode()


# Runs adb with raw args. Returns {exit_code: int, output: String}, stderr merged.
func run(args: PackedStringArray) -> Dictionary:
	if adb_path.is_empty():
		return {"exit_code": -1, "output": "adb not found"}
	var lines: Array = []
	var code: int = OS.execute(adb_path, args, lines, true)
	var text: String = ""
	for chunk: Variant in lines:
		text += str(chunk)
	return {"exit_code": code, "output": text}


func _run_for(serial: String, args: PackedStringArray) -> Dictionary:
	var full: PackedStringArray = PackedStringArray(["-s", serial])
	full.append_array(args)
	return run(full)


func list_devices() -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	var res: Dictionary = run(PackedStringArray(["devices", "-l"]))
	if int(res["exit_code"]) != 0:
		return result
	for device: Dictionary in Parse.parse_devices(str(res["output"])):
		result.append(device)
	return result


# Installed versionCode of the Handview app, -1 if not installed.
func app_version_code(serial: String) -> int:
	var res: Dictionary = _run_for(serial, PackedStringArray(["shell", "dumpsys", "package", PACKAGE]))
	if int(res["exit_code"]) != 0:
		return -1
	return Parse.parse_version_code(str(res["output"]))


# Returns {ok: bool, signature_mismatch: bool, output: String}.
func install(serial: String, apk_path: String) -> Dictionary:
	var res: Dictionary = _run_for(serial, PackedStringArray(["install", "-r", apk_path]))
	var output: String = str(res["output"])
	return {
		"ok": int(res["exit_code"]) == 0 and Parse.is_install_success(output),
		"signature_mismatch": Parse.is_signature_mismatch(output),
		"output": output,
	}


# Destructive. Only call after the user confirmed.
func uninstall(serial: String) -> Dictionary:
	return _run_for(serial, PackedStringArray(["uninstall", PACKAGE]))


func reverse(serial: String, port: int) -> bool:
	var spec: String = "tcp:%d" % port
	var res: Dictionary = _run_for(serial, PackedStringArray(["reverse", spec, spec]))
	return int(res["exit_code"]) == 0


func reverse_remove(serial: String, port: int) -> void:
	_run_for(serial, PackedStringArray(["reverse", "--remove", "tcp:%d" % port]))


# Ports reversed with the same number on the device and the PC.
func reverse_list(serial: String) -> PackedInt32Array:
	var ports: PackedInt32Array = PackedInt32Array()
	var res: Dictionary = _run_for(serial, PackedStringArray(["reverse", "--list"]))
	if int(res["exit_code"]) != 0:
		return ports
	for entry: Dictionary in Parse.parse_reverse_list(str(res["output"])):
		if int(entry["local"]) == int(entry["remote"]):
			ports.append(int(entry["local"]))
	return ports


# Removes any Handview-range reverse left over from an earlier session.
func remove_stale_reverses(serial: String) -> void:
	for port: int in reverse_list(serial):
		if port >= PORT_FIRST and port <= PORT_LAST:
			reverse_remove(serial, port)


func launch(serial: String, token_hex: String, port: int) -> Dictionary:
	var args: PackedStringArray = PackedStringArray([
		"shell", "am", "start", "-n", ACTIVITY,
		"--es", "token", token_hex,
		"--ei", "port", str(port),
	])
	return _run_for(serial, args)


# First port in the session range free on the PC and not reversed on this device. -1 if none.
func pick_free_port(serial: String) -> int:
	var reversed: PackedInt32Array = reverse_list(serial)
	for port: int in range(PORT_FIRST, PORT_LAST + 1):
		if reversed.has(port):
			continue
		if _port_free(port):
			return port
	return -1


static func _port_free(port: int) -> bool:
	var server: TCPServer = TCPServer.new()
	if server.listen(port, "127.0.0.1") != OK:
		return false
	server.stop()
	return true


# True when nothing on the PC listens on this loopback port.
static func is_port_free(port: int) -> bool:
	return _port_free(port)
