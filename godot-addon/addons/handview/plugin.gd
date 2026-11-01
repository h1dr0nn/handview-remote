@tool
extends EditorPlugin

# Editor side of Handview: dock, USB launch, phone session, Play / Stop.
# The game side is the "Handview" autoload added while the plugin is enabled.

const AUTOLOAD_NAME: String = "Handview"
const AUTOLOAD_PATH: String = "res://addons/handview/runtime/handview_runtime.gd"

const Controller := preload("editor/session_controller.gd")
const Dock := preload("editor/dock.gd")

var _controller: Controller = null
var _dock: Dock = null
var _started: bool = false
var _idle_frames: int = 0


func _enable_plugin() -> void:
	add_autoload_singleton(AUTOLOAD_NAME, AUTOLOAD_PATH)


func _disable_plugin() -> void:
	remove_autoload_singleton(AUTOLOAD_NAME)


func _enter_tree() -> void:
	_controller = Controller.new()
	_dock = Dock.new()
	_dock.setup(_controller)
	add_control_to_dock(EditorPlugin.DOCK_SLOT_RIGHT_UL, _dock)


func _exit_tree() -> void:
	if _dock != null:
		remove_control_from_docks(_dock)
		_dock.queue_free()
		_dock = null
	if _controller != null:
		_controller.shutdown()
		_controller = null


func _process(delta: float) -> void:
	if _controller == null:
		return
	if not _started:
		_try_start()
		return
	_controller.process(delta)


# Background adb tasks run GDScript on worker threads, which must not overlap
# the editor's first scan and import (scripts reload then, and a thread inside
# a reloading script crashes the editor). Start once the scan has settled.
# Headless editors (imports, CI) never start, unless the loopback test hook asks.
func _try_start() -> void:
	if DisplayServer.get_name() == "headless" and not _controller.loopback_requested():
		return
	if EditorInterface.get_resource_filesystem().is_scanning():
		_idle_frames = 0
		return
	_idle_frames += 1
	if _idle_frames < 30:
		return
	_started = true
	_controller.start()
