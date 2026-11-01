extends RefCounted

const Runtime := preload("res://addons/handview/runtime/handview_runtime.gd")

var failures: Array = []


func _eq(actual: Variant, expected: Variant, label: String) -> void:
	if actual != expected:
		failures.append("%s: expected %s, got %s" % [label, str(expected), str(actual)])


func test_tall_view_keeps_width() -> void:
	_eq(Runtime.fit_size(Vector2i(540, 1200), Vector2i(1080, 1980)), Vector2i(540, 990), "portrait stage")
	_eq(Runtime.fit_size(Vector2i(540, 1200), Vector2i(1080, 2400)), Vector2i(540, 1200), "full screen")


func test_wide_view_keeps_height() -> void:
	_eq(Runtime.fit_size(Vector2i(1280, 720), Vector2i(2208, 1000)), Vector2i(1590, 720), "landscape stage")


func test_invalid_view_keeps_base() -> void:
	_eq(Runtime.fit_size(Vector2i(540, 1200), Vector2i(0, 0)), Vector2i(540, 1200), "no view")
