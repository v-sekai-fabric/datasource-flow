@tool
extends EditorPlugin

# IDTXFlow editor plugin — registers the "V-Sekai" submenu under
# Project > Tools so authors can promote the current edited scene to
# USD (or run other one-shot conversions) without dropping to a CLI.
#
# Underlying work is delegated to the GDExtension's IDTXFlowExporter
# class. This file only owns the editor UX wiring.

const _MENU_LABEL: String = "V-Sekai"

var _popup: PopupMenu

func _enter_tree() -> void:
	_popup = PopupMenu.new()
	_popup.add_item("Export Scene to USD…", 0)
	_popup.add_item("Export Scene to VRM…", 1)
	_popup.id_pressed.connect(_on_menu_id_pressed)
	add_tool_submenu_item(_MENU_LABEL, _popup)

func _exit_tree() -> void:
	if _popup != null:
		remove_tool_menu_item(_MENU_LABEL)
		_popup.queue_free()
		_popup = null

func _get_plugin_name() -> String:
	return "IDTXFlow"

func _has_main_screen() -> bool:
	return false

# ---------------------------------------------------------------------
# Menu actions
# ---------------------------------------------------------------------

func _on_menu_id_pressed(id: int) -> void:
	var root: Node = EditorInterface.get_edited_scene_root()
	if root == null:
		push_warning("[IDTXFlow] No edited scene to export.")
		return
	if not (root is Node3D):
		push_warning("[IDTXFlow] Edited scene root is not a Node3D (%s)." % root.get_class())
		return

	var ext: String = (".usda" if id == 0 else ".vrm")
	var label: String = ("USD" if id == 0 else "VRM")

	var dialog := EditorFileDialog.new()
	dialog.access = EditorFileDialog.ACCESS_FILESYSTEM
	dialog.file_mode = EditorFileDialog.FILE_MODE_SAVE_FILE
	dialog.add_filter("*" + ext, "%s file (*%s)" % [label, ext])
	dialog.current_file = root.name + ext
	dialog.file_selected.connect(func(path: String) -> void:
		_do_export(root, path, id)
		dialog.queue_free()
	)
	EditorInterface.get_base_control().add_child(dialog)
	dialog.popup_centered_ratio(0.6)

func _do_export(root: Node3D, path: String, id: int) -> void:
	var exporter := IDTXFlowExporter.new()
	var ok: bool = false
	# The GDExtension currently exposes USD export via export_scene.
	# VRM export goes through the core C ABI; until export_to_vrm is
	# bound, the menu falls back to USD and surfaces a warning.
	if id == 0:
		ok = exporter.export_scene(root, path)
	else:
		# TODO(Phase 8.5): bind IDTXFlowExporter.export_scene_to_vrm.
		# For now write a paired .usda alongside the requested .vrm so
		# the menu still produces something usable.
		var usd_path := path.get_basename() + ".usda"
		ok = exporter.export_scene(root, usd_path)
		push_warning("[IDTXFlow] VRM export pending engine-side binding (Phase 8.5); wrote USD at %s instead." % usd_path)
		path = usd_path

	if ok:
		print("[IDTXFlow] wrote %s" % path)
	else:
		push_error("[IDTXFlow] export failed for %s" % path)
