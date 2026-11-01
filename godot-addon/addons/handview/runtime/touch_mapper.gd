class_name HandviewTouchMapper
extends RefCounted

# Pure helpers that turn native touch records into Godot input events.

const PHASE_BEGAN: int = 0
const PHASE_MOVED: int = 1
const PHASE_STATIONARY: int = 2
const PHASE_ENDED: int = 3
const PHASE_CANCELED: int = 4


# Normalized 0..1 over the captured frame to texture pixels. Values outside 0..1 are clamped.
static func normalized_to_pixels(nx: float, ny: float, tex_size: Vector2) -> Vector2:
	return Vector2(clampf(nx, 0.0, 1.0) * tex_size.x, clampf(ny, 0.0, 1.0) * tex_size.y)


# Viewport texture pixels to window pixels. Identity when both sizes match,
# otherwise the root window's final transform (stretch and canvas transform).
static func texture_to_window(pos: Vector2, tex_size: Vector2, window_size: Vector2, final_transform: Transform2D) -> Vector2:
	if tex_size == window_size:
		return pos
	return final_transform * pos


static func map_point(nx: float, ny: float, tex_size: Vector2, window_size: Vector2, final_transform: Transform2D) -> Vector2:
	return texture_to_window(normalized_to_pixels(nx, ny, tex_size), tex_size, window_size, final_transform)


# Builds the input event for one touch record and keeps the per-index last
# positions in `last_positions`. Returns null for Stationary and unknown phases.
static func make_event(index: int, phase: int, pos: Vector2, pressure: float, last_positions: Dictionary) -> InputEvent:
	match phase:
		PHASE_BEGAN:
			last_positions[index] = pos
			var began: InputEventScreenTouch = InputEventScreenTouch.new()
			began.index = index
			began.position = pos
			began.pressed = true
			return began
		PHASE_MOVED:
			var previous: Vector2 = last_positions.get(index, pos)
			last_positions[index] = pos
			var drag: InputEventScreenDrag = InputEventScreenDrag.new()
			drag.index = index
			drag.position = pos
			drag.relative = pos - previous
			drag.screen_relative = pos - previous
			drag.pressure = pressure
			return drag
		PHASE_ENDED, PHASE_CANCELED:
			last_positions.erase(index)
			var ended: InputEventScreenTouch = InputEventScreenTouch.new()
			ended.index = index
			ended.position = pos
			ended.pressed = false
			ended.canceled = phase == PHASE_CANCELED
			return ended
	return null
