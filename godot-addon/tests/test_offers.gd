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


func test_pin_token_is_sha256() -> void:
	_eq(Logic.pin_token("123456").hex_encode(), "handview-pin:123456".sha256_text(), "sha256 of the PIN string")
	_eq(Logic.new_pin().length(), 6, "six digits")
	_eq(Logic.new_pin().is_valid_int(), true, "digits only")


func test_lan_addresses_keep_private_ipv4() -> void:
	var found: PackedStringArray = Logic.lan_addresses(PackedStringArray(["127.0.0.1", "192.168.1.10", "fe80::1", "10.0.0.5", "8.8.8.8", "172.20.1.2", "172.40.0.1"]))
	_eq(found, PackedStringArray(["10.0.0.5", "172.20.1.2", "192.168.1.10"]), "private only, sorted")


func test_wifi_link_and_beacon() -> void:
	_eq(Logic.wifi_link("192.168.1.10", 38500, "ab", "57"), "handview://wifi?h=192.168.1.10&p=38500&t=ab&n=57", "link")
	var beacon: Dictionary = JSON.parse_string(Logic.beacon_json("Wild Paws", 38500, PackedStringArray(["192.168.1.10"])))
	_eq(int(beacon["port"]), 38500, "beacon port")
	_eq(beacon["name"], "Wild Paws", "beacon name")
	_eq(beacon.has("token"), false, "no token in the beacon")


func test_qr_image_has_quiet_zone() -> void:
	var image: Image = Logic.qr_image({"size": 2, "data": PackedByteArray([1, 0, 0, 1])})
	_eq(image.get_width(), (2 + Logic.QR_QUIET_ZONE * 2) * Logic.QR_SCALE, "scaled side")
	_eq(image.get_pixel(0, 0).r, 1.0, "white margin")
	_eq(image.get_pixel(Logic.QR_QUIET_ZONE * Logic.QR_SCALE, Logic.QR_QUIET_ZONE * Logic.QR_SCALE).r, 0.0, "dark module")
