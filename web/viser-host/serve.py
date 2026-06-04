"""idtx-flow web host — proof-of-concept.

Reads an avatar USD through libidtx_core (server-side, via ctypes — the SAME
path the Blender host uses) and renders its meshes in the browser with viser.
No WebAssembly: the core runs natively in this Python process and viser streams
geometry to a three.js client over websockets.

The GUI panel exposes two round-trip buttons backed by the core:
  * Import USD …    — upload a .usd/.usda/.usdc/.usdz from the browser; it is
                      imported via idtx_core_import_avatar_from_usd and replaces
                      the scene.
  * Export OpenUSD  — re-author the loaded avatar with
                      idtx_core_export_avatar_to_usd and download the .usda.

Run:  pixi run serve     (then open the printed http://localhost:8080 URL)

Copyright 2026 V-Sekai contributors.
SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
"""

from __future__ import annotations

import sys
import tempfile
import threading
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


def _avatar_meshes(lib, avatar):
    """avatar handle -> [(name, verts Nx3 float32, faces Mx3 int32)]."""
    parts = []
    for i in range(lib.idtx_avatar_get_mesh_count(avatar)):
        mesh = lib.idtx_avatar_get_mesh(avatar, i)
        vc = lib.idtx_mesh_get_vertex_count(mesh)
        ic = lib.idtx_mesh_get_index_count(mesh)
        if vc <= 0 or ic <= 0:
            continue
        pos = (c_float * (vc * 3))()
        idx = (c_int32 * ic)()
        lib.idtx_mesh_get_positions(mesh, pos)
        lib.idtx_mesh_get_indices(mesh, idx)
        verts = np.ctypeslib.as_array(pos).reshape(-1, 3).astype(np.float32).copy()
        faces = np.ctypeslib.as_array(idx).reshape(-1, 3).astype(np.int32).copy()
        part = (lib.idtx_mesh_get_name(mesh) or f"mesh_{i}".encode()).decode("utf-8")
        parts.append((part, verts, faces))
    return parts


class Host:
    """Owns the live avatar handle + its rendered scene nodes, so Import can
    swap them and Export can re-author the currently loaded avatar."""

    def __init__(self, lib, server: viser.ViserServer) -> None:
        self.lib = lib
        self.server = server
        self._lock = threading.Lock()
        self._avatar = None          # live idtx_avatar_t* (kept for export)
        self._nodes: list = []       # viser scene handles to remove on reload
        self.name = "avatar"

    def load(self, usd_path: Path) -> str:
        """Import a USD through the core and (re)build the scene. Returns a
        human-readable status string; raises on import failure."""
        avatar = self.lib.idtx_core_import_avatar_from_usd(
            str(usd_path).encode("utf-8"))
        if not avatar:
            raise RuntimeError(f"libidtx_core failed to import {usd_path.name}")
        name = (self.lib.idtx_avatar_get_name(avatar) or b"avatar").decode("utf-8")
        parts = _avatar_meshes(self.lib, avatar)

        with self._lock:
            # Swap in the new avatar; free the previous one + its scene nodes.
            for node in self._nodes:
                node.remove()
            self._nodes.clear()
            if self._avatar is not None:
                self.lib.idtx_avatar_destroy(self._avatar)
            self._avatar = avatar
            self.name = name

            tris = 0
            for part, verts, faces in parts:
                handle = self.server.scene.add_mesh_simple(
                    f"/{name}/{part}",
                    vertices=verts,
                    faces=faces,
                    color=(200, 184, 168),
                )
                self._nodes.append(handle)
                tris += len(faces)
        return f"{name}: {len(parts)} mesh(es), {tris} tris"

    def export_usda(self) -> tuple[str, bytes]:
        """Re-author the live avatar to a .usda and return (filename, bytes)."""
        with self._lock:
            if self._avatar is None:
                raise RuntimeError("nothing loaded to export")
            name = self.name
            with tempfile.TemporaryDirectory() as tmp:
                out = Path(tmp) / f"{name}.usda"
                rc = self.lib.idtx_core_export_avatar_to_usd(
                    self._avatar, str(out).encode("utf-8"))
                if rc != 0 or not out.exists():
                    raise RuntimeError(f"export failed (rc={rc})")
                return f"{name}.usda", out.read_bytes()


def main() -> None:
    lib = core.load(REPO)
    _bind_reader(lib)
    version = (lib.idtx_core_version() or b"?").decode("utf-8")
    print(f"[idtx] libidtx_core {version}")

    server = viser.ViserServer()
    # The USD declares upAxis = "Y" (metersPerUnit = 1), matching the rest of the
    # lineup (Godot / glTF / three.js are all Y-up, right-handed). viser defaults
    # to +Z up (Blender/ROS), which tips the avatar onto its back. Both frames are
    # right-handed, so this is the whole correction — no mirroring needed.
    server.scene.set_up_direction("+y")

    host = Host(lib, server)
    status = server.gui.add_text("Status", initial_value="loading…", disabled=True)

    # --- Import: upload a USD from the browser and render it through the core ---
    import_btn = server.gui.add_upload_button(
        "Import USD …",
        icon=viser.Icon.UPLOAD,
        mime_type=".usd,.usda,.usdc,.usdz",
    )

    @import_btn.on_upload
    def _(_event) -> None:
        uploaded = import_btn.value
        suffix = Path(uploaded.name).suffix or ".usda"
        with tempfile.NamedTemporaryFile(suffix=suffix, delete=False) as tmp:
            tmp.write(uploaded.content)
            tmp_path = Path(tmp.name)
        try:
            msg = host.load(tmp_path)
            status.value = f"imported {uploaded.name} → {msg}"
            print(f"[idtx] imported {uploaded.name}: {msg}")
        except Exception as exc:  # surface to the panel instead of crashing
            status.value = f"import failed: {exc}"
            print(f"[idtx] import failed: {exc}")
        finally:
            tmp_path.unlink(missing_ok=True)

    # --- Export: re-author the loaded avatar and download the .usda ----------
    export_btn = server.gui.add_button("Export OpenUSD", icon=viser.Icon.DOWNLOAD)

    @export_btn.on_click
    def _(event) -> None:
        try:
            filename, data = host.export_usda()
            event.client.send_file_download(filename, data)
            status.value = f"exported {filename} ({len(data)} bytes)"
            print(f"[idtx] exported {filename}: {len(data)} bytes")
        except Exception as exc:
            status.value = f"export failed: {exc}"
            print(f"[idtx] export failed: {exc}")

    # Seed the scene with the default avatar if it is present.
    if USD_PATH.exists():
        try:
            status.value = host.load(USD_PATH)
            print(f"[idtx] loaded default {USD_PATH.name}: {status.value}")
        except Exception as exc:
            status.value = f"default load failed: {exc}"
            print(f"[idtx] {status.value}")
    else:
        status.value = "no default avatar — use Import USD …"

    print("[idtx] viser is serving — open the URL above. Ctrl-C to stop.")
    while True:
        time.sleep(1)


if __name__ == "__main__":
    main()
