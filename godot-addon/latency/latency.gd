extends Control

# Latency test scene: the screen flips color on every touch, so touch-to-display
# can be checked on the phone overlay and with a 240 fps camera. Also shows a
# frame counter and the last touch position.

const COLORS: Array[Color] = [Color(0.1, 0.1, 0.12), Color(0.95, 0.95, 0.95)]

var _color_index: int = 0
var _frames: int = 0
var _last_touch: Vector2 = Vector2(-1, -1)

@onready var _background: ColorRect = $Background
@onready var _marker: ColorRect = $Marker
@onready var _label: Label = $Label


func _ready() -> void:
	_background.color = COLORS[_color_index]
	_marker.visible = false
	# Test hook for crash handling runs: the game kills its own process.
	if OS.has_environment("HANDVIEW_TEST_KILL_AFTER"):
		var seconds: float = OS.get_environment("HANDVIEW_TEST_KILL_AFTER").to_float()
		get_tree().create_timer(seconds).timeout.connect(func() -> void: OS.kill(OS.get_process_id()))


func _input(event: InputEvent) -> void:
	if event is InputEventScreenTouch and event.pressed:
		_color_index = 1 - _color_index
		_background.color = COLORS[_color_index]
		_last_touch = event.position
	elif event is InputEventScreenDrag:
		_last_touch = event.position


func _process(_delta: float) -> void:
	_frames += 1
	_marker.visible = _last_touch.x >= 0.0
	_marker.position = _last_touch - _marker.size * 0.5
	_label.add_theme_color_override("font_color", COLORS[1 - _color_index])
	_label.text = "Frame %d\nTouch %s" % [_frames, str(_last_touch.round()) if _last_touch.x >= 0.0 else "-"]
