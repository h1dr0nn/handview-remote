extends RefCounted

# The editor's offer flow (DECISIONS D18): which devices get prepared, which
# are prepared again, and the broadcast the app receives.

const Logic := preload("res://addons/handview/editor/session_logic.gd")
const Adb := preload("res://addons/handview/editor/adb.gd")

var failures: Array = []


func _eq(actual: Variant, expected: Variant, label: String) -> void:
	if typeof(actual) != typeof(expected) or actual != expected:
		failures.append("%s: expected %s, got %s" % [label, str(expected), str(actual)])


func test_ready_serials() -> void:
	var devices: Array = [
		{"serial": "A", "state": "device"},
		{"serial": "B", "state": "unauthorized"},
		{"serial": "C", "state": "offline"},
	]
	_eq(Logic.ready_serials(devices), ["A"], "only usable devices")


func test_new_devices_are_armed_once() -> void:
	_eq(Logic.serials_to_arm(["A", "B"], [], [], []), ["A", "B"], "both new")
	_eq(Logic.serials_to_arm(["A", "B"], ["A"], [], []), ["B"], "A already prepared")
	_eq(Logic.serials_to_arm(["A", "B"], [], ["A"], []), ["B"], "A being prepared")
	_eq(Logic.serials_to_arm(["A", "B"], [], [], ["B"]), ["A"], "B failed, waits for Refresh")


func test_unplugged_device_is_dropped() -> void:
	_eq(Logic.armed_to_drop(["A", "B"], ["A"], {}, 38500), ["B"], "B unplugged")


func test_lost_reverse_rearms() -> void:
	var reverses: Dictionary = {"A": PackedInt32Array([38500]), "B": PackedInt32Array([38501])}
	_eq(Logic.armed_to_drop(["A", "B"], ["A", "B"], reverses, 38500), ["B"], "B lost this editor's reverse")
	_eq(Logic.armed_to_drop(["A"], ["A"], {}, 38500), [], "unchecked devices are kept")


func test_offer_broadcast() -> void:
	var args: PackedStringArray = Adb.offer_args("ab".repeat(32), 38501, "57696c64", "iVBOR")
	_eq(args[0], "shell", "shell command")
	_eq(args.has("io.github.h1dr0nn.handview/.SessionOfferReceiver"), true, "explicit receiver")
	_eq(args[args.find("-f") + 1], "32", "reaches a never opened app")
	_eq(args[args.find("--ei") + 2], "38501", "port")
	_eq(args[args.find("icon_b64") + 1], "iVBOR", "icon")
	_eq(Adb.offer_args("ab".repeat(32), 38501, "57").has("icon_b64"), false, "no icon, no extra")
