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
const SUBMIT_PER_FRAME := 16    # constant per-frame execution budget

var protocol = MCPProtocolLib.new()
var _server := TCPServer.new()
var _clients: Array = []        # connections still reading a request
var _buffer = BufferLib.new(256)  # parsed requests awaiting constant-work submit

# DNS-rebinding (Origin) protection. The MCP spec says servers MUST validate
# Origin, but the reference TypeScript/Python SDKs ship it DISABLED by default
# (enableDnsRebindingProtection=false; cf. CVE-2025-66414) and most real
# streamable-HTTP servers follow suit, relying on the localhost bind. We match
# that de-facto default (off) so we don't reject legitimate Origin-setting
# clients; flip enforce_origin=true to get spec-strict 403s.
var enforce_origin := false


func start(port: int = DEFAULT_PORT, host: String = "127.0.0.1") -> int:
	return _server.listen(port, host)

func stop() -> void:
	for c in _clients:
		(c.peer as StreamPeerTCP).disconnect_from_host()
	_clients.clear()
	_server.stop()


# --- pure routing (testable) -------------------------------------------------

const SUPPORTED_VERSIONS := ["2025-06-18", "2025-03-26"]

## Produce { code, ctype, body } for a parsed HTTP request. No sockets.
func route(method: String, path: String, headers: Dictionary, body: String) -> Dictionary:
	# Origin / DNS-rebinding protection — OFF by default to match the reference
	# SDKs and real-world servers (see enforce_origin above). When enabled, a
	# present non-localhost Origin is rejected; absent Origin (CLI clients) is
	# always allowed.
	if enforce_origin:
		var origin := String(headers.get("origin", ""))
		if origin != "" and not _origin_allowed(origin):
			return { "code": 403, "ctype": "text/plain", "body": "forbidden origin" }

	if method == "OPTIONS":
		return { "code": 204, "ctype": "text/plain", "body": "" }          # CORS preflight
	if not path.begins_with("/mcp"):
		return { "code": 404, "ctype": "text/plain", "body": "not found" }
	if method == "DELETE":
		# stateless: we don't manage sessions, so we don't allow termination.
		return { "code": 405, "ctype": "text/plain", "body": "session termination not supported" }
	if method == "GET":
		return { "code": 405, "ctype": "text/plain", "body": "no server stream" }
	if method != "POST":
		return { "code": 405, "ctype": "text/plain", "body": "method not allowed" }

	# Protocol version (MUST 400 on unsupported, when the header is present).
	var pv := String(headers.get("mcp-protocol-version", ""))
	if pv != "" and not (pv in SUPPORTED_VERSIONS):
		return { "code": 400, "ctype": "application/json",
			"body": JSON.stringify(protocol.parse_error_response(-32600, "unsupported MCP-Protocol-Version: " + pv)) }

	# Parse (MUST -32700 on an unparseable non-empty body).
	var req = JSON.parse_string(body)
	if req == null and body.strip_edges() != "":
		return { "code": 200, "ctype": "application/json",
			"body": JSON.stringify(protocol.parse_error_response(-32700, "parse error")) }

	var resp = protocol.handle_rpc(req)
	if resp == null:
		return { "code": 202, "ctype": "text/plain", "body": "" }          # notification/response accepted
	var text := JSON.stringify(resp)
	if "text/event-stream" in String(headers.get("accept", "")):
		return { "code": 200, "ctype": "text/event-stream",
			"body": "event: message\ndata: " + text + "\n\n" }
	return { "code": 200, "ctype": "application/json", "body": text }

func _origin_allowed(origin: String) -> bool:
	var o := origin.to_lower()
	return o.begins_with("http://localhost") or o.begins_with("https://localhost") \
		or o.begins_with("http://127.0.0.1") or o.begins_with("https://127.0.0.1") \
		or o.begins_with("http://[::1]") or o.begins_with("https://[::1]")


# --- TCP / HTTP plumbing -----------------------------------------------------

func poll() -> void:
	# --- ingest: accept + parse complete requests into the buffer ----------
	# Bounded heavy work: parsing is cheap; the expensive part (route ->
	# protocol -> command dispatch) is deferred to the constant-budget submit.
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
			if not _buffer.record(req):
				# backpressure: answer immediately rather than queueing unboundedly
				_write(peer, { "code": 503, "ctype": "text/plain", "body": "server busy" })

	# --- constant-work submit: execute at most SUBMIT_PER_FRAME requests ----
	for item in _buffer.submit(SUBMIT_PER_FRAME):
		var r := route(item.method, item.path, item.headers, item.body)
		_write(item.peer, r)

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

func _write(peer: StreamPeerTCP, r: Dictionary) -> void:
	var code := int(r.code)
	var status: String = {
		200: "OK", 202: "Accepted", 204: "No Content", 400: "Bad Request",
		403: "Forbidden", 404: "Not Found", 405: "Method Not Allowed",
		503: "Service Unavailable",
	}.get(code, "OK")
	var body_bytes := String(r.body).to_utf8_buffer()
	var h := "HTTP/1.1 %d %s\r\n" % [code, status]
	h += "Content-Type: %s\r\n" % r.ctype
	h += "Content-Length: %d\r\n" % body_bytes.size()
	# Stateless: no Mcp-Session-Id (we don't manage sessions). CORS for browser
	# clients; Origin itself is validated in route().
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
