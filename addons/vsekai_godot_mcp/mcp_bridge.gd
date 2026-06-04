@tool
extends EditorPlugin
## Godot MCP Bridge — the Godot editor IS the MCP server.
##
## Serves the MCP streamable-HTTP transport directly in GDScript
## (mcp_http_server.gd) on http://127.0.0.1:8788/mcp — MCP clients connect with
## NO external process. Per frame we inject the live EditorInterface + edited
## scene root into the shared command logic, then poll the HTTP server.

const MCPCommandsLib = preload("mcp_commands.gd")
const MCPHttpServerLib = preload("mcp_http_server.gd")

const HOST := "127.0.0.1"
const HTTP_PORT := 8788

var _cmds = MCPCommandsLib.new()
var _http = MCPHttpServerLib.new()


func _enter_tree() -> void:
	_http.protocol.commands = _cmds
	if _http.start(HTTP_PORT, HOST) == OK:
		print("[godot_mcp] MCP streamable-HTTP on http://%s:%d/mcp" % [HOST, HTTP_PORT])
	else:
		push_error("[godot_mcp] HTTP listen failed on :%d" % HTTP_PORT)
	set_process(true)


func _exit_tree() -> void:
	_http.stop()
	print("[godot_mcp] bridge stopped")


func _process(_delta: float) -> void:
	# Inject live editor state into the shared command logic, then serve.
	_cmds.editor = get_editor_interface()
	_cmds.root = get_editor_interface().get_edited_scene_root()
	_http.poll()
