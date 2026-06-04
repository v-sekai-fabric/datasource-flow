# Godot MCP

Drive the **Godot editor** from an MCP client — the Godot-side counterpart to
[IvanMurzak/Unity-MCP](https://github.com/IvanMurzak/Unity-MCP)
(`com.ivanmurzak.unity.mcp`). Built so the Godot host can be exercised the same
way as the Unity host for the two-implementation interop check (Linear CHI-312).

## Architecture

The Godot editor **is** the MCP server — no external (Python) process required:

```
MCP client ──(streamable-HTTP: POST /mcp)──▶ vsekai_godot_mcp addon ──▶ Godot editor
                                              EditorPlugin (GDScript)
```

The addon (`addon/vsekai_godot_mcp/`) serves the MCP **streamable-HTTP** transport
directly in GDScript over `TCPServer`, on `http://127.0.0.1:8788/mcp`:

- `mcp_http_server.gd` — minimal HTTP/1.1 + streamable-HTTP (POST → JSON-RPC,
  replying `application/json` or `text/event-stream` per `Accept`).
- `mcp_protocol.gd` — the MCP / JSON-RPC layer (uses Godot's built-in `JSONRPC`
  for envelopes): `initialize`, `tools/list`, `tools/call`, `ping`.
- `mcp_command_buffer.gd` — fixed-capacity ring (graphics-API command-buffer
  vocabulary: `record()` to enqueue, `submit()` to drain) submitted at a
  **constant per-frame budget**, so editor frame cost doesn't scale with MCP
  request load (bounded work + backpressure; AWS "constant work" pattern).
  Parsing records into it; a fixed budget (`SUBMIT_PER_FRAME`) executes per frame.
- `mcp_commands.gd` — transport-free command logic (`MCPCommands`) against
  `EditorInterface` + the edited `SceneTree`. Unit-tested headless.

### Reflection model

The core is **dynamic reflection by name via `Variant`** — `godot_call_method`,
`godot_get_property`, `godot_set_property`, `godot_list_methods/properties` map
straight onto `Object.callv()` / `Object.get()` / `Object.set()`. This is the
same model the **Godot Sandbox** `program/cpp/api` exposes (calling Godot
methods/properties by name through the Variant API), and it mirrors the
Unity-MCP's "any method becomes a tool" reflection. `godot_run_script` (GDScript
eval) is the escape hatch for multi-step logic, equivalent to Unity execute-C#.

## Tools

| Tool | Unity-MCP analogue |
|------|--------------------|
| `godot_get_scene_tree`, `godot_get_node` | scene hierarchy read |
| `godot_create_node`, `godot_delete_node`, `godot_reparent_node`, `godot_set_script` | GameObject create/destroy/parent/component |
| `godot_get_property`, `godot_set_property`, `godot_call_method`, `godot_list_methods`, `godot_list_properties` | reflection (any method/property) |
| `godot_open_scene`, `godot_save_scene`, `godot_get_open_scene`, `godot_list_scenes` | scene management |
| `godot_play_scene`, `godot_play_main`, `godot_stop`, `godot_is_playing` | play mode |
| `godot_run_script` | execute C# (Roslyn) |
| `godot_read_log` | read console (best-effort) |
| `godot_screenshot` | capture screenshot |
| `godot_ping` | connectivity |

## Setup (no Python)

1. **Enable the addon** in your Godot project:
   - copy `addon/vsekai_godot_mcp/` → `<project>/addons/vsekai_godot_mcp/`
   - Project → Project Settings → Plugins → enable **Godot MCP Bridge**
   - the Output panel prints `MCP streamable-HTTP on http://127.0.0.1:8788/mcp`.
2. **Point your MCP client at the addon** — that's it:
   ```jsonc
   // MCP client config (Claude Code / Cursor / …)
   { "mcpServers": { "godot": { "type": "http", "url": "http://127.0.0.1:8788/mcp" } } }
   ```

Bind host/port are constants in `mcp_bridge.gd` (`HOST`, `HTTP_PORT` = 8788).

## Tests

Headless, no editor needed:
```
godot --headless --path . --script res://tests/test_commands.gd
godot --headless --path . --script res://tests/test_protocol.gd
godot --headless --path . --script res://tests/test_http.gd
```

## Status / parity

v1 covers Scene-Hierarchy, Scripting/Editor (reflection + eval), play mode,
screenshot, and **editor-console capture** (`read_log` reads the Output dock's
`EditorLog` RichTextLabel directly — prints/warnings/errors — falling back to
the log file). Not yet: the Profiling/Diagnostics category and asset-pipeline
operations beyond scenes. Tracked in Linear CHI-313.
