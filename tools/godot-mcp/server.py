#!/usr/bin/env python3
"""
Godot MCP server — exposes the Godot editor to an MCP client by forwarding to
the godot_mcp editor addon's TCP bridge (addon/godot_mcp/mcp_bridge.gd).

Godot-side counterpart to IvanMurzak/Unity-MCP (com.ivanmurzak.unity.mcp):
- Unity GameObject  -> Godot Node          (create/delete/reparent/get tree)
- Unity component   -> Godot node/property/script
- execute C# (Roslyn) -> run_script (GDScript eval)
- "any method via reflection" -> call_method + list_methods/list_properties +
                                 get_property/set_property
- play mode -> play_scene / play_main / stop / is_playing
- read console -> read_log (best-effort)   ;  screenshots -> screenshot

Transport: this process speaks MCP stdio to the client and a tiny line-delimited
JSON protocol over TCP to the editor addon (default 127.0.0.1:9510). Start Godot
with the addon enabled, then run this server from your MCP client.

Requires: pip install "mcp[cli]"   (the official Python MCP SDK / FastMCP)
"""
from __future__ import annotations

import json
import os
import socket
from typing import Any

from mcp.server.fastmcp import FastMCP

# Godot editor bridge (TCP to the addon).
BRIDGE_HOST = os.environ.get("GODOT_MCP_HOST", "127.0.0.1")
BRIDGE_PORT = int(os.environ.get("GODOT_MCP_PORT", "9510"))
TIMEOUT = float(os.environ.get("GODOT_MCP_TIMEOUT", "30"))

# MCP transport. HTTP streaming (streamable-http) is FIRST CLASS — remote- and
# Docker-friendly, matching the Unity-MCP's streamableHttp mode. Set
# GODOT_MCP_TRANSPORT=stdio for a local stdio launch instead.
MCP_HOST = os.environ.get("MCP_HOST", "127.0.0.1")
MCP_PORT = int(os.environ.get("MCP_PORT", "8787"))

mcp = FastMCP("godot-mcp", host=MCP_HOST, port=MCP_PORT)


def _rpc(cmd: str, **args: Any) -> Any:
    """Send one command to the editor bridge and return its result.

    Opens a fresh connection per call (the addon multiplexes clients), frames
    the request as one JSON line, and reads one JSON line back.
    """
    payload = (json.dumps({"id": 1, "cmd": cmd, "args": args}) + "\n").encode("utf-8")
    try:
        with socket.create_connection((HOST, PORT), timeout=TIMEOUT) as s:
            s.sendall(payload)
            buf = b""
            while b"\n" not in buf:
                chunk = s.recv(65536)
                if not chunk:
                    break
                buf += chunk
    except OSError as e:
        raise RuntimeError(
            f"cannot reach the Godot bridge at {HOST}:{PORT} — is Godot open with "
            f"the godot_mcp addon enabled? ({e})"
        )
    line = buf.split(b"\n", 1)[0].decode("utf-8", "replace")
    resp = json.loads(line)
    if not resp.get("ok", False):
        raise RuntimeError(resp.get("error", "godot bridge error"))
    return resp.get("result")


# --- connectivity ------------------------------------------------------------

@mcp.tool()
def godot_ping() -> dict:
    """Check the editor bridge is reachable; returns the Godot engine version."""
    return _rpc("ping")


# --- scene tree / nodes (Unity GameObject parity) ----------------------------

@mcp.tool()
def godot_get_scene_tree(max_depth: int = 64) -> dict:
    """Dump the currently-edited scene's node tree (name, type, script, path, children)."""
    return _rpc("get_scene_tree", max_depth=max_depth)


@mcp.tool()
def godot_get_node(path: str) -> dict:
    """Get a node and its editor-visible properties. `path` is relative to the scene root ('.' = root)."""
    return _rpc("get_node", path=path)


@mcp.tool()
def godot_create_node(parent: str, type: str, name: str = "") -> dict:
    """Create a node of `type` (a Godot class, e.g. 'Node3D') under `parent`; returns its path."""
    return _rpc("create_node", parent=parent, type=type, name=name or type)


@mcp.tool()
def godot_delete_node(path: str) -> dict:
    """Delete the node at `path` (cannot be the scene root)."""
    return _rpc("delete_node", path=path)


@mcp.tool()
def godot_reparent_node(path: str, new_parent: str, keep_global_transform: bool = True) -> dict:
    """Move the node at `path` under `new_parent`."""
    return _rpc("reparent_node", path=path, new_parent=new_parent,
                keep_global_transform=keep_global_transform)


@mcp.tool()
def godot_set_script(path: str, script: str) -> dict:
    """Attach a script resource (res://… path) to the node at `path`."""
    return _rpc("set_script", path=path, script=script)


# --- reflection (Unity "any method / property" parity) -----------------------

@mcp.tool()
def godot_get_property(path: str, property: str) -> dict:
    """Read any property of the node at `path` by name."""
    return _rpc("get_property", path=path, property=property)


@mcp.tool()
def godot_set_property(path: str, property: str, value: Any) -> dict:
    """Set any property of the node at `path`. Math types accept arrays ([x,y,z]) or
    tagged dicts ({"__t__":"Vector3","x":..}); returns the readback value."""
    return _rpc("set_property", path=path, property=property, value=value)


@mcp.tool()
def godot_call_method(path: str, method: str, args: list | None = None) -> dict:
    """Call any method by name on the node at `path` with positional `args`; returns the result."""
    return _rpc("call_method", path=path, method=method, args=args or [])


@mcp.tool()
def godot_list_methods(path: str) -> dict:
    """List the callable methods (name + arg count) of the node at `path`."""
    return _rpc("list_methods", path=path)


@mcp.tool()
def godot_list_properties(path: str) -> dict:
    """List the editor-visible properties (name + type) of the node at `path`."""
    return _rpc("list_properties", path=path)


# --- scenes ------------------------------------------------------------------

@mcp.tool()
def godot_open_scene(path: str) -> dict:
    """Open a scene by res:// path in the editor."""
    return _rpc("open_scene", path=path)


@mcp.tool()
def godot_save_scene() -> dict:
    """Save the currently-edited scene."""
    return _rpc("save_scene")


@mcp.tool()
def godot_get_open_scene() -> dict:
    """Return the path + root of the currently-edited scene."""
    return _rpc("get_open_scene")


@mcp.tool()
def godot_list_scenes() -> dict:
    """List all .tscn scenes under res://."""
    return _rpc("list_scenes")


# --- play mode (Unity play-mode parity) --------------------------------------

@mcp.tool()
def godot_play_scene(path: str = "") -> dict:
    """Run the current scene, or a specific scene if `path` (res://…) is given."""
    return _rpc("play_scene", path=path)


@mcp.tool()
def godot_play_main() -> dict:
    """Run the project's main scene."""
    return _rpc("play_main")


@mcp.tool()
def godot_stop() -> dict:
    """Stop the running scene."""
    return _rpc("stop")


@mcp.tool()
def godot_is_playing() -> dict:
    """Whether a scene is currently playing from the editor."""
    return _rpc("is_playing")


# --- eval / logs / screenshot (Unity execute-C# / console / screenshot) ------

@mcp.tool()
def godot_run_script(source: str) -> dict:
    """Run a GDScript snippet in the editor. The snippet is the body of
    `run(editor, root)` — `editor` is the EditorInterface, `root` the edited
    scene root — and may `return` a value. (Parity with Unity execute-C#.)
    Prefer the reflection tools (godot_call_method / godot_get_property /
    godot_set_property) — same Variant-by-name model as the Godot Sandbox
    cpp/api — and use run_script only for multi-step logic."""
    return _rpc("run_script", source=source)


@mcp.tool()
def godot_read_log(lines: int = 100) -> dict:
    """Best-effort tail of the editor log (requires file logging enabled)."""
    return _rpc("read_log", lines=lines)


@mcp.tool()
def godot_screenshot(path: str = "user://godot_mcp_screenshot.png") -> dict:
    """Capture the editor viewport to a PNG; returns the absolute path + size."""
    return _rpc("screenshot", path=path)


if __name__ == "__main__":
    # HTTP streaming is the default/first-class transport. Override with
    # GODOT_MCP_TRANSPORT=stdio for a local stdio launch.
    transport = os.environ.get("GODOT_MCP_TRANSPORT", "streamable-http")
    mcp.run(transport=transport)
