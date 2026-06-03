@tool
extends EditorPlugin
## Godot MCP Bridge — editor-side TCP command server.
##
## Opens 127.0.0.1:<PORT> and processes newline-delimited JSON commands from
## the external MCP server (tools/godot-mcp/server.py), dispatching against the
## live editor: SceneTree CRUD, generic reflection (call any method / get-set
## any property + introspection), play mode, GDScript eval, scenes, screenshot,
## logs. This is the Godot counterpart to the IvanMurzak Unity-MCP plugin; the
## reflection + eval paths mirror its "any method becomes a tool" design.
##
## Protocol: one JSON object per line in, one per line out.
##   request : {"id": <any>, "cmd": "<name>", "args": {..}}
##   response: {"id": <any>, "ok": true,  "result": <json>}
##           | {"id": <any>, "ok": false, "error": "<msg>"}

const PORT := 9510
const HOST := "127.0.0.1"
const LOG_RING_MAX := 500

var _server := TCPServer.new()
var _clients: Array = []          # [{ peer: StreamPeerTCP, buf: PackedByteArray }]
var _log_ring: PackedStringArray = PackedStringArray()


func _enter_tree() -> void:
	var err := _server.listen(PORT, HOST)
	if err != OK:
		push_error("[godot_mcp] listen failed on %s:%d (err %d)" % [HOST, PORT, err])
	else:
		print("[godot_mcp] bridge listening on %s:%d" % [HOST, PORT])
	set_process(true)


func _exit_tree() -> void:
	for c in _clients:
		(c.peer as StreamPeerTCP).disconnect_from_host()
	_clients.clear()
	_server.stop()
	print("[godot_mcp] bridge stopped")


func _process(_delta: float) -> void:
	while _server.is_connection_available():
		_clients.append({ "peer": _server.take_connection(), "buf": PackedByteArray() })

	for c in _clients.duplicate():
		var peer: StreamPeerTCP = c.peer
		peer.poll()
		if peer.get_status() != StreamPeerTCP.STATUS_CONNECTED:
			_clients.erase(c)
			continue
		var avail := peer.get_available_bytes()
		if avail > 0:
			var got := peer.get_data(avail)   # [err, PackedByteArray]
			if got[0] == OK:
				c.buf.append_array(got[1])
		var nl := c.buf.find(10)              # '\n'
		while nl >= 0:
			var line: PackedByteArray = c.buf.slice(0, nl)
			c.buf = c.buf.slice(nl + 1)
			_handle_line(peer, line.get_string_from_utf8())
			nl = c.buf.find(10)


func _handle_line(peer: StreamPeerTCP, line: String) -> void:
	line = line.strip_edges()
	if line.is_empty():
		return
	var req = JSON.parse_string(line)
	var rid = req.get("id") if typeof(req) == TYPE_DICTIONARY else null
	var resp := {}
	if typeof(req) != TYPE_DICTIONARY or not req.has("cmd"):
		resp = { "id": rid, "ok": false, "error": "malformed request" }
	else:
		var args: Dictionary = req.get("args", {})
		if typeof(args) != TYPE_DICTIONARY:
			args = {}
		var result = _dispatch(String(req["cmd"]), args)
		if result is Dictionary and result.get("__error__", false):
			resp = { "id": rid, "ok": false, "error": String(result.get("msg", "error")) }
		else:
			resp = { "id": rid, "ok": true, "result": result }
	var out := (JSON.stringify(resp) + "\n").to_utf8_buffer()
	peer.put_data(out)


func _err(msg: String) -> Dictionary:
	return { "__error__": true, "msg": msg }


# --- command dispatch --------------------------------------------------------

func _dispatch(cmd: String, a: Dictionary):
	match cmd:
		"ping":
			return { "pong": true, "engine": Engine.get_version_info() }
		"get_scene_tree":
			return _cmd_get_scene_tree(a)
		"get_node":
			return _cmd_get_node(a)
		"get_property":
			return _cmd_get_property(a)
		"set_property":
			return _cmd_set_property(a)
		"call_method":
			return _cmd_call_method(a)
		"list_methods":
			return _cmd_list_members(a, true)
		"list_properties":
			return _cmd_list_members(a, false)
		"create_node":
			return _cmd_create_node(a)
		"delete_node":
			return _cmd_delete_node(a)
		"reparent_node":
			return _cmd_reparent_node(a)
		"set_script":
			return _cmd_set_script(a)
		"open_scene":
			return _cmd_open_scene(a)
		"save_scene":
			get_editor_interface().save_scene()
			return { "saved": true }
		"get_open_scene":
			var r := get_editor_interface().get_edited_scene_root()
			return { "path": r.scene_file_path if r else "", "root": (_node_brief(r) if r else null) }
		"list_scenes":
			return { "scenes": _scan_ext("res://", ".tscn") }
		"play_scene":
			if a.has("path") and String(a["path"]) != "":
				get_editor_interface().play_custom_scene(String(a["path"]))
			else:
				get_editor_interface().play_current_scene()
			return { "playing": true }
		"play_main":
			get_editor_interface().play_main_scene()
			return { "playing": true }
		"stop":
			get_editor_interface().stop_playing_scene()
			return { "playing": false }
		"is_playing":
			return { "playing": get_editor_interface().is_playing_scene() }
		"run_script":
			return _cmd_run_script(a)
		"read_log":
			return _cmd_read_log(a)
		"screenshot":
			return _cmd_screenshot(a)
		_:
			return _err("unknown cmd: " + cmd)


# --- scene tree / nodes ------------------------------------------------------

func _root() -> Node:
	return get_editor_interface().get_edited_scene_root()

func _resolve(path: String) -> Node:
	var root := _root()
	if root == null:
		return null
	if path == "" or path == "." or path == "/root" or path == root.name:
		return root
	return root.get_node_or_null(NodePath(path))

func _cmd_get_scene_tree(a: Dictionary):
	var root := _root()
	if root == null:
		return _err("no scene is open in the editor")
	var depth := int(a.get("max_depth", 64))
	return _node_tree(root, root, depth)

func _node_tree(n: Node, root: Node, depth: int) -> Dictionary:
	var d := _node_brief(n)
	d["path"] = String(root.get_path_to(n)) if n != root else "."
	var kids := []
	if depth > 0:
		for child in n.get_children():
			kids.append(_node_tree(child, root, depth - 1))
	d["children"] = kids
	return d

func _node_brief(n: Node) -> Dictionary:
	if n == null:
		return {}
	return { "name": n.name, "type": n.get_class(),
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
	var root := _root()
	node.owner = root
	return { "created": String(root.get_path_to(node)) }

func _cmd_delete_node(a: Dictionary):
	var n := _resolve(String(a.get("path", "")))
	if n == null:
		return _err("node not found")
	if n == _root():
		return _err("refusing to delete the scene root")
	n.get_parent().remove_child(n)
	n.queue_free()
	return { "deleted": true }

func _cmd_reparent_node(a: Dictionary):
	var n := _resolve(String(a.get("path", "")))
	var new_parent := _resolve(String(a.get("new_parent", "")))
	if n == null or new_parent == null:
		return _err("node or new_parent not found")
	var keep := bool(a.get("keep_global_transform", true))
	if n is Node3D and keep:
		n.reparent(new_parent, true)
	else:
		n.get_parent().remove_child(n)
		new_parent.add_child(n)
		n.owner = _root()
	return { "reparented": String(_root().get_path_to(n)) }

func _cmd_set_script(a: Dictionary):
	var n := _resolve(String(a.get("path", "")))
	if n == null:
		return _err("node not found")
	var res := load(String(a.get("script", "")))
	if res == null:
		return _err("could not load script: " + String(a.get("script", "")))
	n.set_script(res)
	return { "ok": true }


# --- reflection: properties + methods ---------------------------------------

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
	var coerced = _coerce(a.get("value"), typeof(n.get(prop)))
	n.set(prop, coerced)
	return { "value": _to_json(n.get(prop)) }

func _cmd_call_method(a: Dictionary):
	var n := _resolve(String(a.get("path", "")))
	if n == null:
		return _err("node not found")
	var method := String(a.get("method", ""))
	if not n.has_method(method):
		return _err("no such method: " + method)
	var raw_args: Array = a.get("args", [])
	if typeof(raw_args) != TYPE_ARRAY:
		raw_args = []
	var call_args := []
	for v in raw_args:
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


# --- GDScript eval (parity with Unity execute-C#) ---------------------------

func _cmd_run_script(a: Dictionary):
	var body := String(a.get("source", ""))
	# Wrap the user body as the function `run(editor, root)`; they may `return`.
	var indented := ""
	for ln in body.split("\n"):
		indented += "\t" + ln + "\n"
	var src := "extends RefCounted\nfunc run(editor, root):\n" + indented + "\tpass\n"
	var gd := GDScript.new()
	gd.source_code = src
	var perr := gd.reload()
	if perr != OK:
		return _err("GDScript compile failed (err %d)" % perr)
	var inst = gd.new()
	var res = inst.call("run", get_editor_interface(), _root())
	return { "value": _to_json(res) }


# --- logs / screenshot (best-effort) ----------------------------------------

func _cmd_read_log(a: Dictionary):
	var n := int(a.get("lines", 100))
	# Preferred: read the editor's Output panel directly. The Output dock is an
	# `EditorLog` node backed by a RichTextLabel; an editor addon can walk the
	# editor control tree, find it, and read its parsed text — capturing prints,
	# warnings and errors as shown in the console.
	var rtl := _find_editor_log_richtext(get_editor_interface().get_base_control())
	if rtl != null:
		var text: String = rtl.get_parsed_text() if rtl.has_method("get_parsed_text") else String(rtl.get("text"))
		var lines := text.split("\n")
		var start = max(0, lines.size() - n)
		return { "source": "editor_output", "lines": lines.slice(start, lines.size()) }
	# Fallback: tail the log file if file logging is enabled.
	var path := "user://logs/godot.log"
	if FileAccess.file_exists(path):
		var f := FileAccess.open(path, FileAccess.READ)
		var all := f.get_as_text().split("\n")
		var s2 = max(0, all.size() - n)
		return { "source": "log_file", "lines": all.slice(s2, all.size()) }
	return { "lines": [], "note": "could not locate the EditorLog RichTextLabel and file logging is disabled" }

func _find_editor_log_richtext(node: Node) -> RichTextLabel:
	# The Output dock is an EditorLog control; its RichTextLabel holds the text.
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
	var img: Image = get_editor_interface().get_base_control().get_viewport().get_texture().get_image()
	if img == null:
		return _err("could not capture editor viewport")
	var path := String(a.get("path", "user://godot_mcp_screenshot.png"))
	var werr := img.save_png(path)
	if werr != OK:
		return _err("save_png failed (err %d)" % werr)
	return { "path": ProjectSettings.globalize_path(path), "width": img.get_width(), "height": img.get_height() }


# --- helpers: scan, JSON (de)serialization, coercion ------------------------

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
		TYPE_NIL, TYPE_BOOL, TYPE_INT, TYPE_FLOAT, TYPE_STRING, TYPE_STRING_NAME:
			return v if typeof(v) != TYPE_STRING_NAME else String(v)
		TYPE_VECTOR2: return { "__t__": "Vector2", "x": v.x, "y": v.y }
		TYPE_VECTOR3: return { "__t__": "Vector3", "x": v.x, "y": v.y, "z": v.z }
		TYPE_VECTOR4: return { "__t__": "Vector4", "x": v.x, "y": v.y, "z": v.z, "w": v.w }
		TYPE_COLOR: return { "__t__": "Color", "r": v.r, "g": v.g, "b": v.b, "a": v.a }
		TYPE_QUATERNION: return { "__t__": "Quaternion", "x": v.x, "y": v.y, "z": v.z, "w": v.w }
		TYPE_NODE_PATH: return String(v)
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
				var root := _root()
				return { "__t__": "Node", "path": (String(root.get_path_to(v)) if root else ""), "class": v.get_class() }
			if v is Resource:
				return { "__t__": "Resource", "path": v.resource_path, "class": v.get_class() }
			return { "__t__": "Object", "class": v.get_class() }
		_:
			return str(v)

func _coerce(v, target_type: int):
	# JSON value -> Godot Variant. Tagged dicts ({"__t__":..}) reconstruct math
	# types; otherwise fall back to the JSON-native value.
	if typeof(v) == TYPE_DICTIONARY and v.has("__t__"):
		match String(v["__t__"]):
			"Vector2": return Vector2(v.get("x", 0), v.get("y", 0))
			"Vector3": return Vector3(v.get("x", 0), v.get("y", 0), v.get("z", 0))
			"Vector4": return Vector4(v.get("x", 0), v.get("y", 0), v.get("z", 0), v.get("w", 0))
			"Color": return Color(v.get("r", 0), v.get("g", 0), v.get("b", 0), v.get("a", 1))
			"Quaternion": return Quaternion(v.get("x", 0), v.get("y", 0), v.get("z", 0), v.get("w", 1))
	# Plain array targeting a known math type.
	if typeof(v) == TYPE_ARRAY:
		if target_type == TYPE_VECTOR3 and v.size() == 3: return Vector3(v[0], v[1], v[2])
		if target_type == TYPE_VECTOR2 and v.size() == 2: return Vector2(v[0], v[1])
		if target_type == TYPE_COLOR and v.size() >= 3: return Color(v[0], v[1], v[2], v[3] if v.size() > 3 else 1.0)
	return v
