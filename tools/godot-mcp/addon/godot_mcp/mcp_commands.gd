@tool
extends RefCounted
class_name MCPCommands
## Pure command logic for the Godot MCP bridge — transport-free so it can be
## unit-tested headless (see tools/godot-mcp/tests/). The EditorPlugin
## (mcp_bridge.gd) injects `root` (the edited scene root) and `editor` (the
## EditorInterface, or null in tests) per request and calls dispatch().
##
## Returns plain JSON-able values; an error is the dict {"__error__": true,
## "msg": ...} which the transport turns into {"ok": false, "error": ...}.

var root: Node = null            # scene root the commands operate on
var editor = null                # EditorInterface (null in headless tests)


func dispatch(cmd: String, a: Dictionary):
	match cmd:
		"ping":
			return { "pong": true, "engine": Engine.get_version_info() }
		"get_scene_tree": return _cmd_get_scene_tree(a)
		"get_node": return _cmd_get_node(a)
		"get_property": return _cmd_get_property(a)
		"set_property": return _cmd_set_property(a)
		"call_method": return _cmd_call_method(a)
		"list_methods": return _cmd_list_members(a, true)
		"list_properties": return _cmd_list_members(a, false)
		"create_node": return _cmd_create_node(a)
		"delete_node": return _cmd_delete_node(a)
		"reparent_node": return _cmd_reparent_node(a)
		"set_script": return _cmd_set_script(a)
		"run_script": return _cmd_run_script(a)
		"list_scenes": return { "scenes": _scan_ext("res://", ".tscn") }
		# --- editor-only (require EditorInterface) ---
		"open_scene": return _need_editor() if editor == null else _ok(editor.open_scene_from_path(String(a.get("path", ""))))
		"save_scene": return _need_editor() if editor == null else _ok(editor.save_scene())
		"get_open_scene":
			if editor == null: return _need_editor()
			var r = editor.get_edited_scene_root()
			return { "path": (r.scene_file_path if r else ""), "root": (_node_brief(r) if r else null) }
		"play_scene":
			if editor == null: return _need_editor()
			if a.has("path") and String(a["path"]) != "": editor.play_custom_scene(String(a["path"]))
			else: editor.play_current_scene()
			return { "playing": true }
		"play_main":
			if editor == null: return _need_editor()
			editor.play_main_scene(); return { "playing": true }
		"stop":
			if editor == null: return _need_editor()
			editor.stop_playing_scene(); return { "playing": false }
		"is_playing":
			if editor == null: return _need_editor()
			return { "playing": editor.is_playing_scene() }
		"read_log": return _cmd_read_log(a)
		"screenshot": return _cmd_screenshot(a)
		# --- profiling (parity) ---
		"get_performance": return _cmd_get_performance(a)
		_:
			return _err("unknown cmd: " + cmd)


# --- helpers: errors ---------------------------------------------------------

func _err(msg: String) -> Dictionary:
	return { "__error__": true, "msg": msg }

func _need_editor() -> Dictionary:
	return _err("EditorInterface not available in this context")

func _ok(_v = null) -> Dictionary:
	return { "ok": true }


# --- scene tree / nodes ------------------------------------------------------

func _resolve(path: String) -> Node:
	if root == null:
		return null
	if path == "" or path == "." or path == "/root" or path == root.name:
		return root
	return root.get_node_or_null(NodePath(path))

func _cmd_get_scene_tree(a: Dictionary):
	if root == null:
		return _err("no scene root")
	return _node_tree(root, root, int(a.get("max_depth", 64)))

func _node_tree(n: Node, r: Node, depth: int) -> Dictionary:
	var d := _node_brief(n)
	d["path"] = String(r.get_path_to(n)) if n != r else "."
	var kids := []
	if depth > 0:
		for child in n.get_children():
			kids.append(_node_tree(child, r, depth - 1))
	d["children"] = kids
	return d

func _node_brief(n: Node) -> Dictionary:
	if n == null:
		return {}
	return { "name": String(n.name), "type": n.get_class(),
		"script": (n.get_script().resource_path if n.get_script() else "") }

func _cmd_get_node(a: Dictionary):
	var n := _resolve(String(a.get("path", "")))
	if n == null:
		return _err("node not found: " + String(a.get("path", "")))
	var props := {}
	for p in n.get_property_list():
		if int(p.usage) & PROPERTY_USAGE_EDITOR:
			props[p.name] = _to_json(n.get(p.name))
	var brief := _node_brief(n)
	brief["properties"] = props
	return brief

func _cmd_create_node(a: Dictionary):
	var parent := _resolve(String(a.get("parent", "")))
	if parent == null:
		return _err("parent not found: " + String(a.get("parent", "")))
	var type := String(a.get("type", "Node"))
	if not ClassDB.class_exists(type) or not ClassDB.can_instantiate(type):
		return _err("cannot instantiate type: " + type)
	var node: Node = ClassDB.instantiate(type)
	node.name = String(a.get("name", type))
	parent.add_child(node)
	node.owner = root
	return { "created": String(root.get_path_to(node)) }

func _cmd_delete_node(a: Dictionary):
	var n := _resolve(String(a.get("path", "")))
	if n == null:
		return _err("node not found")
	if n == root:
		return _err("refusing to delete the scene root")
	n.get_parent().remove_child(n)
	n.queue_free()
	return { "deleted": true }

func _cmd_reparent_node(a: Dictionary):
	var n := _resolve(String(a.get("path", "")))
	var new_parent := _resolve(String(a.get("new_parent", "")))
	if n == null or new_parent == null:
		return _err("node or new_parent not found")
	if n is Node3D and bool(a.get("keep_global_transform", true)):
		n.reparent(new_parent, true)
	else:
		n.get_parent().remove_child(n)
		new_parent.add_child(n)
		n.owner = root
	return { "reparented": String(root.get_path_to(n)) }

func _cmd_set_script(a: Dictionary):
	var n := _resolve(String(a.get("path", "")))
	if n == null:
		return _err("node not found")
	var res := load(String(a.get("script", "")))
	if res == null:
		return _err("could not load script: " + String(a.get("script", "")))
	n.set_script(res)
	return { "ok": true }


# --- reflection --------------------------------------------------------------

func _cmd_get_property(a: Dictionary):
	var n := _resolve(String(a.get("path", "")))
	if n == null:
		return _err("node not found")
	return { "value": _to_json(n.get(String(a.get("property", "")))) }

func _cmd_set_property(a: Dictionary):
	var n := _resolve(String(a.get("path", "")))
	if n == null:
		return _err("node not found")
	var prop := String(a.get("property", ""))
	n.set(prop, _coerce(a.get("value"), typeof(n.get(prop))))
	return { "value": _to_json(n.get(prop)) }

func _cmd_call_method(a: Dictionary):
	var n := _resolve(String(a.get("path", "")))
	if n == null:
		return _err("node not found")
	var method := String(a.get("method", ""))
	if not n.has_method(method):
		return _err("no such method: " + method)
	var raw: Array = a.get("args", [])
	if typeof(raw) != TYPE_ARRAY:
		raw = []
	var call_args := []
	for v in raw:
		call_args.append(_coerce(v, TYPE_NIL))
	return { "value": _to_json(n.callv(method, call_args)) }

func _cmd_list_members(a: Dictionary, methods: bool):
	var n := _resolve(String(a.get("path", "")))
	if n == null:
		return _err("node not found")
	var out := []
	if methods:
		for m in n.get_method_list():
			out.append({ "name": m.name, "args": m.args.size() })
	else:
		for p in n.get_property_list():
			if int(p.usage) & PROPERTY_USAGE_EDITOR:
				out.append({ "name": p.name, "type": p.type })
	return { "members": out }

func _cmd_run_script(a: Dictionary):
	var indented := ""
	for ln in String(a.get("source", "")).split("\n"):
		indented += "\t" + ln + "\n"
	var src := "extends RefCounted\nfunc run(editor, root):\n" + indented + "\tpass\n"
	var gd := GDScript.new()
	gd.source_code = src
	var perr := gd.reload()
	if perr != OK:
		return _err("GDScript compile failed (err %d)" % perr)
	var inst = gd.new()
	return { "value": _to_json(inst.call("run", editor, root)) }


# --- profiling (parity with Unity-MCP diagnostics) --------------------------

func _cmd_get_performance(a: Dictionary):
	# Expose Godot's Performance monitors (fps, mem, draw calls, object counts…).
	var names := {
		"fps": Performance.TIME_FPS,
		"frame_time": Performance.TIME_PROCESS,
		"physics_frame_time": Performance.TIME_PHYSICS_PROCESS,
		"static_memory": Performance.MEMORY_STATIC,
		"object_count": Performance.OBJECT_COUNT,
		"node_count": Performance.OBJECT_NODE_COUNT,
		"resource_count": Performance.OBJECT_RESOURCE_COUNT,
		"draw_calls": Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME,
		"video_memory": Performance.RENDER_VIDEO_MEM_USED,
	}
	var out := {}
	var want = a.get("monitors", [])
	for key in names:
		if typeof(want) == TYPE_ARRAY and want.size() > 0 and not (key in want):
			continue
		out[key] = Performance.get_monitor(names[key])
	return { "monitors": out }


# --- logs / screenshot (need editor) ----------------------------------------

func _cmd_read_log(a: Dictionary):
	var n := int(a.get("lines", 100))
	if editor != null:
		var rtl := _find_editor_log_richtext(editor.get_base_control())
		if rtl != null:
			var text: String = rtl.get_parsed_text() if rtl.has_method("get_parsed_text") else String(rtl.get("text"))
			var lines := text.split("\n")
			return { "source": "editor_output", "lines": lines.slice(max(0, lines.size() - n), lines.size()) }
	var path := "user://logs/godot.log"
	if FileAccess.file_exists(path):
		var all := FileAccess.open(path, FileAccess.READ).get_as_text().split("\n")
		return { "source": "log_file", "lines": all.slice(max(0, all.size() - n), all.size()) }
	return { "lines": [], "note": "no EditorLog and file logging disabled" }

func _find_editor_log_richtext(node: Node) -> RichTextLabel:
	if node.get_class() == "EditorLog":
		var r := _first_richtext(node)
		if r != null:
			return r
	for child in node.get_children():
		var found := _find_editor_log_richtext(child)
		if found != null:
			return found
	return null

func _first_richtext(node: Node) -> RichTextLabel:
	if node is RichTextLabel:
		return node
	for child in node.get_children():
		var r := _first_richtext(child)
		if r != null:
			return r
	return null

func _cmd_screenshot(a: Dictionary):
	if editor == null:
		return _need_editor()
	var img: Image = editor.get_base_control().get_viewport().get_texture().get_image()
	if img == null:
		return _err("could not capture editor viewport")
	var path := String(a.get("path", "user://godot_mcp_screenshot.png"))
	if img.save_png(path) != OK:
		return _err("save_png failed")
	return { "path": ProjectSettings.globalize_path(path), "width": img.get_width(), "height": img.get_height() }


# --- scan + JSON (de)serialization + coercion -------------------------------

func _scan_ext(dir_path: String, ext: String, out: PackedStringArray = PackedStringArray()) -> PackedStringArray:
	var d := DirAccess.open(dir_path)
	if d == null:
		return out
	d.list_dir_begin()
	var name := d.get_next()
	while name != "":
		if name != "." and name != "..":
			var full := dir_path.path_join(name)
			if d.current_is_dir():
				_scan_ext(full, ext, out)
			elif name.ends_with(ext):
				out.append(full)
		name = d.get_next()
	d.list_dir_end()
	return out

func _to_json(v):
	match typeof(v):
		TYPE_NIL, TYPE_BOOL, TYPE_INT, TYPE_FLOAT, TYPE_STRING:
			return v
		TYPE_STRING_NAME, TYPE_NODE_PATH:
			return String(v)
		TYPE_VECTOR2: return { "__t__": "Vector2", "x": v.x, "y": v.y }
		TYPE_VECTOR3: return { "__t__": "Vector3", "x": v.x, "y": v.y, "z": v.z }
		TYPE_VECTOR4: return { "__t__": "Vector4", "x": v.x, "y": v.y, "z": v.z, "w": v.w }
		TYPE_COLOR: return { "__t__": "Color", "r": v.r, "g": v.g, "b": v.b, "a": v.a }
		TYPE_QUATERNION: return { "__t__": "Quaternion", "x": v.x, "y": v.y, "z": v.z, "w": v.w }
		TYPE_ARRAY, TYPE_PACKED_INT32_ARRAY, TYPE_PACKED_INT64_ARRAY, \
		TYPE_PACKED_FLOAT32_ARRAY, TYPE_PACKED_FLOAT64_ARRAY, TYPE_PACKED_STRING_ARRAY, \
		TYPE_PACKED_VECTOR2_ARRAY, TYPE_PACKED_VECTOR3_ARRAY, TYPE_PACKED_COLOR_ARRAY:
			var arr := []
			for e in v:
				arr.append(_to_json(e))
			return arr
		TYPE_DICTIONARY:
			var dd := {}
			for k in v:
				dd[String(k)] = _to_json(v[k])
			return dd
		TYPE_OBJECT:
			if v == null:
				return null
			if v is Node:
				return { "__t__": "Node", "path": (String(root.get_path_to(v)) if root else ""), "class": v.get_class() }
			if v is Resource:
				return { "__t__": "Resource", "path": v.resource_path, "class": v.get_class() }
			return { "__t__": "Object", "class": v.get_class() }
		_:
			return str(v)

func _coerce(v, target_type: int):
	if typeof(v) == TYPE_DICTIONARY and v.has("__t__"):
		match String(v["__t__"]):
			"Vector2": return Vector2(v.get("x", 0), v.get("y", 0))
			"Vector3": return Vector3(v.get("x", 0), v.get("y", 0), v.get("z", 0))
			"Vector4": return Vector4(v.get("x", 0), v.get("y", 0), v.get("z", 0), v.get("w", 0))
			"Color": return Color(v.get("r", 0), v.get("g", 0), v.get("b", 0), v.get("a", 1))
			"Quaternion": return Quaternion(v.get("x", 0), v.get("y", 0), v.get("z", 0), v.get("w", 1))
	if typeof(v) == TYPE_ARRAY:
		if target_type == TYPE_VECTOR3 and v.size() == 3: return Vector3(v[0], v[1], v[2])
		if target_type == TYPE_VECTOR2 and v.size() == 2: return Vector2(v[0], v[1])
		if target_type == TYPE_COLOR and v.size() >= 3: return Color(v[0], v[1], v[2], v[3] if v.size() > 3 else 1.0)
	return v
