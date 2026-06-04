extends SceneTree
## Adversarial / fuzz tests — hostile inputs must not crash the editor and must
## fail safe. Run: godot --headless --path addons/godot-mcp --script res://tests/test_adversarial.gd

const Commands = preload("res://addons/godot_mcp/mcp_commands.gd")
const Protocol = preload("res://addons/godot_mcp/mcp_protocol.gd")
const Http = preload("res://addons/godot_mcp/mcp_http_server.gd")

var _fail := 0
var _pass := 0


func _initialize() -> void:
	_run()
	print("\n[godot_mcp adversarial] %d passed, %d failed" % [_pass, _fail])
	quit(1 if _fail > 0 else 0)


func _check(cond: bool, msg: String) -> void:
	if cond:
		_pass += 1
	else:
		_fail += 1
		printerr("  FAIL: ", msg)

func _is_err(v) -> bool:
	return v is Dictionary and v.get("__error__", false)


func _run() -> void:
	# 1) Every command with NULL root + empty args must return (no crash).
	var c = Commands.new()
	for cmd in ["get_scene_tree", "get_node", "get_property", "set_property", "call_method",
			"list_methods", "list_properties", "create_node", "delete_node", "reparent_node",
			"duplicate_node", "move_child", "find_nodes", "get_node_count",
			"get_nodes_in_group", "add_to_group", "list_signals", "emit_signal",
			"instance_scene", "save_branch_as_scene", "get_class_methods"]:
		_check(c.dispatch(cmd, {}) != null, "null-root '%s' returns (no crash)" % cmd)

	# 2) Editor-only commands without an editor must error, not crash.
	for cmd in ["save_scene", "open_scene", "play_main", "stop", "is_playing", "screenshot", "get_editor_setting"]:
		_check(_is_err(c.dispatch(cmd, {})), "editor-only '%s' errors w/o editor" % cmd)

	# 3) Protocol: garbage messages.
	var p = Protocol.new()
	_check(p.handle_rpc(null).has("error"), "handle_rpc(null) -> error")
	_check(p.handle_rpc([]).has("error"), "handle_rpc(array/batch) -> error")
	_check(p.handle_rpc(42).has("error"), "handle_rpc(int) -> error")
	_check(p.handle_rpc({}) == null, "handle_rpc({}) (no method) -> null")
	_check(p.handle_rpc({ "method": "tools/call", "id": 1, "params": "nope" }).has("error"), "tools/call non-dict params -> error")
	var bad_args = p.handle_rpc({ "method": "tools/call", "id": 1, "params": { "name": "get_node", "arguments": "nope" } })
	_check(bad_args.has("result") and bad_args.result.get("isError"), "tools/call non-dict arguments -> tool error")

	# 4) HTTP route: hostile bodies never crash, always a valid status >= 200.
	var bodies := ["", "[]", "12345", "true", "null", '{bad json', '{"jsonrpc":"2.0"}', " "]
	for body in bodies:
		var rr = h_route(body)
		_check(rr.has("code") and int(rr.code) >= 200, "hostile body ok: '%s'" % body.substr(0, 12))

	# 5) Dangerous reflection: free()/queue_free() must be blocked.
	var root := Node3D.new(); root.name = "Root"
	var kid := Node3D.new(); kid.name = "Kid"; root.add_child(kid)
	c.root = root
	_check(_is_err(c.dispatch("call_method", { "path": ".", "method": "free" })), "call_method free -> blocked")
	_check(_is_err(c.dispatch("call_method", { "path": "Kid", "method": "queue_free" })), "call_method queue_free -> blocked")
	_check(is_instance_valid(root), "root still valid after blocked free")
	_check(is_instance_valid(kid), "kid still valid after blocked free")

	# 6) Out-of-range move_child index must clamp, not crash.
	c.dispatch("create_node", { "parent": ".", "type": "Node3D", "name": "A" })
	_check(not _is_err(c.dispatch("move_child", { "path": "A", "to_index": 99999 })), "move_child huge index clamps")
	_check(not _is_err(c.dispatch("move_child", { "path": "A", "to_index": -5 })), "move_child negative index clamps")

	# 7) Cyclic structure must not blow the stack in _to_json (depth guard).
	var cyclic := {}
	cyclic["self"] = cyclic
	_check(c.call("_to_json", cyclic) != null, "_to_json(cyclic) returns (depth-guarded)")

	# 8) get_scene_tree with an absurd max_depth must be bounded.
	_check(not _is_err(c.dispatch("get_scene_tree", { "max_depth": 1000000000 })), "get_scene_tree huge max_depth ok")

	# 9) call_method with non-array args coerces, doesn't crash.
	_check(not _is_err(c.dispatch("call_method", { "path": ".", "method": "get_class", "args": "notarray" })), "call_method non-array args ok")

	# 10) create_node with an instantiable-but-NOT-Node class must error, not crash.
	_check(_is_err(c.dispatch("create_node", { "parent": ".", "type": "RefCounted" })), "create_node RefCounted -> error (not a Node)")
	_check(_is_err(c.dispatch("create_node", { "parent": ".", "type": "Resource" })), "create_node Resource -> error")
	_check(_is_err(c.dispatch("create_node", { "parent": ".", "type": "Object" })), "create_node Object -> error")
	_check(is_instance_valid(root), "root alive after bad create_node")

	# 11) run_script returning a cyclic structure must not overflow.
	var rs = c.dispatch("run_script", { "source": "var d = {}
d.self = d
return d" })
	_check(not _is_err(rs), "run_script returning cyclic dict ok (depth-guarded)")

	# 12) set_property with a wildly wrong-typed value must not crash.
	_check(not _is_err(c.dispatch("set_property", { "path": "Kid", "property": "position", "value": "not a vector" })), "set_property wrong type ok")
	_check(not _is_err(c.dispatch("set_property", { "path": "Kid", "property": "nonexistent_prop", "value": 5 })), "set_property unknown prop ok")

	# 13) instance_scene with a non-PackedScene resource must error.
	_check(_is_err(c.dispatch("instance_scene", { "scene": "res://addons/godot_mcp/mcp_commands.gd", "parent": "." })), "instance_scene non-scene -> error")

	# 14) reparent a node under its OWN descendant (would create a cycle) must
	#     not crash. (A->A/Sub; reparent A under Sub.)
	c.dispatch("create_node", { "parent": "A", "type": "Node3D", "name": "Sub" })
	var rc = c.dispatch("reparent_node", { "path": "A", "new_parent": "A/Sub" })
	_check(is_instance_valid(root) and is_instance_valid(kid), "reparent into own descendant did not crash editor")

	# 15) run_script with a RUNTIME error (not a parse error) must return, not crash.
	var rerr = c.dispatch("run_script", { "source": "return [0][7]" })
	_check(rerr != null, "run_script runtime error returns (no crash)")

	# 16) connect_signal to a nonexistent signal / missing target must error.
	_check(_is_err(c.dispatch("connect_signal", { "path": "Kid", "signal": "no_such_signal", "target": "Kid", "method": "queue_free" })), "connect bad signal -> error")
	_check(_is_err(c.dispatch("connect_signal", { "path": "Kid", "signal": "tree_entered", "target": "Ghost", "method": "x" })), "connect missing target -> error")


	root.free()


func h_route(body: String) -> Dictionary:
	return Http.new().route("POST", "/mcp", {}, body)
