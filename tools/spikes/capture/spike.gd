extends Node3D

# Measures root viewport readback after RenderingServer.frame_post_draw.
# User args (after --): mode=sync|async|none window=normal|offscreen|minimized|nofocus frames=N
# Prints one RESULT line with timings in microseconds.

var mode := "sync"
var window_mode := "normal"
var frames_target := 600
var warmup := 60

var _rd: RenderingDevice
var _rd_tex: RID
var _frame := 0
var _post_draw_count := 0
var _cpu_us: Array[int] = []
var _latency_us: Array[int] = []
var _latency_frames: Array[int] = []
var _bytes := 0
var _t_begin := 0
var _f_begin := 0


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		var kv := arg.split("=")
		if kv.size() != 2:
			continue
		match kv[0]:
			"mode": mode = kv[1]
			"window": window_mode = kv[1]
			"frames": frames_target = int(kv[1])
	_rd = RenderingServer.get_rendering_device()
	if _rd == null and mode != "image":
		print("RESULT error=no_rendering_device")
		get_tree().quit(1)
		return
	if _rd != null:
		_rd_tex = RenderingServer.texture_get_rd_texture(get_viewport().get_texture().get_rid())
	match window_mode:
		"offscreen":
			DisplayServer.window_set_position(Vector2i(-20000, -20000))
		"nofocus":
			DisplayServer.window_set_position(Vector2i(-20000, -20000))
			DisplayServer.window_set_flag(DisplayServer.WINDOW_FLAG_NO_FOCUS, true)
		"minimized":
			DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_MINIMIZED)
	RenderingServer.frame_post_draw.connect(_on_post_draw)


func _process(delta: float) -> void:
	$Cube.rotate_y(delta * 1.5)
	$Cube.rotate_x(delta * 0.7)
	_frame += 1
	if _frame == warmup:
		_t_begin = Time.get_ticks_usec()
		_f_begin = _post_draw_count
	if _frame >= warmup + frames_target:
		_report()


func _on_post_draw() -> void:
	_post_draw_count += 1
	if _frame < warmup:
		return
	var t0 := Time.get_ticks_usec()
	match mode:
		"image":
			var image := get_viewport().get_texture().get_image()
			_bytes = image.get_data().size()
			_cpu_us.append(Time.get_ticks_usec() - t0)
		"sync":
			var data := _rd.texture_get_data(_rd_tex, 0)
			_bytes = data.size()
			_cpu_us.append(Time.get_ticks_usec() - t0)
		"async":
			var requested_frame := _post_draw_count
			_rd.texture_get_data_async(_rd_tex, 0, func(data: PackedByteArray) -> void:
				_bytes = data.size()
				_latency_us.append(Time.get_ticks_usec() - t0)
				_latency_frames.append(_post_draw_count - requested_frame))
			_cpu_us.append(Time.get_ticks_usec() - t0)


func _pct(values: Array[int], p: float) -> int:
	if values.is_empty():
		return -1
	var sorted := values.duplicate()
	sorted.sort()
	return sorted[mini(sorted.size() - 1, int(sorted.size() * p))]


func _report() -> void:
	var elapsed := Time.get_ticks_usec() - _t_begin
	var drawn := _post_draw_count - _f_begin
	var fps := drawn * 1000000.0 / maxf(1.0, elapsed)
	print("RESULT mode=%s window=%s size=%s fps=%.1f drawn=%d bytes=%d cpu_p50=%d cpu_p95=%d lat_p50=%d lat_p95=%d lat_frames_p50=%d driver=%s" % [
		mode, window_mode, str(get_viewport().get_visible_rect().size), fps, drawn, _bytes,
		_pct(_cpu_us, 0.5), _pct(_cpu_us, 0.95), _pct(_latency_us, 0.5), _pct(_latency_us, 0.95),
		_pct(_latency_frames, 0.5), RenderingServer.get_current_rendering_driver_name()])
	get_tree().quit()
