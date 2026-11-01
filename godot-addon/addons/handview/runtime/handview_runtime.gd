extends Node

# Game side of Handview (autoload "Handview"). Inert unless the editor started
# the game with HANDVIEW_LINK_PORT set. Streams the root viewport to the editor
# link and injects the phone's touches as screen touch and drag events.

const Mapper := preload("touch_mapper.gd")

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


func _process(_delta: float) -> void:
	if _runtime == null:
		return
	_frame_counter += 1
	if _frame_counter % STATUS_INTERVAL_FRAMES == 1:
		_streaming = bool(_runtime.call("get_status").get("streaming", false))
	var fps: int = maxi(1, int(_runtime.call("get_target_fps"))) if _streaming else IDLE_FPS
	if fps != _applied_fps:
		_applied_fps = fps
		Engine.max_fps = fps
	_inject_touches()


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
