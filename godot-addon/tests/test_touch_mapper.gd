extends RefCounted

const Mapper := preload("res://addons/handview/runtime/touch_mapper.gd")

var failures: Array = []


func _eq(actual: Variant, expected: Variant, label: String) -> void:
	if typeof(actual) != typeof(expected) or actual != expected:
		failures.append("%s: expected %s, got %s" % [label, str(expected), str(actual)])


func _near(actual: Vector2, expected: Vector2, label: String) -> void:
	if not actual.is_equal_approx(expected):
		failures.append("%s: expected %s, got %s" % [label, str(expected), str(actual)])


func test_identity_mapping() -> void:
	var size: Vector2 = Vector2(1280, 720)
	_near(Mapper.map_point(0.5, 0.25, size, size, Transform2D.IDENTITY), Vector2(640, 180), "center")
	_near(Mapper.map_point(0.0, 1.0, size, size, Transform2D.IDENTITY), Vector2(0, 720), "corner")
	# The transform is ignored when sizes match.
	var scaled: Transform2D = Transform2D(0.0, Vector2(2, 2), 0.0, Vector2(10, 10))
	_near(Mapper.map_point(0.5, 0.5, size, size, scaled), Vector2(640, 360), "identity wins")


func test_scale_and_offset_mapping() -> void:
	var tex: Vector2 = Vector2(1000, 500)
	var window: Vector2 = Vector2(2000, 1100)
	var xform: Transform2D = Transform2D(0.0, Vector2(2, 2), 0.0, Vector2(0, 50))
	_near(Mapper.map_point(0.5, 0.5, tex, window, xform), Vector2(1000, 550), "scale+offset center")
	_near(Mapper.map_point(1.0, 1.0, tex, window, xform), Vector2(2000, 1050), "scale+offset corner")


func test_out_of_range_is_clamped() -> void:
	var size: Vector2 = Vector2(800, 600)
	_near(Mapper.normalized_to_pixels(-0.5, 2.0, size), Vector2(0, 600), "clamped")


func test_began_event() -> void:
	var last: Dictionary = {}
	var event: InputEvent = Mapper.make_event(2, Mapper.PHASE_BEGAN, Vector2(10, 20), 0.5, last)
	_eq(event is InputEventScreenTouch, true, "type")
	var touch: InputEventScreenTouch = event as InputEventScreenTouch
	_eq(touch.pressed, true, "pressed")
	_eq(touch.canceled, false, "not canceled")
	_eq(touch.index, 2, "index")
	_eq(touch.position, Vector2(10, 20), "position")
	_eq(last.get(2), Vector2(10, 20), "last stored")


func test_moved_event_relative() -> void:
	var last: Dictionary = {}
	Mapper.make_event(0, Mapper.PHASE_BEGAN, Vector2(100, 100), 1.0, last)
	var first: InputEventScreenDrag = Mapper.make_event(0, Mapper.PHASE_MOVED, Vector2(110, 95), 0.7, last) as InputEventScreenDrag
	_eq(first != null, true, "drag type")
	_eq(first.relative, Vector2(10, -5), "relative")
	_eq(first.screen_relative, Vector2(10, -5), "screen relative")
	_eq(is_equal_approx(first.pressure, 0.7), true, "pressure")
	var second: InputEventScreenDrag = Mapper.make_event(0, Mapper.PHASE_MOVED, Vector2(112, 95), 0.7, last) as InputEventScreenDrag
	_eq(second.relative, Vector2(2, 0), "relative from last")


func test_moved_tracks_indices_separately() -> void:
	var last: Dictionary = {}
	Mapper.make_event(0, Mapper.PHASE_BEGAN, Vector2(0, 0), 1.0, last)
	Mapper.make_event(1, Mapper.PHASE_BEGAN, Vector2(50, 50), 1.0, last)
	var drag: InputEventScreenDrag = Mapper.make_event(1, Mapper.PHASE_MOVED, Vector2(60, 50), 1.0, last) as InputEventScreenDrag
	_eq(drag.relative, Vector2(10, 0), "index 1 relative")
	_eq(drag.index, 1, "index 1")


func test_stationary_is_skipped() -> void:
	var last: Dictionary = {}
	_eq(Mapper.make_event(0, Mapper.PHASE_STATIONARY, Vector2.ZERO, 1.0, last), null, "stationary")
	_eq(Mapper.make_event(0, 9, Vector2.ZERO, 1.0, last), null, "unknown phase")


func test_ended_and_canceled_release() -> void:
	var last: Dictionary = {}
	Mapper.make_event(3, Mapper.PHASE_BEGAN, Vector2(5, 5), 1.0, last)
	var ended: InputEventScreenTouch = Mapper.make_event(3, Mapper.PHASE_ENDED, Vector2(6, 6), 0.0, last) as InputEventScreenTouch
	_eq(ended.pressed, false, "ended pressed")
	_eq(ended.canceled, false, "ended canceled")
	_eq(last.has(3), false, "ended clears last")
	Mapper.make_event(3, Mapper.PHASE_BEGAN, Vector2(5, 5), 1.0, last)
	var canceled: InputEventScreenTouch = Mapper.make_event(3, Mapper.PHASE_CANCELED, Vector2(5, 5), 0.0, last) as InputEventScreenTouch
	_eq(canceled.pressed, false, "canceled pressed")
	_eq(canceled.canceled, true, "canceled flag")
	_eq(last.has(3), false, "canceled clears last")
