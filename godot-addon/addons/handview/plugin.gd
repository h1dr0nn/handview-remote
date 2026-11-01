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


func _enable_plugin() -> void:
	add_autoload_singleton(AUTOLOAD_NAME, AUTOLOAD_PATH)


func _disable_plugin() -> void:
	remove_autoload_singleton(AUTOLOAD_NAME)


func _enter_tree() -> void:
	_controller = Controller.new()
	_dock = Dock.new()
	_dock.setup(_controller)
	add_control_to_dock(EditorPlugin.DOCK_SLOT_RIGHT_UL, _dock)
	_controller.start()


func _exit_tree() -> void:
	if _dock != null:
		remove_control_from_docks(_dock)
		_dock.queue_free()
		_dock = null
	if _controller != null:
		_controller.shutdown()
		_controller = null


func _process(delta: float) -> void:
	if _controller != null:
		_controller.process(delta)
