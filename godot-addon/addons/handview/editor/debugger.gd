@tool
extends EditorDebuggerPlugin

# Pauses and steps the played game through the editor debugger with the same
# messages the Game workspace sends (docs/spikes/03-editor-control.md). The Game
# workspace's own pause button is not updated by these messages.


func has_active_session() -> bool:
	for session: EditorDebuggerSession in get_sessions():
		if session.is_active():
			return true
	return false


func set_suspended(suspended: bool) -> bool:
	return _send("scene:suspend_changed", [suspended])


func next_frame() -> bool:
	return _send("scene:next_frame", [])


func _send(message: String, data: Array) -> bool:
	var sent: bool = false
	for session: EditorDebuggerSession in get_sessions():
		if session.is_active():
			session.send_message(message, data)
			sent = true
	return sent
