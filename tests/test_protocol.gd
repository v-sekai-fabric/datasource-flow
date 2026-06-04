extends SceneTree
## Headless tests for MCPProtocol (JSON-RPC / MCP handler).
## Run: godot --headless --path . --script res://tests/test_protocol.gd

const Protocol = preload("res://addons/vsekai_godot_mcp/mcp_protocol.gd")

var _fail := 0
var _pass := 0


func _initialize() -> void:
	_run()
	print("\n[godot_mcp protocol tests] %d passed, %d failed" % [_pass, _fail])
	quit(1 if _fail > 0 else 0)


func _check(cond: bool, msg: String) -> void:
	if cond:
		_pass += 1
	else:
		_fail += 1
		printerr("  FAIL: ", msg)


func _run() -> void:
	var p = Protocol.new()

	# initialize handshake
	var ini = p.handle_rpc({ "jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {} })
	_check(ini.get("result", {}).get("serverInfo", {}).get("name") == "vsekai-godot-mcp", "initialize serverInfo")
	_check(ini.result.has("protocolVersion"), "initialize protocolVersion")
	_check(ini.result.get("capabilities", {}).has("tools"), "initialize advertises tools")

	# notification -> no response
	_check(p.handle_rpc({ "jsonrpc": "2.0", "method": "notifications/initialized" }) == null, "notification -> null")

	# ping
	_check(p.handle_rpc({ "jsonrpc": "2.0", "id": 2, "method": "ping" }).has("result"), "ping result")

	# tools/list — count + schema shape
	var tl = p.handle_rpc({ "jsonrpc": "2.0", "id": 3, "method": "tools/list" })
	var tools: Array = tl.result.tools
	_check(tools.size() >= 20, "tools/list count >= 20")
	var schema_ok := true
	var has_get_scene_tree := false
	for t in tools:
		if not (t.has("name") and t.has("description") and t.has("inputSchema") and t.inputSchema.get("type") == "object"):
			schema_ok = false
		if t.name == "get_scene_tree":
			has_get_scene_tree = true
	_check(schema_ok, "every tool has name/description/inputSchema(object)")
	_check(has_get_scene_tree, "registry contains get_scene_tree")

	# tools/call ping -> content block, not error
	var cp = p.handle_rpc({ "jsonrpc": "2.0", "id": 4, "method": "tools/call",
		"params": { "name": "ping", "arguments": {} } })
	_check(cp.result.get("isError") == false, "tools/call ping not error")
	_check(cp.result.content[0].get("type") == "text", "tools/call content is text block")

	# tools/call unknown tool -> JSON-RPC error
	var cu = p.handle_rpc({ "jsonrpc": "2.0", "id": 5, "method": "tools/call",
		"params": { "name": "nope", "arguments": {} } })
	_check(cu.has("error") and cu.error.code == -32602, "unknown tool -> -32602")

	# tools/call against a real scene command (needs root injected)
	var root := Node3D.new(); root.name = "Root"
	p.commands.root = root
	var ct = p.handle_rpc({ "jsonrpc": "2.0", "id": 6, "method": "tools/call",
		"params": { "name": "get_scene_tree", "arguments": {} } })
	_check(ct.result.get("isError") == false, "tools/call get_scene_tree ok")
	var payload = JSON.parse_string(ct.result.content[0].text)
	_check(payload.get("name") == "Root", "tools/call payload carries scene tree")

	# tool-level error -> isError true (in-band, not JSON-RPC error)
	var ce = p.handle_rpc({ "jsonrpc": "2.0", "id": 7, "method": "tools/call",
		"params": { "name": "get_node", "arguments": { "path": "DoesNotExist" } } })
	_check(ce.has("result") and ce.result.get("isError") == true, "tool error -> isError true")

	# unknown method -> -32601
	var um = p.handle_rpc({ "jsonrpc": "2.0", "id": 8, "method": "foo/bar" })
	_check(um.has("error") and um.error.code == -32601, "unknown method -> -32601")

	root.free()
