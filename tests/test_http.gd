extends SceneTree
## Headless tests for MCPHttpServer.route() — the transport-free HTTP routing of
## the MCP streamable-HTTP server (no sockets involved).
## Run: godot --headless --path . --script res://tests/test_http.gd

const Http = preload("res://addons/vsekai_godot_mcp/mcp_http_server.gd")

var _fail := 0
var _pass := 0


func _initialize() -> void:
	_run()
	print("\n[godot_mcp http tests] %d passed, %d failed" % [_pass, _fail])
	quit(1 if _fail > 0 else 0)


func _check(cond: bool, msg: String) -> void:
	if cond:
		_pass += 1
	else:
		_fail += 1
		printerr("  FAIL: ", msg)


func _rpc(method: String, params: Dictionary, id = 1) -> String:
	var m := { "jsonrpc": "2.0", "method": method, "params": params }
	if id != null:
		m["id"] = id
	return JSON.stringify(m)


func _run() -> void:
	var h = Http.new()
	var root := Node3D.new(); root.name = "Root"
	h.protocol.commands.root = root

	# CORS preflight
	_check(h.route("OPTIONS", "/mcp", {}, "").code == 204, "OPTIONS -> 204")
	# wrong path / method
	_check(h.route("POST", "/other", {}, "").code == 404, "non-/mcp -> 404")
	_check(h.route("GET", "/mcp", {}, "").code == 405, "GET -> 405")

	# initialize over POST -> application/json with a JSON-RPC result
	var ini := h.route("POST", "/mcp", {}, _rpc("initialize", {}))
	_check(ini.code == 200 and ini.ctype == "application/json", "POST initialize -> 200 json")
	var ini_obj = JSON.parse_string(ini.body)
	_check(ini_obj.get("result", {}).get("serverInfo", {}).get("name") == "vsekai-godot-mcp", "initialize body is JSON-RPC result")

	# tools/list
	var tl := h.route("POST", "/mcp", {}, _rpc("tools/list", {}))
	var tl_obj = JSON.parse_string(tl.body)
	_check(tl_obj.result.tools.size() >= 20, "tools/list body carries tools")

	# tools/call get_scene_tree (root injected) -> content payload
	var call := h.route("POST", "/mcp", {}, _rpc("tools/call", { "name": "get_scene_tree", "arguments": {} }))
	var call_obj = JSON.parse_string(call.body)
	_check(call_obj.result.get("isError") == false, "tools/call ok")
	var payload = JSON.parse_string(call_obj.result.content[0].text)
	_check(payload.get("name") == "Root", "tools/call payload has scene tree")

	# notification (no id) -> 202, empty
	var note := h.route("POST", "/mcp", {}, _rpc("notifications/initialized", {}, null))
	_check(note.code == 202, "notification -> 202")

	# SSE: Accept text/event-stream -> event-stream body
	var sse := h.route("POST", "/mcp", { "accept": "application/json, text/event-stream" }, _rpc("ping", {}))
	_check(sse.code == 200 and sse.ctype == "text/event-stream", "Accept SSE -> event-stream")
	_check(sse.body.begins_with("event: message\ndata: "), "SSE body framed")

	# --- streamable-HTTP behavior (de-facto: match the reference SDKs) ---
	# Origin enforcement is OFF by default, like @modelcontextprotocol/sdk —
	# any Origin is accepted so legit browser/proxy clients aren't rejected.
	_check(h.route("POST", "/mcp", { "origin": "https://evil.example.com" }, _rpc("ping", {})).code == 200, "default: foreign Origin allowed (enforce off)")
	# Opt-in strict mode reinstates the spec MUST.
	h.enforce_origin = true
	_check(h.route("POST", "/mcp", { "origin": "https://evil.example.com" }, _rpc("ping", {})).code == 403, "enforce: foreign Origin -> 403")
	_check(h.route("POST", "/mcp", { "origin": "http://localhost:5173" }, _rpc("ping", {})).code == 200, "enforce: localhost Origin allowed")
	_check(h.route("POST", "/mcp", {}, _rpc("ping", {})).code != 403, "enforce: no-Origin (CLI) allowed")
	h.enforce_origin = false
	# DELETE (no sessions) -> 405
	_check(h.route("DELETE", "/mcp", {}, "").code == 405, "DELETE -> 405")
	# MCP-Protocol-Version: unsupported -> 400, supported -> 200, absent -> 200
	_check(h.route("POST", "/mcp", { "mcp-protocol-version": "1999-01-01" }, _rpc("ping", {})).code == 400, "bad protocol version -> 400")
	_check(h.route("POST", "/mcp", { "mcp-protocol-version": "2025-06-18" }, _rpc("ping", {})).code == 200, "supported protocol version ok")
	# Parse error -> JSON-RPC -32700
	var pe := h.route("POST", "/mcp", {}, "{ this is not json")
	var pe_obj = JSON.parse_string(pe.body)
	_check(pe.code == 200 and pe_obj.error.code == -32700, "unparseable body -> -32700")
	# JSON-RPC response (no "method") -> 202 accepted, no reply
	_check(h.route("POST", "/mcp", {}, '{"jsonrpc":"2.0","id":9,"result":{}}').code == 202, "response (no method) -> 202")

	root.free()
