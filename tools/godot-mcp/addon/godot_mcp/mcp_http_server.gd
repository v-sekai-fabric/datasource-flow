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
const BufferLib = preload("mcp_command_buffer.gd")
const DEFAULT_PORT := 8788
const DRAIN_PER_FRAME := 16     # constant per-frame execution budget

var protocol = MCPProtocolLib.new()
var _server := TCPServer.new()
var _clients: Array = []        # connections still reading a request
var _buffer = BufferLib.new(256)  # parsed requests awaiting constant-work drain


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
	# --- ingest: accept + parse complete requests into the buffer ----------
	# Bounded heavy work: parsing is cheap; the expensive part (route ->
	# protocol -> command dispatch) is deferred to the constant-budget drain.
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
		var req = _try_parse(c.buf)
		if req != null:
			_clients.erase(c)                         # request complete; leaves the reading set
			req["peer"] = peer
			if not _buffer.enqueue(req):
				# backpressure: answer immediately rather than queueing unboundedly
				_write(peer, { "code": 503, "ctype": "text/plain", "body": "server busy" },
					String(req.headers.get("mcp-session-id", "godot-mcp")))

	# --- constant-work drain: execute at most DRAIN_PER_FRAME requests ------
	for item in _buffer.drain(DRAIN_PER_FRAME):
		var r := route(item.method, item.path, item.headers, item.body)
		_write(item.peer, r, String(item.headers.get("mcp-session-id", "godot-mcp")))

## Parse one complete HTTP request out of `buf` (mutating it), or null if more
## bytes are needed. Returns { method, path, headers, body }.
func _try_parse(buf: PackedByteArray):
	var sep := _find_seq(buf, "\r\n\r\n".to_utf8_buffer())
	if sep < 0:
		return null                                   # headers incomplete
	var header_text: String = buf.slice(0, sep).get_string_from_utf8()
	var lines := header_text.split("\r\n")
	var rl := lines[0].split(" ")
	var headers := {}
	for i in range(1, lines.size()):
		var idx := lines[i].find(":")
		if idx > 0:
			headers[lines[i].substr(0, idx).strip_edges().to_lower()] = lines[i].substr(idx + 1).strip_edges()
	var clen := int(headers.get("content-length", "0"))
	var body_start := sep + 4
	if buf.size() < body_start + clen:
		return null                                   # body incomplete
	var body: String = buf.slice(body_start, body_start + clen).get_string_from_utf8()
	# NOTE: callers drop the connection after one response; we don't reslice buf.
	return {
		"method": (rl[0] if rl.size() > 0 else ""),
		"path": (rl[1] if rl.size() > 1 else "/"),
		"headers": headers,
		"body": body,
	}

func _write(peer: StreamPeerTCP, r: Dictionary, session: String) -> void:
	var code := int(r.code)
	var status: String = {
		200: "OK", 202: "Accepted", 204: "No Content",
		404: "Not Found", 405: "Method Not Allowed", 503: "Service Unavailable",
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
