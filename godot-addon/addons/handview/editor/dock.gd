@tool
extends VBoxContainer

# The Handview dock: devices and their state, status, encoder and phone. There
# is no session button: every plugged in phone is prepared on its own and joins
# from the Handview app (DECISIONS D18). Built in code.

const Logic := preload("session_logic.gd")
const Controller := preload("session_controller.gd")
const NO_DEVICES_STATUS: String = "No devices found. Connect a phone with USB debugging enabled."

var _controller: Controller = null
var _list: VBoxContainer = null
var _hint: Label = null
var _status: Label = null
var _encoder: Label = null
var _phone: Label = null
var _dialog: ConfirmationDialog = null
var _wifi_toggle: CheckButton = null
var _wifi_line: Label = null
var _wifi_qr: TextureRect = null


func setup(controller: Controller) -> void:
	_controller = controller
	name = "Handview"
	_build()
	_controller.devices_changed.connect(_update)
	_controller.state_changed.connect(_update)
	_controller.uninstall_requested.connect(_on_uninstall_requested)
	_controller.uninstall_cancelled.connect(_on_uninstall_cancelled)
	_update()


func _build() -> void:
	var header: HBoxContainer = HBoxContainer.new()
	var title: Label = Label.new()
	title.text = "Devices"
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	header.add_child(title)
	var refresh: Button = Button.new()
	refresh.text = "Refresh"
	refresh.pressed.connect(_controller.retry_failed)
	header.add_child(refresh)
	add_child(header)

	# One entry per device: the name, and its state on the line below.
	_list = VBoxContainer.new()
	_list.add_theme_constant_override("separation", 8)
	add_child(_list)

	_hint = _make_label()
	add_child(_hint)
	_status = _make_label()
	add_child(_status)
	_encoder = _make_label()
	add_child(_encoder)
	_phone = _make_label()
	add_child(_phone)

	add_child(HSeparator.new())
	_wifi_toggle = CheckButton.new()
	_wifi_toggle.text = "Allow Wi-Fi"
	_wifi_toggle.tooltip_text = "Phones on the same network join with the code or the PIN."
	_wifi_toggle.toggled.connect(_controller.set_wifi_enabled)
	add_child(_wifi_toggle)
	_wifi_line = _make_label()
	add_child(_wifi_line)
	_wifi_qr = TextureRect.new()
	_wifi_qr.custom_minimum_size = Vector2(180, 180)
	_wifi_qr.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_wifi_qr.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	_wifi_qr.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
	add_child(_wifi_qr)

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


func _on_uninstall_requested(_serial: String, output: String) -> void:
	_dialog.dialog_text = (
		"The Handview app on the phone was signed with a different key and cannot be updated in place. "
		+ "Uninstall it and install the bundled version? The app data on the phone will be removed.\n\n"
		+ output.strip_edges()
	)
	_dialog.popup_centered(Vector2i(480, 260))


func _on_uninstall_cancelled() -> void:
	_dialog.hide()


func _update() -> void:
	# A dock built by an older script keeps its old controls until the editor restarts.
	if _controller == null or not (_list is VBoxContainer):
		return
	for child: Node in _list.get_children():
		_list.remove_child(child)
		child.queue_free()
	for device: Dictionary in _controller.devices:
		var entry: VBoxContainer = VBoxContainer.new()
		entry.add_theme_constant_override("separation", 0)
		var title: Label = _make_label()
		title.text = Logic.device_label(device)
		entry.add_child(title)
		var state: String = _controller.device_status(str(device["serial"]))
		if not state.is_empty():
			var detail: Label = _make_label()
			detail.text = state
			detail.modulate = Color(1, 1, 1, 0.65)
			entry.add_child(detail)
		_list.add_child(entry)
	_hint.text = "" if _controller.adb_found else Logic.SDK_HINT
	_hint.visible = not _hint.text.is_empty()
	if _controller.adb_found and _controller.devices.is_empty():
		_status.text = NO_DEVICES_STATUS
	else:
		_status.text = _controller.status
	_status.visible = not _status.text.is_empty()
	_encoder.text = _controller.encoder_text
	_encoder.visible = not _encoder.text.is_empty()
	_phone.text = Logic.phone_line(_controller.phone_info) if _controller.is_phone_connected() else ""
	_phone.visible = not _phone.text.is_empty()
	# A dock built by an older script (the scripts reloaded after an update) has no Wi-Fi controls.
	if _wifi_toggle == null:
		return
	_wifi_toggle.set_pressed_no_signal(_controller.wifi_enabled)
	_wifi_toggle.disabled = not _controller.is_wifi_supported()
	if not _controller.is_wifi_supported():
		_wifi_line.text = "Restart the editor to load the updated Handview library."
	elif _controller.wifi_enabled and _controller.wifi_addresses.is_empty():
		_wifi_line.text = "No network found. Connect this PC to Wi-Fi or Ethernet."
	elif _controller.wifi_enabled:
		_wifi_line.text = "Scan with the Handview app, or enter %s:%d and PIN %s. Windows may ask to allow Godot through the firewall." % [_controller.wifi_addresses[0], _controller.get_port(), _controller.wifi_pin]
	else:
		_wifi_line.text = ""
	_wifi_line.visible = not _wifi_line.text.is_empty()
	_wifi_qr.texture = ImageTexture.create_from_image(_controller.wifi_qr) if _controller.wifi_qr != null else null
	_wifi_qr.visible = _wifi_qr.texture != null
