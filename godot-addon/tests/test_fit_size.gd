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


func test_safe_rect_scales_insets() -> void:
	# 1080x2400 phone with a 118 px cutout and 63 px bottom bar, window 540x1200.
	_eq(Runtime.safe_rect(Vector2i(540, 1200), Vector2i(1080, 2400), Vector4i(0, 118, 0, 63)), Rect2i(0, 59, 540, 1109), "portrait insets")
	_eq(Runtime.safe_rect(Vector2i(540, 990), Vector2i(1080, 1980), Vector4i.ZERO), Rect2i(0, 0, 540, 990), "no insets")
