extends SceneTree
## Headless unit tests for MCPCommands (the transport-free Godot MCP logic).
## Run: godot --headless --path . --script res://tests/test_commands.gd
## Exit code 0 = all pass, 1 = failure(s).

const Commands = preload("res://addons/vsekai_godot_mcp/mcp_commands.gd")

var _fail := 0
var _pass := 0


func _initialize() -> void:
	_run()
	print("\n[godot_mcp tests] %d passed, %d failed" % [_pass, _fail])
	quit(1 if _fail > 0 else 0)


func _check(cond: bool, msg: String) -> void:
	if cond:
		_pass += 1
	else:
		_fail += 1
		printerr("  FAIL: ", msg)


func _err_dict(v) -> bool:
	return v is Dictionary and v.get("__error__", false)


func _run() -> void:
	# Synthetic scene: Root(Node3D) -> Child(Node3D)
	var root := Node3D.new(); root.name = "Root"
	var child := Node3D.new(); child.name = "Child"
	root.add_child(child)

	var cmds = Commands.new()
	cmds.root = root
	cmds.editor = null   # headless: editor-only commands must error cleanly

	# ping
	_check(cmds.dispatch("ping", {}).get("pong", false), "ping")

	# get_scene_tree
	var tree = cmds.dispatch("get_scene_tree", {})
	_check(tree.get("name") == "Root", "scene_tree root name")
	_check(tree.children.size() == 1 and tree.children[0].name == "Child", "scene_tree child")
	_check(tree.children[0].path == "Child", "scene_tree child path")

	# create_node
	var cr = cmds.dispatch("create_node", { "parent": ".", "type": "Node3D", "name": "Made" })
	_check(not _err_dict(cr) and cr.get("created") == "Made", "create_node")
	_check(root.get_node_or_null("Made") != null, "created node in tree")

	# create_node bad type errors
	_check(_err_dict(cmds.dispatch("create_node", { "parent": ".", "type": "NotARealClass" })), "create_node bad type errors")

	# set_property (Vector3 via tagged dict) + get_property round-trip
	cmds.dispatch("set_property", { "path": "Child", "property": "position",
		"value": { "__t__": "Vector3", "x": 1, "y": 2, "z": 3 } })
	var gp = cmds.dispatch("get_property", { "path": "Child", "property": "position" })
	_check(gp.value.get("__t__") == "Vector3" and gp.value.x == 1 and gp.value.z == 3, "set/get Vector3 (tagged)")

	# set_property via plain [x,y,z] array
	cmds.dispatch("set_property", { "path": "Child", "property": "position", "value": [4, 5, 6] })
	_check(cmds.dispatch("get_property", { "path": "Child", "property": "position" }).value.z == 6, "set Vector3 (array)")

	# call_method (reflection by name)
	var cm = cmds.dispatch("call_method", { "path": ".", "method": "get_class", "args": [] })
	_check(cm.get("value") == "Node3D", "call_method get_class")
	_check(_err_dict(cmds.dispatch("call_method", { "path": ".", "method": "no_such_method" })), "call_method missing errors")

	# list_methods / list_properties
	_check(cmds.dispatch("list_methods", { "path": "." }).members.size() > 0, "list_methods")
	_check(cmds.dispatch("list_properties", { "path": "." }).members.size() > 0, "list_properties")

	# run_script (eval) — value + access to root
	_check(cmds.dispatch("run_script", { "source": "return 2 + 3" }).value == 5, "run_script value")
	_check(str(cmds.dispatch("run_script", { "source": "return root.name" }).value) == "Root", "run_script sees root")
	_check(_err_dict(cmds.dispatch("run_script", { "source": "this is not gdscript !!!" })), "run_script compile error")

	# get_performance (profiling parity)
	var perf = cmds.dispatch("get_performance", {})
	_check(perf.monitors.has("object_count") and perf.monitors.has("fps"), "get_performance monitors")
	_check(cmds.dispatch("get_performance", { "monitors": ["fps"] }).monitors.size() == 1, "get_performance filtered")

	# reparent_node
	var rp = cmds.dispatch("reparent_node", { "path": "Made", "new_parent": "Child" })
	_check(not _err_dict(rp) and root.get_node_or_null("Child/Made") != null, "reparent_node")

	# delete_node + refuse-root
	_check(not _err_dict(cmds.dispatch("delete_node", { "path": "Child/Made" })), "delete_node")
	_check(_err_dict(cmds.dispatch("delete_node", { "path": "." })), "delete_node refuses root")

	# unknown command + editor-only without editor
	_check(_err_dict(cmds.dispatch("bogus_cmd", {})), "unknown cmd errors")
	_check(_err_dict(cmds.dispatch("save_scene", {})), "editor-only errors when editor==null")
	_check(_err_dict(cmds.dispatch("screenshot", {})), "screenshot errors when editor==null")

	root.free()
