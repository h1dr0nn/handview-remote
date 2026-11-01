extends Node

# Game side of Handview (autoload "Handview"). Inert unless the editor started
# the game with HANDVIEW_LINK_PORT set. Streams the root viewport to the editor
# link and injects the phone's touches as screen touch and drag events.

const Mapper := preload("touch_mapper.gd")

# Emitted when the phone's safe area changes (rotation, Full Screen). See get_safe_area().
signal safe_area_changed(rect: Rect2i)

const HIDDEN_POSITION: Vector2i = Vector2i(-32000, -32000)
const IDLE_FPS: int = 60
const STATUS_INTERVAL_FRAMES: int = 10
# Node::NOTIFICATION_SUSPENDED / UNSUSPENDED, sent when the editor pauses the
# game (DECISIONS D14). Not bound as GDScript constants in Godot 4.4 to 4.7.
const NOTIFICATION_SUSPENDED_VALUE: int = 9003
const NOTIFICATION_UNSUSPENDED_VALUE: int = 9004

# The native HandviewRuntime object. Untyped so the script still parses when
# the native library is missing on this platform.
var _runtime: Object = null
var _last_positions: Dictionary = {}
var _streaming: bool = false
var _frame_counter: int = 0
var _applied_fps: int = -1
# While suspended the renderer keeps drawing the same image, so capture stops.
# A Step unsuspends for one frame; that frame is captured once.
var _suspended: bool = false
var _capture_once: bool = false
# Phone plays (hidden window) follow the phone's video area like the Game
# workspace's "Stretch to Fit": the window takes its aspect ratio.
var _fit_window: bool = false
var _base_size: Vector2i = Vector2i.ZERO
var _applied_view: Vector2i = Vector2i.ZERO
var _applied_insets: Vector4i = Vector4i.ZERO
var _safe_area: Rect2i = Rect2i()


func _ready() -> void:
	if Engine.is_editor_hint() or not OS.has_environment("HANDVIEW_LINK_PORT"):
		set_process(false)
		return
	if not ClassDB.class_exists("HandviewRuntime"):
		push_warning("Handview: the native library is not available on this platform.")
		set_process(false)
		return
	_runtime = ClassDB.instantiate("HandviewRuntime")
	var port: int = OS.get_environment("HANDVIEW_LINK_PORT").to_int()
	var token: String = OS.get_environment("HANDVIEW_LINK_TOKEN")
	if _runtime == null or not _runtime.call("start", port, token):
		push_warning("Handview: could not connect to the editor link.")
		_runtime = null
		set_process(false)
		return
	process_mode = Node.PROCESS_MODE_ALWAYS
	process_priority = -100000
	OS.low_processor_usage_mode = false
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	_hide_window_if_requested()
	RenderingServer.frame_post_draw.connect(_on_frame_post_draw)


func _hide_window_if_requested() -> void:
	if OS.get_environment("HANDVIEW_HIDE_WINDOW") != "1" or Engine.is_embedded_in_editor():
		return
	# Never minimize: a minimized window stops drawing.
	DisplayServer.window_set_flag(DisplayServer.WINDOW_FLAG_NO_FOCUS, true)
	DisplayServer.window_set_position(HIDDEN_POSITION)
	var handle: int = DisplayServer.window_get_native_handle(DisplayServer.WINDOW_HANDLE)
	_runtime.call("set_tool_window", handle)
	_fit_window = true
	_base_size = DisplayServer.window_get_size()


func _process(_delta: float) -> void:
	if _runtime == null:
		return
	_frame_counter += 1
	if _frame_counter % STATUS_INTERVAL_FRAMES == 1:
		var status: Dictionary = _runtime.call("get_status")
		_streaming = bool(status.get("streaming", false))
		_fit_to_view(
			Vector2i(int(status.get("view_width", 0)), int(status.get("view_height", 0))),
			Vector4i(int(status.get("safe_left", 0)), int(status.get("safe_top", 0)), int(status.get("safe_right", 0)), int(status.get("safe_bottom", 0)))
		)
	var fps: int = maxi(1, int(_runtime.call("get_target_fps"))) if _streaming else IDLE_FPS
	if fps != _applied_fps:
		_applied_fps = fps
		Engine.max_fps = fps
	_inject_touches()
	_inject_keys()
	_apply_sensors()


# The usable part of the game window while a phone shows it, in window
# pixels like DisplayServer.get_display_safe_area(). Use this instead of
# DisplayServer in a game that runs under Handview: the editor's desktop has
# no notch, the phone does. Outside a phone play it returns the DisplayServer
# value.
func get_safe_area() -> Rect2i:
	if _safe_area.has_area():
		return _safe_area
	return DisplayServer.get_display_safe_area()


func _fit_to_view(view: Vector2i, insets: Vector4i) -> void:
	if not _fit_window or view.x <= 0 or view.y <= 0 or (view == _applied_view and insets == _applied_insets):
		return
	var size: Vector2i = fit_size(_base_size, view)
	if view != _applied_view:
		DisplayServer.window_set_size(size)
	_applied_view = view
	_applied_insets = insets
	var safe: Rect2i = safe_rect(size, view, insets)
	if safe != _safe_area:
		_safe_area = safe
		safe_area_changed.emit(safe)


# Insets given in the phone's view pixels, scaled to a window of `size`.
static func safe_rect(size: Vector2i, view: Vector2i, insets: Vector4i) -> Rect2i:
	if view.x <= 0 or view.y <= 0:
		return Rect2i(Vector2i.ZERO, size)
	var sx: float = float(size.x) / view.x
	var sy: float = float(size.y) / view.y
	var left: int = roundi(insets.x * sx)
	var top: int = roundi(insets.y * sy)
	var right: int = roundi(insets.z * sx)
	var bottom: int = roundi(insets.w * sy)
	return Rect2i(left, top, maxi(0, size.x - left - right), maxi(0, size.y - top - bottom))


# Window size with the view's aspect ratio: a tall view keeps the base width,
# a wide view keeps the base height. Even numbers for the encoder.
static func fit_size(base: Vector2i, view: Vector2i) -> Vector2i:
	if base.x <= 0 or base.y <= 0 or view.x <= 0 or view.y <= 0:
		return base
	var size: Vector2i = base
	if view.y >= view.x:
		size.y = roundi(float(base.x) * view.y / view.x)
	else:
		size.x = roundi(float(base.y) * view.x / view.y)
	return Vector2i(maxi(2, size.x - size.x % 2), maxi(2, size.y - size.y % 2))


# Keys typed on the phone, as Godot key events.
func _inject_keys() -> void:
	var keys: Array = _runtime.call("take_keys")
	if keys.is_empty():
		return
	for key: Dictionary in keys:
		var event: InputEventKey = InputEventKey.new()
		event.keycode = int(key["keycode"]) as Key
		event.physical_keycode = event.keycode
		event.unicode = int(key["unicode"])
		event.pressed = bool(key["pressed"])
		Input.parse_input_event(event)
	Input.flush_buffered_events()


# The phone's motion sensors replace the PC's (which has none).
func _apply_sensors() -> void:
	var sensors: Dictionary = _runtime.call("get_sensors")
	if sensors.has("accelerometer"):
		Input.set_accelerometer(sensors["accelerometer"])
	if sensors.has("gravity"):
		Input.set_gravity(sensors["gravity"])
	if sensors.has("gyroscope"):
		Input.set_gyroscope(sensors["gyroscope"])
	if sensors.has("magnetometer"):
		Input.set_magnetometer(sensors["magnetometer"])


func _inject_touches() -> void:
	var touches: Array = _runtime.call("take_touches")
	if touches.is_empty():
		return
	var root: Window = get_tree().root
	var tex_size: Vector2 = Vector2(root.get_texture().get_size())
	var window_size: Vector2 = Vector2(root.size)
	var final_xform: Transform2D = root.get_final_transform()
	var viewport_stretch: bool = root.content_scale_mode == Window.CONTENT_SCALE_MODE_VIEWPORT
	for touch: Dictionary in touches:
		var pos: Vector2 = Mapper.map_point(float(touch["x"]), float(touch["y"]), tex_size, window_size, final_xform, viewport_stretch)
		var event: InputEvent = Mapper.make_event(int(touch["id"]), int(touch["phase"]), pos, float(touch["pressure"]), _last_positions)
		if event != null:
			Input.parse_input_event(event)
	# Run input handlers now, before other nodes process this frame.
	Input.flush_buffered_events()


func _on_frame_post_draw() -> void:
	if _runtime == null or (_suspended and not _capture_once) or not _runtime.call("wants_frame"):
		return
	_capture_once = false
	var texture: ViewportTexture = get_viewport().get_texture()
	var size: Vector2i = Vector2i(texture.get_size())
	var data: PackedByteArray = PackedByteArray()
	var device: RenderingDevice = RenderingServer.get_rendering_device()
	if device != null:
		var rd_texture: RID = RenderingServer.texture_get_rd_texture(texture.get_rid())
		if rd_texture.is_valid():
			data = device.texture_get_data(rd_texture, 0)
	if data.size() != size.x * size.y * 4:
		# HDR formats and the Compatibility renderer take the image path.
		var image: Image = texture.get_image()
		if image == null or image.is_empty():
			return
		if image.get_format() != Image.FORMAT_RGBA8:
			image.convert(Image.FORMAT_RGBA8)
		data = image.get_data()
		size = image.get_size()
	_runtime.call("submit_frame", data, size.x, size.y)


func _stop_runtime() -> void:
	if RenderingServer.frame_post_draw.is_connected(_on_frame_post_draw):
		RenderingServer.frame_post_draw.disconnect(_on_frame_post_draw)
	if _runtime != null:
		_runtime.call("stop")
		_runtime = null


func _exit_tree() -> void:
	_stop_runtime()


func _notification(what: int) -> void:
	if what == NOTIFICATION_PREDELETE:
		_stop_runtime()
	elif what == NOTIFICATION_SUSPENDED_VALUE:
		_suspended = true
	elif what == NOTIFICATION_UNSUSPENDED_VALUE:
		_suspended = false
		_capture_once = true
