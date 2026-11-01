@tool
class_name HandviewEmbedToggle
extends RefCounted

# Turns the Game workspace's "Embed Game on Next Play" off for one launch and
# restores it afterwards (D13). The editor reads its embed settings only once,
# so the Game workspace's own menu item is the only switch that works. The item
# is found by its label, not by id. When it cannot be found the game simply
# runs embedded.

const Logic := preload("session_logic.gd")

var _popup: PopupMenu = null
var _item_id: int = -1
var _changed: bool = false


# True while the item is switched off by us and not restored yet.
func is_changed() -> bool:
	return _changed


# The Game workspace sits in the main screen, directly or inside a window wrapper.
static func find_game_view() -> Node:
	var screen: Control = EditorInterface.get_editor_main_screen()
	if screen == null:
		return null
	return _find_game_view_in(screen, 3)


static func _find_game_view_in(node: Node, depth: int) -> Node:
	for child: Node in node.get_children():
		if child.get_class() == "GameView":
			return child
		if depth > 1:
			var found: Node = _find_game_view_in(child, depth - 1)
			if found != null:
				return found
	return null


# Label candidates: English, the main translation domain and the editor domain.
static func _label_candidates(context: Node) -> PackedStringArray:
	var extra: PackedStringArray = PackedStringArray()
	extra.append(TranslationServer.translate(Logic.EMBED_LABEL))
	if context != null:
		extra.append(context.tr(Logic.EMBED_LABEL))
	# The editor domain exists on newer engines; stay duck-typed for 4.4.
	if TranslationServer.has_method("get_or_add_domain"):
		var domain: Object = TranslationServer.call("get_or_add_domain", "godot.editor")
		if domain != null:
			extra.append(str(domain.call("translate", Logic.EMBED_LABEL)))
	return Logic.embed_candidates(extra)


# Returns {popup: PopupMenu, index: int} or an empty dictionary.
static func locate_item() -> Dictionary:
	var game_view: Node = find_game_view()
	if game_view == null:
		return {}
	var candidates: PackedStringArray = _label_candidates(game_view)
	for button: Node in game_view.find_children("*", "MenuButton", true, false):
		var popup: PopupMenu = (button as MenuButton).get_popup()
		var texts: PackedStringArray = PackedStringArray()
		for i: int in popup.item_count:
			texts.append(popup.get_item_text(i))
		var index: int = Logic.find_embed_index(texts, candidates)
		if index >= 0:
			return {"popup": popup, "index": index}
	return {}


# Unchecks the item if it is checked. Returns false when the item was not found
# or could not be used; the game then runs embedded.
func begin_launch() -> bool:
	_changed = false
	var item: Dictionary = locate_item()
	if item.is_empty():
		return false
	var popup: PopupMenu = item["popup"]
	var index: int = item["index"]
	if popup.is_item_disabled(index):
		return false
	if not popup.is_item_checked(index):
		return true
	_popup = popup
	_item_id = popup.get_item_id(index)
	popup.id_pressed.emit(_item_id)
	_changed = true
	return true


# Puts the item back to checked if begin_launch() unchecked it.
func end_launch() -> void:
	if not _changed:
		return
	_changed = false
	if not is_instance_valid(_popup):
		return
	var index: int = _popup.get_item_index(_item_id)
	if index >= 0 and not _popup.is_item_checked(index):
		_popup.id_pressed.emit(_item_id)
	_popup = null
