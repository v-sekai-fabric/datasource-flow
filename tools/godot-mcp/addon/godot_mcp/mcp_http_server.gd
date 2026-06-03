@tool
extends RefCounted
class_name MCPHttpServer
## MCP streamable-HTTP server in pure GDScript — lets the Godot editor BE the
## MCP server with no external (Python) process. A minimal HTTP/1.1 endpoint
## over TCPServer handles `POST /mcp` with JSON-RPC, replying `application/json`
## or `text/event-stream` (SSE) per the client's Accept header.
##
## route() is transport-free (parsed request -> response parts) so it's unit-
## tested headless (tests/test_http.gd); poll() does the TCP/HTTP plumbing.

const MCPProtocolLib = preload("mcp_protocol.gd")
const DEFAULT_PORT := 8788

var protocol = MCPProtocolLib.new()
var _server := TCPServer.new()
var _clients: Array = []   # [{ peer: StreamPeerTCP, buf: PackedByteArray }]


func start(port: int = DEFAULT_PORT, host: String = "127.0.0.1") -> int:
	return _server.listen(port, host)

func stop() -> void:
	for c in _clients:
		(c.peer as StreamPeerTCP).disconnect_from_host()
	_clients.clear()
	_server.stop()


# --- pure routing (testable) -------------------------------------------------

## Produce { code, ctype, body } for a parsed HTTP request. No sockets.
func route(method: String, path: String, headers: Dictionary, body: String) -> Dictionary:
	if method == "OPTIONS":
		return { "code": 204, "ctype": "text/plain", "body": "" }          # CORS preflight
	if not path.begins_with("/mcp"):
		return { "code": 404, "ctype": "text/plain", "body": "not found" }
	if method == "DELETE":
		return { "code": 200, "ctype": "text/plain", "body": "" }          # session end
	if method == "GET":
		# server->client stream; we issue no server-initiated messages.
		return { "code": 405, "ctype": "text/plain", "body": "method not allowed" }
	if method != "POST":
		return { "code": 405, "ctype": "text/plain", "body": "method not allowed" }

	var req = JSON.parse_string(body)
	var resp = protocol.handle_rpc(req)
	if resp == null:
		return { "code": 202, "ctype": "text/plain", "body": "" }          # notification
	var text := JSON.stringify(resp)
	if "text/event-stream" in String(headers.get("accept", "")):
		return { "code": 200, "ctype": "text/event-stream",
			"body": "event: message\ndata: " + text + "\n\n" }
	return { "code": 200, "ctype": "application/json", "body": text }


# --- TCP / HTTP plumbing -----------------------------------------------------

func poll() -> void:
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
			var got := peer.get_data(avail)
			if got[0] == OK:
				c.buf.append_array(got[1])
		_try_request(c)

func _try_request(c) -> void:
	var sep := _find_seq(c.buf, "\r\n\r\n".to_utf8_buffer())
	if sep < 0:
		return                                   # headers incomplete
	var header_text: String = c.buf.slice(0, sep).get_string_from_utf8()
	var lines := header_text.split("\r\n")
	var rl := lines[0].split(" ")
	var method := rl[0] if rl.size() > 0 else ""
	var path := rl[1] if rl.size() > 1 else "/"
	var headers := {}
	for i in range(1, lines.size()):
		var idx := lines[i].find(":")
		if idx > 0:
			headers[lines[i].substr(0, idx).strip_edges().to_lower()] = lines[i].substr(idx + 1).strip_edges()
	var clen := int(headers.get("content-length", "0"))
	var body_start := sep + 4
	if c.buf.size() < body_start + clen:
		return                                   # body incomplete
	var body: String = c.buf.slice(body_start, body_start + clen).get_string_from_utf8()
	c.buf = c.buf.slice(body_start + clen)
	var r := route(method, path, headers, body)
	_write(c.peer, r, String(headers.get("mcp-session-id", "godot-mcp")))

func _write(peer: StreamPeerTCP, r: Dictionary, session: String) -> void:
	var code := int(r.code)
	var status: String = {
		200: "OK", 202: "Accepted", 204: "No Content",
		404: "Not Found", 405: "Method Not Allowed",
	}.get(code, "OK")
	var body_bytes := String(r.body).to_utf8_buffer()
	var h := "HTTP/1.1 %d %s\r\n" % [code, status]
	h += "Content-Type: %s\r\n" % r.ctype
	h += "Content-Length: %d\r\n" % body_bytes.size()
	h += "Mcp-Session-Id: %s\r\n" % session
	h += "Access-Control-Allow-Origin: *\r\n"
	h += "Access-Control-Allow-Headers: *\r\n"
	h += "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n"
	h += "Connection: close\r\n\r\n"
	peer.put_data(h.to_utf8_buffer())
	if body_bytes.size() > 0:
		peer.put_data(body_bytes)
	peer.disconnect_from_host()

func _find_seq(buf: PackedByteArray, seq: PackedByteArray) -> int:
	var limit := buf.size() - seq.size()
	var i := 0
	while i <= limit:
		var ok := true
		for j in seq.size():
			if buf[i + j] != seq[j]:
				ok = false
				break
		if ok:
			return i
		i += 1
	return -1
