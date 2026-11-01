@tool
class_name HandviewAdbParse
extends RefCounted

# Pure parsers for adb output. No process or engine state is touched here.


static func _tokens(line: String) -> PackedStringArray:
	return line.replace("\t", " ").strip_edges().split(" ", false)


# Parses `adb devices -l`. Returns Array of {serial, state, model, product, transport_id}.
static func parse_devices(output: String) -> Array:
	var result: Array = []
	for raw_line: String in output.replace("\r", "").split("\n"):
		var line: String = raw_line.strip_edges()
		if line.is_empty() or line.begins_with("*") or line.begins_with("List of devices"):
			continue
		if line.begins_with("adb ") or line.begins_with("error:"):
			continue
		var parts: PackedStringArray = _tokens(line)
		if parts.size() < 2:
			continue
		var state: String = parts[1]
		var next_index: int = 2
		if state == "no" and parts.size() > 2 and parts[2] == "permissions":
			state = "no permissions"
			next_index = 3
		var device: Dictionary = {
			"serial": parts[0],
			"state": state,
			"model": "",
			"product": "",
			"transport_id": "",
		}
		for i: int in range(next_index, parts.size()):
			var sep: int = parts[i].find(":")
			if sep <= 0:
				continue
			var key: String = parts[i].substr(0, sep)
			var value: String = parts[i].substr(sep + 1)
			if key == "model":
				device["model"] = value
			elif key == "product":
				device["product"] = value
			elif key == "transport_id":
				device["transport_id"] = value
		result.append(device)
	return result


# Reads the first versionCode=NN from `dumpsys package` output. -1 when absent.
static func parse_version_code(dumpsys_output: String) -> int:
	var regex: RegEx = RegEx.new()
	regex.compile("versionCode=(\\d+)")
	var found: RegExMatch = regex.search(dumpsys_output)
	if found == null:
		return -1
	return found.get_string(1).to_int()


static func _tcp_port(token: String) -> int:
	if not token.begins_with("tcp:"):
		return -1
	var digits: String = token.substr(4)
	if not digits.is_valid_int():
		return -1
	return digits.to_int()


# Parses `adb reverse --list`, lines `<prefix> <remote> <local>` (remote is the device side,
# local the PC side). Returns Array of {serial, local, remote}, tcp entries only.
static func parse_reverse_list(output: String) -> Array:
	var result: Array = []
	for raw_line: String in output.replace("\r", "").split("\n"):
		var parts: PackedStringArray = _tokens(raw_line)
		if parts.size() != 3:
			continue
		var remote_port: int = _tcp_port(parts[1])
		var local_port: int = _tcp_port(parts[2])
		if local_port < 0 or remote_port < 0:
			continue
		var serial: String = parts[0]
		if serial == "(reverse)":
			serial = ""
		result.append({"serial": serial, "local": local_port, "remote": remote_port})
	return result


static func is_signature_mismatch(install_output: String) -> bool:
	var text: String = install_output.to_lower()
	return (
		text.contains("install_failed_update_incompatible")
		or text.contains("signatures do not match")
		or text.contains("install_parse_failed_inconsistent_certificates")
	)


static func is_install_success(output: String) -> bool:
	for raw_line: String in output.replace("\r", "").split("\n"):
		if raw_line.strip_edges() == "Success":
			return true
	return false
