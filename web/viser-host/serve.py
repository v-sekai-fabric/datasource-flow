"""idtx-flow web host — viser.

The **web host** for idtx-flow: a Python viser server that runs `libidtx_core`
server-side natively (via ctypes, the SAME path the Blender host uses) and
streams geometry to a three.js client in the browser. No WebAssembly.

Parity with the other host adapters (Godot / Unity): the importer reads the
full avatar through the core — skeleton, per-vertex skinning, blend shapes,
material base color and normals — and renders skinned meshes (with bones) plus
live blend-shape sliders. Two GUI buttons drive the round-trip:

  * Import USD …    — upload a .usd/.usda/.usdc/.usdz; imported via
                      idtx_core_import_avatar_from_usd (the unified Y-up reader).
  * Export OpenUSD  — re-author the loaded avatar with
                      idtx_core_export_avatar_to_usd and download the .usda.

Run:  pixi run serve     (then open the printed http://localhost:8080 URL)

Copyright 2026 V-Sekai contributors.
SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
"""

from __future__ import annotations

import io
import sys
import tempfile
import threading
import time
from ctypes import POINTER, c_char_p, c_float, c_int32, c_ubyte, c_void_p
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
import trimesh
import viser
from PIL import Image

# web/viser-host -> web -> repo root
REPO = Path(__file__).resolve().parents[2]
USD_PATH = REPO / "build" / "miroir_from_unity.usda"

# Reuse the proven Blender ctypes loader: it finds libidtx_core, wires its
# OpenUSD dependency dirs, calls idtx_core_init(), and binds the export side +
# idtx_core_import_avatar_from_usd / idtx_avatar_destroy.
sys.path.insert(0, str(REPO / "openusd-fabric" / "blender"))
import idtx_core_ctypes as core  # noqa: E402


def _bind_reader(lib) -> None:
    """Declare the full read-side ABI (the loader only binds import/export).
    Every symbol already exists in idtx_core.h — the same ones the Godot and
    Unity importers consume, so the web host reaches geometry parity."""
    f4 = POINTER(c_float)
    i4 = POINTER(c_int32)
    sigs = {
        "idtx_avatar_get_name": ([c_void_p], c_char_p),
        "idtx_avatar_get_mesh_count": ([c_void_p], c_int32),
        "idtx_avatar_get_mesh": ([c_void_p, c_int32], c_void_p),
        "idtx_avatar_get_mesh_material": ([c_void_p, c_int32], c_int32),
        "idtx_avatar_get_skeleton": ([c_void_p], c_void_p),
        "idtx_avatar_get_material_count": ([c_void_p], c_int32),
        "idtx_avatar_get_material": ([c_void_p, c_int32], c_void_p),
        # mesh
        "idtx_mesh_get_name": ([c_void_p], c_char_p),
        "idtx_mesh_get_vertex_count": ([c_void_p], c_int32),
        "idtx_mesh_get_index_count": ([c_void_p], c_int32),
        "idtx_mesh_get_bones_per_vertex": ([c_void_p], c_int32),
        "idtx_mesh_get_positions": ([c_void_p, f4], None),
        "idtx_mesh_get_normals": ([c_void_p, f4], None),
        "idtx_mesh_get_uvs": ([c_void_p, f4], None),
        "idtx_mesh_get_colors": ([c_void_p, f4], None),
        "idtx_mesh_get_indices": ([c_void_p, i4], None),
        "idtx_mesh_get_bone_indices": ([c_void_p, i4], None),
        "idtx_mesh_get_weights": ([c_void_p, f4], None),
        "idtx_mesh_has_normals": ([c_void_p], c_int32),
        "idtx_mesh_has_uvs": ([c_void_p], c_int32),
        "idtx_mesh_has_colors": ([c_void_p], c_int32),
        "idtx_mesh_get_blendshape_count": ([c_void_p], c_int32),
        "idtx_mesh_get_blendshape_name": ([c_void_p, c_int32], c_char_p),
        "idtx_mesh_get_blendshape_weight": ([c_void_p, c_int32], c_float),
        "idtx_mesh_get_blendshape_position_deltas": ([c_void_p, c_int32, f4], None),
        # skeleton
        "idtx_skeleton_get_bone_count": ([c_void_p], c_int32),
        "idtx_skeleton_get_bone_name": ([c_void_p, c_int32], c_char_p),
        "idtx_skeleton_get_bone_parent": ([c_void_p, c_int32], c_int32),
        "idtx_skeleton_get_bone_bind": ([c_void_p, c_int32, f4], None),
        # material
        "idtx_material_get_base_color": ([c_void_p, f4], None),
        "idtx_material_get_base_color_texture": ([c_void_p], c_char_p),
        # decoded texture bytes (resolves .usdz package members too)
        "idtx_avatar_get_texture_count": ([c_void_p], c_int32),
        "idtx_avatar_get_texture_name": ([c_void_p, c_int32], c_char_p),
        "idtx_avatar_get_texture_byte_count": ([c_void_p, c_int32], c_int32),
        "idtx_avatar_get_texture_bytes": ([c_void_p, c_int32, POINTER(c_ubyte)], None),
    }
    for fn, (argtypes, restype) in sigs.items():
        getattr(lib, fn).argtypes = argtypes
        getattr(lib, fn).restype = restype


# --------------------------------------------------------------------------
# Core readback -> plain numpy structures.
# --------------------------------------------------------------------------

@dataclass
class MeshData:
    name: str
    verts: np.ndarray            # (V,3) f32, rest pose
    faces: np.ndarray            # (F,3) i32
    normals: np.ndarray | None   # (V,3) f32
    color: tuple[int, int, int]  # base color (0-255)
    skin: np.ndarray | None      # (V,B) f32 dense skin weights, or None
    uvs: np.ndarray | None = None       # (V,2) f32
    tex_path: str | None = None         # base-color texture file path
    blendshapes: list = field(default_factory=list)  # [(name, deltas (V,3))]


# Albedo textures, loaded once per path (RGB PIL images).
_TEXTURE_CACHE: dict[str, Image.Image | None] = {}
# Decoded texture bytes keyed by path, supplied by the core on import. This is
# how a .usdz-packed avatar's textures arrive — their paths are package members,
# not files PIL can open, so we decode the bytes the core resolved.
_TEXTURE_BYTES: dict[str, bytes] = {}


def _load_texture(path: str | None) -> Image.Image | None:
    if not path:
        return None
    if path not in _TEXTURE_CACHE:
        try:
            if path in _TEXTURE_BYTES:
                _TEXTURE_CACHE[path] = Image.open(io.BytesIO(_TEXTURE_BYTES[path])).convert("RGB")
            else:
                _TEXTURE_CACHE[path] = Image.open(path).convert("RGB")
        except Exception as exc:  # missing / unreadable -> fall back to flat color
            print(f"[idtx] texture load failed for {path}: {exc}")
            _TEXTURE_CACHE[path] = None
    return _TEXTURE_CACHE[path]


def _read_avatar_textures(lib, avatar) -> dict[str, bytes]:
    """Pull the core's decoded texture bytes (path -> bytes) off the avatar."""
    out: dict[str, bytes] = {}
    for i in range(lib.idtx_avatar_get_texture_count(avatar)):
        name = (lib.idtx_avatar_get_texture_name(avatar, i) or b"").decode("utf-8")
        nbytes = lib.idtx_avatar_get_texture_byte_count(avatar, i)
        if not name or nbytes <= 0:
            continue
        buf = (c_ubyte * nbytes)()
        lib.idtx_avatar_get_texture_bytes(avatar, i, buf)
        out[name] = bytes(buf)
    return out


def _textured_trimesh(md: "MeshData", verts: np.ndarray, image: Image.Image) -> trimesh.Trimesh:
    """Build a double-sided, albedo-textured trimesh for GLB rendering. Double
    sided so reversed winding never reads as 'inside out'; normals from the core
    drive the shading."""
    material = trimesh.visual.material.PBRMaterial(
        baseColorTexture=image,
        metallicFactor=0.0,
        roughnessFactor=1.0,
        doubleSided=True,
    )
    visual = trimesh.visual.TextureVisuals(uv=md.uvs, material=material, image=image)
    kwargs = {"vertices": verts, "faces": md.faces, "visual": visual, "process": False}
    if md.normals is not None:
        kwargs["vertex_normals"] = md.normals
    return trimesh.Trimesh(**kwargs)


@dataclass
class Bone:
    name: str
    parent: int
    wxyz: tuple[float, float, float, float]
    position: tuple[float, float, float]


def _mat_to_wxyz(m16: np.ndarray) -> tuple[float, float, float, float]:
    """Quaternion (w,x,y,z) from the rotation block of a row-vector float16."""
    R = np.array([[m16[0], m16[1], m16[2]],
                  [m16[4], m16[5], m16[6]],
                  [m16[8], m16[9], m16[10]]], dtype=np.float64)
    # Orthonormalise rows (strip any uniform scale) so the trace method is stable.
    for i in range(3):
        n = np.linalg.norm(R[i])
        if n > 1e-12:
            R[i] /= n
    t = R[0, 0] + R[1, 1] + R[2, 2]
    if t > 0:
        s = np.sqrt(t + 1.0) * 2
        w = 0.25 * s
        x = (R[2, 1] - R[1, 2]) / s
        y = (R[0, 2] - R[2, 0]) / s
        z = (R[1, 0] - R[0, 1]) / s
    elif R[0, 0] > R[1, 1] and R[0, 0] > R[2, 2]:
        s = np.sqrt(1.0 + R[0, 0] - R[1, 1] - R[2, 2]) * 2
        w = (R[2, 1] - R[1, 2]) / s
        x = 0.25 * s
        y = (R[0, 1] + R[1, 0]) / s
        z = (R[0, 2] + R[2, 0]) / s
    elif R[1, 1] > R[2, 2]:
        s = np.sqrt(1.0 + R[1, 1] - R[0, 0] - R[2, 2]) * 2
        w = (R[0, 2] - R[2, 0]) / s
        x = (R[0, 1] + R[1, 0]) / s
        y = 0.25 * s
        z = (R[1, 2] + R[2, 1]) / s
    else:
        s = np.sqrt(1.0 + R[2, 2] - R[0, 0] - R[1, 1]) * 2
        w = (R[1, 0] - R[0, 1]) / s
        x = (R[0, 2] + R[2, 0]) / s
        y = (R[1, 2] + R[2, 1]) / s
        z = 0.25 * s
    return (float(w), float(x), float(y), float(z))


def read_skeleton(lib, skel) -> list[Bone]:
    if not skel:
        return []
    bones = []
    for i in range(lib.idtx_skeleton_get_bone_count(skel)):
        name = (lib.idtx_skeleton_get_bone_name(skel, i) or f"bone_{i}".encode()).decode("utf-8")
        parent = lib.idtx_skeleton_get_bone_parent(skel, i)
        m = (c_float * 16)()
        lib.idtx_skeleton_get_bone_bind(skel, i, m)
        m = np.ctypeslib.as_array(m).copy()
        bones.append(Bone(name, parent, _mat_to_wxyz(m), (float(m[12]), float(m[13]), float(m[14]))))
    return bones


def read_mesh(lib, avatar, i: int, num_bones: int) -> MeshData | None:
    mesh = lib.idtx_avatar_get_mesh(avatar, i)
    vc = lib.idtx_mesh_get_vertex_count(mesh)
    ic = lib.idtx_mesh_get_index_count(mesh)
    if vc <= 0 or ic <= 0:
        return None
    name = (lib.idtx_mesh_get_name(mesh) or f"mesh_{i}".encode()).decode("utf-8")

    pos = (c_float * (vc * 3))()
    idx = (c_int32 * ic)()
    lib.idtx_mesh_get_positions(mesh, pos)
    lib.idtx_mesh_get_indices(mesh, idx)
    verts = np.ctypeslib.as_array(pos).reshape(-1, 3).astype(np.float32).copy()
    faces = np.ctypeslib.as_array(idx).reshape(-1, 3).astype(np.int32).copy()

    normals = None
    if lib.idtx_mesh_has_normals(mesh):
        nrm = (c_float * (vc * 3))()
        lib.idtx_mesh_get_normals(mesh, nrm)
        normals = np.ctypeslib.as_array(nrm).reshape(-1, 3).astype(np.float32).copy()

    uvs = None
    if lib.idtx_mesh_has_uvs(mesh):
        uv = (c_float * (vc * 2))()
        lib.idtx_mesh_get_uvs(mesh, uv)
        uvs = np.ctypeslib.as_array(uv).reshape(-1, 2).astype(np.float32).copy()

    # Base color + albedo texture from the bound material.
    color = (200, 184, 168)
    tex_path = None
    mat_idx = lib.idtx_avatar_get_mesh_material(avatar, i)
    if mat_idx >= 0:
        mat = lib.idtx_avatar_get_material(avatar, mat_idx)
        if mat:
            rgba = (c_float * 4)()
            lib.idtx_material_get_base_color(mat, rgba)
            color = tuple(int(max(0.0, min(1.0, rgba[k])) * 255) for k in range(3))
            t = lib.idtx_material_get_base_color_texture(mat)
            tex_path = t.decode("utf-8") if t else None

    # Dense skin weights (V, num_bones) from the 4-per-vertex sparse channels.
    skin = None
    bpv = lib.idtx_mesh_get_bones_per_vertex(mesh)
    if bpv > 0 and num_bones > 0:
        bi = (c_int32 * (vc * bpv))()
        bw = (c_float * (vc * bpv))()
        lib.idtx_mesh_get_bone_indices(mesh, bi)
        lib.idtx_mesh_get_weights(mesh, bw)
        bidx = np.ctypeslib.as_array(bi).reshape(vc, bpv)
        bwt = np.ctypeslib.as_array(bw).reshape(vc, bpv).astype(np.float32)
        skin = np.zeros((vc, num_bones), dtype=np.float32)
        rows = np.arange(vc)[:, None]
        np.clip(bidx, 0, num_bones - 1, out=bidx)
        skin[rows, bidx] = bwt

    # Blend shapes (position deltas only; viser morphs CPU-side).
    blendshapes = []
    for b in range(lib.idtx_mesh_get_blendshape_count(mesh)):
        bname = (lib.idtx_mesh_get_blendshape_name(mesh, b) or f"shape_{b}".encode()).decode("utf-8")
        d = (c_float * (vc * 3))()
        lib.idtx_mesh_get_blendshape_position_deltas(mesh, b, d)
        deltas = np.ctypeslib.as_array(d).reshape(-1, 3).astype(np.float32).copy()
        blendshapes.append((bname, deltas))

    return MeshData(name, verts, faces, normals, color, skin,
                    uvs=uvs, tex_path=tex_path, blendshapes=blendshapes)


# --------------------------------------------------------------------------
# Host: owns the live avatar + scene nodes; drives import/export/blendshapes.
# --------------------------------------------------------------------------

class Host:
    def __init__(self, lib, server: viser.ViserServer) -> None:
        self.lib = lib
        self.server = server
        self._lock = threading.Lock()
        self._avatar = None
        self._nodes: list = []
        self._meshes: list[MeshData] = []
        self._weights: dict[str, float] = {}   # blendshape name -> weight
        self.name = "avatar"

    def load(self, usd_path: Path):
        avatar = self.lib.idtx_core_import_avatar_from_usd(str(usd_path).encode("utf-8"))
        if not avatar:
            raise RuntimeError(f"libidtx_core failed to import {usd_path.name}")
        name = (self.lib.idtx_avatar_get_name(avatar) or b"avatar").decode("utf-8")
        # Refresh the decoded-texture table (handles .usdz package members) and
        # drop any cached PIL images so they re-decode against the new source.
        _TEXTURE_BYTES.clear()
        _TEXTURE_BYTES.update(_read_avatar_textures(self.lib, avatar))
        _TEXTURE_CACHE.clear()
        skel = self.lib.idtx_avatar_get_skeleton(avatar)
        bones = read_skeleton(self.lib, skel)
        nb = len(bones)
        meshes = []
        for i in range(self.lib.idtx_avatar_get_mesh_count(avatar)):
            md = read_mesh(self.lib, avatar, i, nb)
            if md is not None:
                meshes.append(md)

        with self._lock:
            if self._avatar is not None:
                self.lib.idtx_avatar_destroy(self._avatar)
            self._avatar = avatar
            self.name = name
            self._meshes = meshes
            self._bones = bones
            # collect the blend-shape names across meshes for the GUI
            self._weights = {bn: 0.0 for md in meshes for (bn, _) in md.blendshapes}
            self._rebuild_scene()
            tris = sum(len(m.faces) for m in meshes)
        return name, len(meshes), tris, nb, len(self._weights)

    def _rebuild_scene(self) -> None:
        """(Re)create scene nodes from current meshes + blend-shape weights.
        Caller holds the lock."""
        for node in self._nodes:
            node.remove()
        self._nodes.clear()
        bones = getattr(self, "_bones", [])
        bone_wxyzs = [b.wxyz for b in bones]
        bone_pos = [b.position for b in bones]
        for md in self._meshes:
            verts = md.verts
            if md.blendshapes:  # apply morph deltas CPU-side
                verts = verts.copy()
                for bn, deltas in md.blendshapes:
                    w = self._weights.get(bn, 0.0)
                    if w != 0.0:
                        verts = verts + w * deltas
            path = f"/{self.name}/{md.name}"
            image = _load_texture(md.tex_path)
            if image is not None and md.uvs is not None:
                # Textured render via GLB: shows the avatar's actual albedo and
                # is double-sided, so reversed winding never reads "inside out".
                h = self.server.scene.add_mesh_trimesh(path, _textured_trimesh(md, verts, image))
            elif md.skin is not None and bones:
                h = self.server.scene.add_mesh_skinned(
                    path, vertices=verts, faces=md.faces,
                    bone_wxyzs=bone_wxyzs, bone_positions=bone_pos,
                    skin_weights=md.skin, color=md.color, material="toon3",
                    side="double",
                )
            else:
                h = self.server.scene.add_mesh_simple(
                    path, vertices=verts, faces=md.faces, color=md.color,
                    side="double",
                )
            self._nodes.append(h)

    def set_blendshape(self, name: str, weight: float) -> None:
        with self._lock:
            self._weights[name] = weight
            self._rebuild_scene()

    @property
    def blendshape_names(self) -> list[str]:
        return list(self._weights.keys())

    def export_usda(self) -> tuple[str, bytes]:
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
    # Whole lineup is Y-up right-handed (USD upAxis=Y, Godot, glTF, three.js);
    # viser defaults to +Z up, so correct it once. No mirroring (both RH).
    server.scene.set_up_direction("+y")

    host = Host(lib, server)
    status = server.gui.add_text("Status", initial_value="loading…", disabled=True)

    import_btn = server.gui.add_upload_button(
        "Import USD …", icon=viser.Icon.UPLOAD, mime_type=".usd,.usda,.usdc,.usdz")
    export_btn = server.gui.add_button("Export OpenUSD", icon=viser.Icon.DOWNLOAD)
    shapes_folder = server.gui.add_folder("Blend shapes")

    def rebuild_blendshape_gui() -> None:
        shapes_folder.remove()
        new_folder = server.gui.add_folder("Blend shapes")
        with new_folder:
            names = host.blendshape_names
            if not names:
                server.gui.add_text("(none)", initial_value="", disabled=True)
            for bn in names[:64]:  # cap the panel for very large rigs
                sl = server.gui.add_slider(bn, min=0.0, max=1.0, step=0.01, initial_value=0.0)

                @sl.on_update
                def _(_e, _name=bn, _slider=sl) -> None:
                    host.set_blendshape(_name, _slider.value)
        return new_folder

    def do_load(path: Path, label: str) -> None:
        nonlocal shapes_folder
        name, nmesh, tris, nb, nshapes = host.load(path)
        status.value = f"{label}: {name} — {nmesh} mesh, {tris} tris, {nb} bones, {nshapes} shapes"
        shapes_folder = rebuild_blendshape_gui()
        print(f"[idtx] {status.value}")

    @import_btn.on_upload
    def _(_event) -> None:
        up = import_btn.value
        suffix = Path(up.name).suffix or ".usda"
        with tempfile.NamedTemporaryFile(suffix=suffix, delete=False) as tmp:
            tmp.write(up.content)
            tmp_path = Path(tmp.name)
        try:
            do_load(tmp_path, f"imported {up.name}")
        except Exception as exc:
            status.value = f"import failed: {exc}"
            print(f"[idtx] import failed: {exc}")
        finally:
            tmp_path.unlink(missing_ok=True)

    @export_btn.on_click
    def _(event) -> None:
        try:
            filename, data = host.export_usda()
            event.client.send_file_download(filename, data)
            status.value = f"exported {filename} ({len(data)} bytes)"
            print(f"[idtx] {status.value}")
        except Exception as exc:
            status.value = f"export failed: {exc}"
            print(f"[idtx] export failed: {exc}")

    if USD_PATH.exists():
        try:
            do_load(USD_PATH, "loaded default")
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
