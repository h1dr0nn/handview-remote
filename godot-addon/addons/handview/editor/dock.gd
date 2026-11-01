@tool
extends VBoxContainer

# The Handview dock: device list, Start / Stop Session, status. Built in code.

const Logic := preload("session_logic.gd")
const Controller := preload("session_controller.gd")
const DEFAULT_STATUS: String = "Select a device and start a session."
const NO_DEVICES_STATUS: String = "No devices found. Connect a phone with USB debugging enabled."

var _controller: Controller = null
var _list: ItemList = null
var _hint: Label = null
var _start_button: Button = null
var _stop_button: Button = null
var _status: Label = null
var _encoder: Label = null
var _phone: Label = null
var _dialog: ConfirmationDialog = null
var _list_serials: Array[String] = []
var _updating: bool = false


func setup(controller: Controller) -> void:
	_controller = controller
	name = "Handview"
	_build()
	_controller.devices_changed.connect(_rebuild_list)
	_controller.state_changed.connect(_update)
	_controller.uninstall_requested.connect(_on_uninstall_requested)
	_rebuild_list()
	_update()


func _build() -> void:
	var header: HBoxContainer = HBoxContainer.new()
	var title: Label = Label.new()
	title.text = "Devices"
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	header.add_child(title)
	var refresh: Button = Button.new()
	refresh.text = "Refresh"
	refresh.pressed.connect(_controller.refresh_devices)
	header.add_child(refresh)
	add_child(header)

	_list = ItemList.new()
	_list.custom_minimum_size = Vector2(0, 90)
	_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_list.item_selected.connect(_on_item_selected)
	add_child(_list)

	_hint = _make_label()
	add_child(_hint)

	var buttons: HBoxContainer = HBoxContainer.new()
	_start_button = Button.new()
	_start_button.text = "Start Session"
	_start_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_start_button.pressed.connect(_on_start_pressed)
	buttons.add_child(_start_button)
	_stop_button = Button.new()
	_stop_button.text = "Stop Session"
	_stop_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_stop_button.pressed.connect(_controller.stop_session)
	buttons.add_child(_stop_button)
	add_child(buttons)

	_status = _make_label()
	add_child(_status)
	_encoder = _make_label()
	add_child(_encoder)
	_phone = _make_label()
	add_child(_phone)

	_dialog = ConfirmationDialog.new()
	_dialog.title = "Replace Handview App"
	_dialog.ok_button_text = "Uninstall and Install"
	_dialog.cancel_button_text = "Cancel"
	_dialog.confirmed.connect(_controller.answer_uninstall.bind(true))
	_dialog.canceled.connect(_controller.answer_uninstall.bind(false))
	add_child(_dialog)


func _make_label() -> Label:
	var label: Label = Label.new()
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.custom_minimum_size = Vector2(120, 0)
	return label


func _rebuild_list() -> void:
	_updating = true
	_list.clear()
	_list_serials.clear()
	for device: Dictionary in _controller.devices:
		var serial: String = str(device["serial"])
		_list.add_item(Logic.device_label(device))
		_list_serials.append(serial)
		if serial == _controller.selected_serial:
			_list.select(_list.item_count - 1)
	_updating = false
	_update()


func _on_item_selected(index: int) -> void:
	if _updating or index < 0 or index >= _list_serials.size():
		return
	_controller.select_serial(_list_serials[index])


func _on_start_pressed() -> void:
	_controller.start_session_for(_controller.selected_serial)


func _on_uninstall_requested(_serial: String, output: String) -> void:
	_dialog.dialog_text = (
		"The Handview app on the phone was signed with a different key and cannot be updated in place. "
		+ "Uninstall it and install the bundled version? The app data on the phone will be removed.\n\n"
		+ output.strip_edges()
	)
	_dialog.popup_centered(Vector2i(480, 260))


func _update() -> void:
	if _controller == null:
		return
	var selected_state: String = _controller.selected_state()
	var active: bool = _controller.is_session_active() or _controller.is_busy()
	_hint.text = Logic.device_hint(selected_state, _controller.adb_found) if (not _controller.adb_found or not selected_state.is_empty()) else ""
	_hint.visible = not _hint.text.is_empty()
	_start_button.disabled = (
		not _controller.is_native_available()
		or _controller.is_busy()
		or not Logic.can_start(selected_state)
	)
	_stop_button.disabled = not active
	if not _controller.status.is_empty():
		_status.text = _controller.status
	elif _controller.adb_found and _controller.devices.is_empty():
		_status.text = NO_DEVICES_STATUS
	else:
		_status.text = DEFAULT_STATUS
	_encoder.text = _controller.encoder_text
	_encoder.visible = not _encoder.text.is_empty()
	_phone.text = Logic.phone_line(_controller.phone_info) if _controller.is_phone_connected() else ""
	_phone.visible = not _phone.text.is_empty()
