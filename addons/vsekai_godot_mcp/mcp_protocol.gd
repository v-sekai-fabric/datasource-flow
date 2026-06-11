@tool
extends RefCounted
class_name MCPProtocol
## Pure MCP / JSON-RPC 2.0 handler — no transport, no HTTP. Turns a decoded
## JSON-RPC request into a response Dictionary (or null for notifications),
## dispatching tools/call to MCPCommands. This lets the Godot addon BE the MCP
## server (streamable-HTTP) with no external Python process; mcp_http_server.gd
## wraps this in a TCP/HTTP loop. Unit-tested headless (tests/test_protocol.gd).

const PROTOCOL_VERSION := "2025-06-18"
const SERVER_NAME := "vsekai-godot-mcp"
const SERVER_VERSION := "0.1.0"

# Relative preload (not the class_name global) so this resolves headless and
# wherever the addon is installed.
const MCPCommandsLib = preload("mcp_commands.gd")

var commands = null  # MCPCommands instance
var _rpc := JSONRPC.new()   # Godot's built-in JSON-RPC envelope helper


func _init() -> void:
	if commands == null:
		commands = MCPCommandsLib.new()


# --- JSON-RPC entry ----------------------------------------------------------

## Returns a JSON-RPC response Dictionary, or null when the message is a
## notification (no "id") that needs no reply.
func handle_rpc(req) -> Variant:
	if typeof(req) != TYPE_DICTIONARY:
		return _error(null, -32600, "invalid request")
	# A JSON-RPC message with no "method" is a response (to a server->client
	# request). We issue no server requests, so accept it with no reply (the
	# transport answers 202). Per spec these need no response.
	if not req.has("method"):
		return null
	var id = req.get("id", null)
	var method := String(req.get("method", ""))
	match method:
		"initialize":
			return _result(id, {
				"protocolVersion": PROTOCOL_VERSION,
				"capabilities": { "tools": { "listChanged": false } },
				"serverInfo": { "name": SERVER_NAME, "version": SERVER_VERSION },
			})
		"notifications/initialized", "notifications/cancelled":
			return null
		"ping":
			return _result(id, {})
		"tools/list":
			return _result(id, { "tools": _tool_schemas() })
		"tools/call":
			return _call_tool(id, req.get("params", {}))
		_:
			if id == null:
				return null  # unknown notification: ignore
			return _error(id, -32601, "method not found: " + method)


func _result(id, value) -> Dictionary:
	return _rpc.make_response(value, id)

func _error(id, code: int, msg: String) -> Dictionary:
	return _rpc.make_response_error(code, msg, id)

## JSON-RPC error with a null id — for transport-level failures (parse error
## -32700, unsupported protocol version) before/without a request id.
func parse_error_response(code: int, msg: String) -> Dictionary:
	return _rpc.make_response_error(code, msg, null)


# --- tools/call --------------------------------------------------------------

func _call_tool(id, params) -> Dictionary:
	if typeof(params) != TYPE_DICTIONARY:
		return _error(id, -32602, "invalid params")
	var name := String(params.get("name", ""))
	var args = params.get("arguments", {})
	if typeof(args) != TYPE_DICTIONARY:
		args = {}
	if not _tool_names().has(name):
		return _error(id, -32602, "unknown tool: " + name)
	var out = commands.dispatch(name, args)
	var is_err: bool = out is Dictionary and out.get("__error__", false)
	var text := JSON.stringify(out.get("msg", "error")) if is_err else JSON.stringify(out)
	# MCP tool result: content blocks + isError flag (tool errors are reported
	# in-band, not as JSON-RPC errors).
	return _result(id, {
		"content": [ { "type": "text", "text": text } ],
		"isError": is_err,
	})


# --- tool registry -----------------------------------------------------------
# Tool name == MCPCommands dispatch command; arguments pass straight through.

func _tool_defs() -> Array:
	return [
		["ping", "Check the bridge; returns the Godot engine version.", {}],
		["get_scene_tree", "Dump the edited scene's node tree.", { "max_depth": "integer" }],
		["get_node", "Get a node and its editor properties.", { "path": "string" }],
		["get_property", "Read any property of a node by name.", { "path": "string", "property": "string" }],
		["set_property", "Set any property of a node by name.", { "path": "string", "property": "string", "value": "any" }],
		["call_method", "Call any method on a node by name.", { "path": "string", "method": "string", "args": "array" }],
		["list_methods", "List a node's callable methods.", { "path": "string" }],
		["list_properties", "List a node's editor properties.", { "path": "string" }],
		["create_node", "Create a node of a class under a parent.", { "parent": "string", "type": "string", "name": "string" }],
		["delete_node", "Delete a node (not the root).", { "path": "string" }],
		["reparent_node", "Move a node under a new parent.", { "path": "string", "new_parent": "string" }],
		["set_script", "Attach a script (res://…) to a node.", { "path": "string", "script": "string" }],
		["run_script", "Run a GDScript snippet (body of run(editor, root)).", { "source": "string" }],
		["open_scene", "Open a scene by res:// path.", { "path": "string" }],
		["save_scene", "Save the edited scene.", {}],
		["get_open_scene", "Path + root of the edited scene.", {}],
		["list_scenes", "List all res:// .tscn scenes.", {}],
		["play_scene", "Run the current (or given) scene.", { "path": "string" }],
		["play_main", "Run the project's main scene.", {}],
		["stop", "Stop the running scene.", {}],
		["is_playing", "Whether a scene is playing.", {}],
		["get_performance", "Godot Performance monitors (fps, memory, draw calls…).", { "monitors": "array" }],
		["read_log", "Read the editor Output console (best-effort).", { "lines": "integer" }],
		["screenshot", "Capture the editor viewport to a PNG.", { "path": "string" }],
		# profiling / diagnostics
		["list_monitors", "List available Performance monitor keys.", {}],
		["get_memory_info", "Static memory usage + peak.", {}],
		["get_os_info", "OS name/model/locale/processor/threads.", {}],
		["get_video_info", "Video adapter name/vendor/API.", {}],
		["get_render_info", "RenderingServer frame stats (objects/primitives/draw calls/mem).", {}],
		["copy_file", "Copy a file.", { "from": "string", "to": "string" }],
		["move_file", "Move/rename a file.", { "from": "string", "to": "string" }],
		["make_dir", "Create a directory (recursive).", { "path": "string" }],
		["create_resource", "Create + save a Resource of a class.", { "class": "string", "path": "string" }],
		# project / assets
		["read_file", "Read a text file (res:// or user://).", { "path": "string" }],
		["write_file", "Write text to a file.", { "path": "string", "text": "string" }],
		["file_exists", "Whether a file or dir exists.", { "path": "string" }],
		["delete_file", "Delete a file.", { "path": "string" }],
		["list_dir", "List files + subdirs of a directory.", { "path": "string" }],
		["find_files", "Recursively find files by extension under a root.", { "root": "string", "ext": "string" }],
		["create_script", "Write a new GDScript file.", { "path": "string", "source": "string" }],
		["create_scene", "Create + save a new .tscn with a typed root.", { "path": "string", "root_type": "string", "root_name": "string" }],
		["instance_scene", "Instance a PackedScene under a parent.", { "scene": "string", "parent": "string" }],
		["save_branch_as_scene", "Pack a node subtree and save it as a .tscn.", { "path": "string", "scene": "string" }],
		["reimport_asset", "Reimport one or more assets after their source changed on disk (EditorFileSystem.reimport_files) — the analogue of Unity-MCP's asset refresh. Pass a single 'path' or a 'paths' array of res:// paths.", { "path": "string", "paths": "array" }],
		["rescan_filesystem", "Rescan the project filesystem for added/removed/changed files (EditorFileSystem.scan).", {}],
		["get_project_setting", "Read a ProjectSettings value.", { "setting": "string" }],
		["set_project_setting", "Set a ProjectSettings value.", { "setting": "string", "value": "any" }],
		# scene / hierarchy
		["duplicate_node", "Duplicate a node under its parent.", { "path": "string" }],
		["move_child", "Reorder a node among its siblings.", { "path": "string", "to_index": "integer" }],
		["add_to_group", "Add a node to a group.", { "path": "string", "group": "string" }],
		["remove_from_group", "Remove a node from a group.", { "path": "string", "group": "string" }],
		["get_nodes_in_group", "Paths of nodes in a group.", { "group": "string" }],
		["find_nodes", "Find nodes by name pattern and/or type.", { "name": "string", "type": "string" }],
		["get_node_count", "Count nodes under the scene root.", {}],
		# spatial: transforms / bounds / skeleton (geometry diagnosis)
		["get_transform", "Get a Node3D's local and global Transform3D plus world position, rotation (degrees) and scale. Transforms come back as Godot-native JSON (JSON.from_native).", { "path": "string" }],
		["get_aabb", "World-space bounding box (min/max/size/center) merged over every VisualInstance3D in a node's subtree (default: whole scene). Use it to frame a camera or spot a mesh that is offset/splayed out of place.", { "path": "string" }],
		["get_bone_poses", "Dump a Skeleton3D's bones — name, parent index, rest, local pose and global pose transforms (Godot-native JSON). Use to diagnose skeletal offsets, e.g. a shoulder/clavicle bone whose bind or rest transform is wrong. Optional 'bones' filters to specific bone names.", { "path": "string", "bones": "array" }],
		# reflection: ClassDB / signals / singletons
		["class_exists", "Whether a Godot class exists.", { "class": "string" }],
		["get_class_methods", "Methods of a class (ClassDB).", { "class": "string", "no_inheritance": "boolean" }],
		["get_class_properties", "Properties of a class (ClassDB).", { "class": "string", "no_inheritance": "boolean" }],
		["list_signals", "Signals of a node.", { "path": "string" }],
		["emit_signal", "Emit a node signal.", { "path": "string", "signal": "string", "args": "array" }],
		["connect_signal", "Connect a node signal to a target method.", { "path": "string", "signal": "string", "target": "string", "method": "string" }],
		["call_singleton", "Call a method on an Engine singleton (Input, OS…).", { "singleton": "string", "method": "string", "args": "array" }],
		["get_editor_setting", "Read an EditorSettings value.", { "setting": "string" }],
	]

func _tool_names() -> PackedStringArray:
	var names := PackedStringArray()
	for t in _tool_defs():
		names.append(t[0])
	return names

# MCP REQUIRES inputSchema to be a JSON Schema (clients validate tool arguments
# against it) — JSON-LD is not accepted here, so JSON Schema is the only correct
# choice for the tool surface. (A JSON-LD @context could annotate the avatar/USD
# *domain* separately, but not the MCP inputSchema.)
const _OPTIONAL_PARAMS := ["max_depth", "name", "args", "monitors", "default", "no_inheritance", "lines", "bones", "paths"]
const _ALL_OPTIONAL_TOOLS := ["play_scene", "screenshot", "get_performance", "find_nodes", "read_log", "get_aabb", "reimport_asset"]

func _tool_schemas() -> Array:
	var out := []
	for t in _tool_defs():
		var props := {}
		var required := []
		for pname in t[2]:
			var ptype: String = t[2][pname]
			props[pname] = {} if ptype == "any" else { "type": ptype }
			if not (t[0] in _ALL_OPTIONAL_TOOLS) and not (pname in _OPTIONAL_PARAMS):
				required.append(pname)
		var schema := { "type": "object", "properties": props, "additionalProperties": false }
		if not required.is_empty():
			schema["required"] = required
		out.append({ "name": t[0], "description": t[1], "inputSchema": schema })
	return out
