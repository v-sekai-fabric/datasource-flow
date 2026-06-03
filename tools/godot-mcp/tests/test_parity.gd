extends SceneTree
## Headless tests for the Unity-MCP-parity command additions.
## Run: godot --headless --path tools/godot-mcp --script res://tests/test_parity.gd

const Commands = preload("res://addons/godot_mcp/mcp_commands.gd")

var _fail := 0
var _pass := 0


func _initialize() -> void:
	_run()
	print("\n[godot_mcp parity tests] %d passed, %d failed" % [_pass, _fail])
	quit(1 if _fail > 0 else 0)


func _check(cond: bool, msg: String) -> void:
	if cond:
		_pass += 1
	else:
		_fail += 1
		printerr("  FAIL: ", msg)

func _err(v) -> bool:
	return v is Dictionary and v.get("__error__", false)


func _run() -> void:
	var root := Node3D.new(); root.name = "Root"
	var a := Node3D.new(); a.name = "A"; root.add_child(a)
	var b := Node3D.new(); b.name = "B"; root.add_child(b)
	var cmds = Commands.new()
	cmds.root = root

	# --- files (user:// scratch) ---
	var p := "user://_godot_mcp_test.txt"
	_check(not _err(cmds.dispatch("write_file", { "path": p, "text": "hello" })), "write_file")
	_check(cmds.dispatch("file_exists", { "path": p }).exists == true, "file_exists true")
	_check(cmds.dispatch("read_file", { "path": p }).text == "hello", "read_file round-trip")
	_check(not _err(cmds.dispatch("delete_file", { "path": p })), "delete_file")
	_check(cmds.dispatch("file_exists", { "path": p }).exists == false, "file_exists false after delete")
	_check(_err(cmds.dispatch("read_file", { "path": "res://does_not_exist.xyz" })), "read_file missing errors")

	# --- dir / find ---
	_check(cmds.dispatch("list_dir", { "path": "res://addons/godot_mcp" }).files.has("mcp_commands.gd"), "list_dir finds script")
	_check(cmds.dispatch("find_files", { "root": "res://addons", "ext": ".gd" }).files.size() >= 5, "find_files .gd")

	# --- create script / scene ---
	_check(not _err(cmds.dispatch("create_script", { "path": "user://_t.gd", "source": "extends Node\n" })), "create_script")
	_check(not _err(cmds.dispatch("create_scene", { "path": "user://_t.tscn", "root_type": "Node3D", "root_name": "S" })), "create_scene")
	_check(not _err(cmds.dispatch("instance_scene", { "scene": "user://_t.tscn", "parent": "." })), "instance_scene")
	_check(root.get_node_or_null("S") != null, "instanced scene under root")
	cmds.dispatch("delete_file", { "path": "user://_t.gd" })
	cmds.dispatch("delete_file", { "path": "user://_t.tscn" })

	# --- project settings ---
	_check(cmds.dispatch("get_project_setting", { "setting": "application/config/name" }).value == "godot_mcp_tests", "get_project_setting name")

	# --- hierarchy ---
	var dup_res = cmds.dispatch("duplicate_node", { "path": "A" })
	_check(not _err(dup_res), "duplicate_node")
	_check(root.get_node_or_null(dup_res.duplicated) != null, "duplicate created sibling at returned path")
	cmds.dispatch("move_child", { "path": "B", "to_index": 0 })
	_check(b.get_index() == 0, "move_child reorders")
	cmds.dispatch("add_to_group", { "path": "A", "group": "team" })
	_check(cmds.dispatch("get_nodes_in_group", { "group": "team" }).nodes.has("A"), "add_to_group + get_nodes_in_group")
	cmds.dispatch("remove_from_group", { "path": "A", "group": "team" })
	_check(not cmds.dispatch("get_nodes_in_group", { "group": "team" }).nodes.has("A"), "remove_from_group")
	_check(cmds.dispatch("find_nodes", { "type": "Node3D" }).nodes.size() >= 2, "find_nodes by type")
	_check(cmds.dispatch("get_node_count", {}).count >= 3, "get_node_count")

	# --- ClassDB / signals / singletons ---
	_check(cmds.dispatch("class_exists", { "class": "Node3D" }).exists == true, "class_exists true")
	_check(cmds.dispatch("class_exists", { "class": "Nope" }).exists == false, "class_exists false")
	_check(cmds.dispatch("get_class_methods", { "class": "Node" }).members.has("add_child"), "get_class_methods has add_child")
	_check(cmds.dispatch("get_class_properties", { "class": "Node3D" }).members.size() > 0, "get_class_properties")
	_check(cmds.dispatch("list_signals", { "path": "A" }).signals.has("tree_entered"), "list_signals")
	_check(not _err(cmds.dispatch("emit_signal", { "path": "A", "signal": "renamed" })), "emit_signal")
	_check(cmds.dispatch("call_singleton", { "singleton": "OS", "method": "get_name", "args": [] }).value != "", "call_singleton OS.get_name")
	_check(_err(cmds.dispatch("call_singleton", { "singleton": "Nope", "method": "x" })), "call_singleton bad singleton errors")

	# --- profiling / diagnostics ---
	_check(cmds.dispatch("list_monitors", {}).monitors.size() >= 8, "list_monitors")
	_check(cmds.dispatch("get_memory_info", {}).has("static"), "get_memory_info")
	_check(cmds.dispatch("get_os_info", {}).has("name"), "get_os_info")
	_check(cmds.dispatch("get_video_info", {}).has("adapter"), "get_video_info")

	# --- light parity additions ---
	cmds.dispatch("make_dir", { "path": "user://_mcpd" })
	cmds.dispatch("write_file", { "path": "user://_mcpd/a.txt", "text": "hi" })
	_check(not _err(cmds.dispatch("copy_file", { "from": "user://_mcpd/a.txt", "to": "user://_mcpd/b.txt" })), "copy_file")
	_check(cmds.dispatch("file_exists", { "path": "user://_mcpd/b.txt" }).exists, "copy_file created dest")
	_check(not _err(cmds.dispatch("move_file", { "from": "user://_mcpd/b.txt", "to": "user://_mcpd/c.txt" })), "move_file")
	_check(cmds.dispatch("file_exists", { "path": "user://_mcpd/c.txt" }).exists, "move_file dest exists")
	_check(not _err(cmds.dispatch("create_resource", { "class": "Resource", "path": "user://_r.tres" })), "create_resource")
	_check(_err(cmds.dispatch("create_resource", { "class": "Node3D", "path": "user://_x.tres" })), "create_resource non-Resource -> error")
	_check(cmds.dispatch("get_render_info", {}).has("draw_calls"), "get_render_info")
	cmds.dispatch("delete_file", { "path": "user://_r.tres" })

	root.free()


func _count_named(n: Node, prefix: String) -> int:
	var c := 0
	for ch in n.get_children():
		if String(ch.name).begins_with(prefix):
			c += 1
	return c
