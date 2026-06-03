@tool
extends EditorPlugin
## Godot MCP Bridge — editor-side TCP transport.
##
## Opens 127.0.0.1:<PORT> and frames newline-delimited JSON. All command logic
## lives in MCPCommands (mcp_commands.gd), which is transport-free so it can be
## unit-tested headless (tools/godot-mcp/tests/). Per request we inject the
## edited scene root + EditorInterface and delegate to it.
##
##   request : {"id": <any>, "cmd": "<name>", "args": {..}}
##   response: {"id": <any>, "ok": true,  "result": <json>}
##           | {"id": <any>, "ok": false, "error": "<msg>"}

const PORT := 9510
const HOST := "127.0.0.1"

var _server := TCPServer.new()
var _clients: Array = []          # [{ peer: StreamPeerTCP, buf: PackedByteArray }]
var _cmds := MCPCommands.new()


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
		var args = req.get("args", {})
		if typeof(args) != TYPE_DICTIONARY:
			args = {}
		# Inject live editor state, then delegate to the transport-free logic.
		_cmds.editor = get_editor_interface()
		_cmds.root = get_editor_interface().get_edited_scene_root()
		var result = _cmds.dispatch(String(req["cmd"]), args)
		if result is Dictionary and result.get("__error__", false):
			resp = { "id": rid, "ok": false, "error": String(result.get("msg", "error")) }
		else:
			resp = { "id": rid, "ok": true, "result": result }
	peer.put_data((JSON.stringify(resp) + "\n").to_utf8_buffer())
