extends RefCounted

const Parse := preload("res://addons/handview/editor/adb_parse.gd")
const Adb := preload("res://addons/handview/editor/adb.gd")

var failures: Array = []


func _eq(actual: Variant, expected: Variant, label: String) -> void:
	if typeof(actual) != typeof(expected) or actual != expected:
		failures.append("%s: expected %s, got %s" % [label, str(expected), str(actual)])


func _true(value: bool, label: String) -> void:
	if not value:
		failures.append("%s: expected true" % label)


func test_devices_single_ok() -> void:
	var out: String = "List of devices attached\nR58M12ABCDE            device usb:1-2 product:a52sxq model:SM_A528B device:a52s transport_id:3\n\n"
	var devices: Array = Parse.parse_devices(out)
	_eq(devices.size(), 1, "count")
	_eq(devices[0]["serial"], "R58M12ABCDE", "serial")
	_eq(devices[0]["state"], "device", "state")
	_eq(devices[0]["model"], "SM_A528B", "model")
	_eq(devices[0]["product"], "a52sxq", "product")
	_eq(devices[0]["transport_id"], "3", "transport")


func test_devices_unauthorized_and_offline() -> void:
	var out: String = "List of devices attached\nABC123\tunauthorized usb:1-1 transport_id:1\nXYZ789\toffline transport_id:2\n"
	var devices: Array = Parse.parse_devices(out)
	_eq(devices.size(), 2, "count")
	_eq(devices[0]["state"], "unauthorized", "state 0")
	_eq(devices[0]["model"], "", "model 0 empty")
	_eq(devices[1]["serial"], "XYZ789", "serial 1")
	_eq(devices[1]["state"], "offline", "state 1")


func test_devices_two_with_daemon_lines() -> void:
	var out: String = "* daemon not running; starting now at tcp:5037\n* daemon started successfully\nList of devices attached\r\nemulator-5554          device product:sdk_gphone64_x86_64 model:sdk_gphone64_x86_64 device:emu64xa transport_id:1\r\n0123456789ABCDEF       device usb:2-3 product:raven model:Pixel_6_Pro device:raven transport_id:4\r\n"
	var devices: Array = Parse.parse_devices(out)
	_eq(devices.size(), 2, "count")
	_eq(devices[0]["serial"], "emulator-5554", "emulator serial")
	_eq(devices[0]["model"], "sdk_gphone64_x86_64", "emulator model")
	_eq(devices[1]["serial"], "0123456789ABCDEF", "phone serial")
	_eq(devices[1]["model"], "Pixel_6_Pro", "phone model")
	_eq(devices[1]["transport_id"], "4", "phone transport")


func test_devices_empty() -> void:
	_eq(Parse.parse_devices("List of devices attached\n\n").size(), 0, "header only")
	_eq(Parse.parse_devices("").size(), 0, "empty string")


func test_devices_no_permissions() -> void:
	var out: String = "List of devices attached\nABC123 no permissions (user in plugdev group; are your udev rules wrong?); see [http://developer.android.com/tools/device.html] usb:1-1\n"
	var devices: Array = Parse.parse_devices(out)
	_eq(devices.size(), 1, "count")
	_eq(devices[0]["state"], "no permissions", "state")


func test_version_code_present() -> void:
	var out: String = "Packages:\n  Package [io.github.h1dr0nn.handview] (a1b2c3):\n    userId=10234\n    versionCode=12 minSdk=26 targetSdk=34\n    versionName=0.1.0\n  Hidden system packages:\n    versionCode=99 minSdk=1\n"
	_eq(Parse.parse_version_code(out), 12, "first versionCode")


func test_version_code_missing() -> void:
	_eq(Parse.parse_version_code("Unable to find package: io.github.h1dr0nn.handview"), -1, "missing package")
	_eq(Parse.parse_version_code("Packages:\n"), -1, "no versionCode")
	_eq(Parse.parse_version_code(""), -1, "empty")


func test_reverse_list_serial_format() -> void:
	var out: String = "R58M12ABCDE tcp:38500 tcp:38500\nR58M12ABCDE tcp:38502 tcp:9000\n"
	var entries: Array = Parse.parse_reverse_list(out)
	_eq(entries.size(), 2, "count")
	_eq(entries[0]["serial"], "R58M12ABCDE", "serial")
	_eq(entries[0]["local"], 38500, "local 0")
	_eq(entries[0]["remote"], 38500, "remote 0")
	_eq(entries[1]["remote"], 38502, "remote 1 is the device side")
	_eq(entries[1]["local"], 9000, "local 1 is the PC side")


func test_reverse_list_reverse_format() -> void:
	var out: String = "(reverse) tcp:38501 tcp:38501\r\n(reverse) tcp:38505 tcp:38505\r\n"
	var entries: Array = Parse.parse_reverse_list(out)
	_eq(entries.size(), 2, "count")
	_eq(entries[0]["local"], 38501, "local 0")
	_eq(entries[1]["remote"], 38505, "remote 1")
	var mixed: Array = Parse.parse_reverse_list("(reverse) tcp:38503 tcp:7000\n")
	_eq(mixed[0]["remote"], 38503, "mixed remote")
	_eq(mixed[0]["local"], 7000, "mixed local")


func test_reverse_list_empty_and_noise() -> void:
	_eq(Parse.parse_reverse_list("").size(), 0, "empty")
	_eq(Parse.parse_reverse_list("\n\n").size(), 0, "blank lines")
	_eq(Parse.parse_reverse_list("error: closed\n").size(), 0, "error line")
	_eq(Parse.parse_reverse_list("(reverse) localabstract:foo tcp:1\n").size(), 0, "non tcp skipped")


func test_install_success() -> void:
	var out: String = "Performing Streamed Install\nSuccess\n"
	_true(Parse.is_install_success(out), "success")
	_true(not Parse.is_signature_mismatch(out), "no mismatch")


func test_install_update_incompatible() -> void:
	var out: String = "Performing Streamed Install\nadb: failed to install handview.apk: Failure [INSTALL_FAILED_UPDATE_INCOMPATIBLE: Package io.github.h1dr0nn.handview signatures do not match previously installed version; ignoring!]\n"
	_true(Parse.is_signature_mismatch(out), "mismatch")
	_true(not Parse.is_install_success(out), "not success")
	_true(Parse.is_signature_mismatch("Failure: signatures do not match the previously installed version"), "plain text mismatch")


func test_install_generic_failure() -> void:
	var out: String = "Performing Streamed Install\nadb: failed to install handview.apk: Failure [INSTALL_FAILED_INSUFFICIENT_STORAGE]\n"
	_true(not Parse.is_install_success(out), "not success")
	_true(not Parse.is_signature_mismatch(out), "not mismatch")
	_true(not Parse.is_install_success(""), "empty output")


func test_new_token_hex() -> void:
	var first: String = Adb.new_token_hex()
	var second: String = Adb.new_token_hex()
	_eq(first.length(), 64, "length")
	var regex: RegEx = RegEx.new()
	regex.compile("^[0-9a-f]{64}$")
	_true(regex.search(first) != null, "lowercase hex")
	_true(first != second, "tokens differ")


func test_find_adb_missing() -> void:
	var found: String = Adb.find_adb("Z:/definitely/not/a/sdk")
	if found != "":
		_true(FileAccess.file_exists(found), "found path exists")
	_eq(Adb.sdk_path_from_editor_settings(), "", "no editor settings outside editor")
