"""idtx-flow web host — proof-of-concept.

Reads an avatar USD through libidtx_core (server-side, via ctypes — the SAME
path the Blender host uses) and renders its meshes in the browser with viser.
No WebAssembly: the core runs natively in this Python process and viser streams
geometry to a three.js client over websockets.

Run:  pixi run serve     (then open the printed http://localhost:8080 URL)

Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
"""

from __future__ import annotations

import sys
import time
from ctypes import POINTER, c_char_p, c_float, c_int32, c_void_p
from pathlib import Path

import numpy as np
import viser

# web/viser-host -> web -> repo root
REPO = Path(__file__).resolve().parents[2]
USD_PATH = REPO / "build" / "miroir_from_unity.usda"

# Reuse the proven Blender ctypes loader: it finds libidtx_core, wires its
# OpenUSD dependency dirs, calls idtx_core_init(), and binds the export side +
# idtx_core_import_avatar_from_usd.
sys.path.insert(0, str(REPO / "openusd-fabric" / "blender"))
import idtx_core_ctypes as core  # noqa: E402


def _bind_reader(lib) -> None:
    """Declare the read-side ABI that load() does not bind (it only needed the
    export side for Blender). All of these already exist in idtx_core.h."""
    lib.idtx_avatar_get_name.argtypes = [c_void_p]
    lib.idtx_avatar_get_name.restype = c_char_p
    lib.idtx_avatar_get_mesh_count.argtypes = [c_void_p]
    lib.idtx_avatar_get_mesh_count.restype = c_int32
    lib.idtx_avatar_get_mesh.argtypes = [c_void_p, c_int32]
    lib.idtx_avatar_get_mesh.restype = c_void_p
    lib.idtx_mesh_get_name.argtypes = [c_void_p]
    lib.idtx_mesh_get_name.restype = c_char_p
    lib.idtx_mesh_get_vertex_count.argtypes = [c_void_p]
    lib.idtx_mesh_get_vertex_count.restype = c_int32
    lib.idtx_mesh_get_index_count.argtypes = [c_void_p]
    lib.idtx_mesh_get_index_count.restype = c_int32
    lib.idtx_mesh_get_positions.argtypes = [c_void_p, POINTER(c_float)]
    lib.idtx_mesh_get_indices.argtypes = [c_void_p, POINTER(c_int32)]


def read_avatar(lib, usd_path: Path):
    """USD -> [(name, verts Nx3 float32, faces Mx3 int32)] via the core."""
    avatar = lib.idtx_core_import_avatar_from_usd(str(usd_path).encode("utf-8"))
    if not avatar:
        raise RuntimeError(f"libidtx_core failed to import {usd_path}")
    try:
        name = (lib.idtx_avatar_get_name(avatar) or b"avatar").decode("utf-8")
        parts = []
        for i in range(lib.idtx_avatar_get_mesh_count(avatar)):
            mesh = lib.idtx_avatar_get_mesh(avatar, i)
            vc = lib.idtx_mesh_get_vertex_count(mesh)
            ic = lib.idtx_mesh_get_index_count(mesh)
            pos = (c_float * (vc * 3))()
            idx = (c_int32 * ic)()
            lib.idtx_mesh_get_positions(mesh, pos)
            lib.idtx_mesh_get_indices(mesh, idx)
            verts = np.ctypeslib.as_array(pos).reshape(-1, 3).astype(np.float32).copy()
            faces = np.ctypeslib.as_array(idx).reshape(-1, 3).astype(np.int32).copy()
            part = (lib.idtx_mesh_get_name(mesh) or f"mesh_{i}".encode()).decode("utf-8")
            parts.append((part, verts, faces))
        return name, parts
    finally:
        lib.idtx_avatar_destroy(avatar)


def main() -> None:
    lib = core.load(REPO)
    _bind_reader(lib)
    version = (lib.idtx_core_version() or b"?").decode("utf-8")
    print(f"[idtx] libidtx_core {version}")

    name, parts = read_avatar(lib, USD_PATH)
    print(f"[idtx] imported '{name}' — {len(parts)} mesh(es) from {USD_PATH.name}")

    server = viser.ViserServer()
    # The USD declares upAxis = "Y" (metersPerUnit = 1), matching the rest of the
    # lineup (Godot / glTF / three.js are all Y-up, right-handed). viser defaults
    # to +Z up (Blender/ROS), which tips the avatar onto its back. Both frames are
    # right-handed, so this is the whole correction — no mirroring needed.
    server.scene.set_up_direction("+y")
    for part, verts, faces in parts:
        server.scene.add_mesh_simple(
            f"/{name}/{part}",
            vertices=verts,
            faces=faces,
            color=(200, 184, 168),
        )
        print(f"[idtx]   + {part}: {len(verts)} verts, {len(faces)} tris")

    print("[idtx] viser is serving — open the URL above. Ctrl-C to stop.")
    while True:
        time.sleep(1)


if __name__ == "__main__":
    main()
