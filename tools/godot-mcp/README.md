# Godot MCP

Drive the **Godot editor** from an MCP client — the Godot-side counterpart to
[IvanMurzak/Unity-MCP](https://github.com/IvanMurzak/Unity-MCP)
(`com.ivanmurzak.unity.mcp`). Built so the Godot host can be exercised the same
way as the Unity host for the two-implementation interop check (Linear CHI-312).

## Architecture

Two parts, mirroring the Unity-MCP (editor plugin + standalone server):

```
MCP client ──(HTTP streaming / stdio)──▶ server.py ──(TCP, line-JSON)──▶ godot_mcp addon ──▶ Godot editor
                                          FastMCP                         EditorPlugin
```

- **`addon/godot_mcp/`** — a Godot 4 `@tool` `EditorPlugin` that opens
  `127.0.0.1:9510` and dispatches newline-delimited JSON commands against the
  live `EditorInterface` + edited `SceneTree`.
- **`server.py`** — a FastMCP server that exposes the tools and forwards each to
  the addon. **HTTP streaming (`streamable-http`) is the first-class transport**
  (remote/Docker-friendly, matching the Unity-MCP's streamableHttp mode); stdio
  is available via `GODOT_MCP_TRANSPORT=stdio`.

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

## Setup

1. **Enable the addon** in your Godot project:
   - copy `addon/godot_mcp/` → `<project>/addons/godot_mcp/`
   - Project → Project Settings → Plugins → enable **Godot MCP Bridge**
   - the Output panel prints `bridge listening on 127.0.0.1:9510`.
2. **Install the server deps:** `pip install "mcp[cli]"`
3. **Run / register the server** (HTTP streaming on `127.0.0.1:8787` by default):
   ```jsonc
   // MCP client config (Claude Code / Cursor / …)
   {
     "mcpServers": {
       "godot": {
         "type": "http",
         "url": "http://127.0.0.1:8787/mcp"
       }
     }
   }
   ```
   Start it with `python tools/godot-mcp/server.py` (or, for stdio:
   `GODOT_MCP_TRANSPORT=stdio python tools/godot-mcp/server.py`).

## Env vars

| var | default | meaning |
|-----|---------|---------|
| `GODOT_MCP_TRANSPORT` | `streamable-http` | `streamable-http` \| `sse` \| `stdio` |
| `MCP_HOST` / `MCP_PORT` | `127.0.0.1` / `8787` | HTTP transport bind |
| `GODOT_MCP_HOST` / `GODOT_MCP_PORT` | `127.0.0.1` / `9510` | editor-bridge TCP |
| `GODOT_MCP_TIMEOUT` | `30` | per-command timeout (s) |

## Status / parity

v1 covers Scene-Hierarchy, Scripting/Editor (reflection + eval), play mode,
screenshot, and **editor-console capture** (`read_log` reads the Output dock's
`EditorLog` RichTextLabel directly — prints/warnings/errors — falling back to
the log file). Not yet: the Profiling/Diagnostics category and asset-pipeline
operations beyond scenes. Tracked in Linear CHI-313.
