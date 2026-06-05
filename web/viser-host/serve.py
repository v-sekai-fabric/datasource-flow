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
import os
import sys
import tempfile
import threading
import time
from ctypes import POINTER, c_char_p, c_double, c_float, c_int32, c_ubyte, c_void_p
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
import trimesh
import viser
from PIL import Image

# web/viser-host -> web -> repo root
REPO = Path(__file__).resolve().parents[2]
# Default avatar to auto-load; override with IDTX_VISER_USD for ad-hoc test files.
USD_PATH = Path(os.environ.get("IDTX_VISER_USD", str(REPO / "build" / "miroir_from_unity.usda")))

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
        # geom subsets (UsdGeomSubset): per-material face ranges within one mesh
        "idtx_mesh_get_subset_count": ([c_void_p], c_int32),
        "idtx_mesh_get_subset": ([c_void_p, c_int32, i4, i4, i4], None),
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
        # scene + skeletal animation (the avatar API doesn't carry the clip, so we
        # re-open the same file as a scene — USD caches the layer, so it's cheap —
        # and read the skeleton node's animation tracks for timeline playback).
        "idtx_core_import_scene_from_usd": ([c_char_p], c_void_p),
        "idtx_core_scene_destroy": ([c_void_p], None),
        "idtx_scene_get_node_count": ([c_void_p], c_int32),
        "idtx_scene_get_node": ([c_void_p, c_int32], c_void_p),
        "idtx_node_get_skeleton": ([c_void_p], c_void_p),
        "idtx_node_get_animation": ([c_void_p], c_void_p),
        "idtx_anim_get_length": ([c_void_p], c_float),
        "idtx_anim_get_track_count": ([c_void_p], c_int32),
        "idtx_anim_track_get_bone_name": ([c_void_p, c_int32], c_char_p),
        "idtx_anim_track_get_type": ([c_void_p, c_int32], c_int32),
        "idtx_anim_track_get_key_count": ([c_void_p, c_int32], c_int32),
        "idtx_anim_track_get_key_time": ([c_void_p, c_int32, c_int32], c_double),
        "idtx_anim_track_get_key_vec3": ([c_void_p, c_int32, c_int32, f4], None),
        "idtx_anim_track_get_key_quat": ([c_void_p, c_int32, c_int32, f4], None),
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
    # UsdGeomSubset material ranges: [(face_start, face_count, color, tex_path)].
    # Empty/one => single-material mesh (use `color`/`tex_path`).
    subsets: list = field(default_factory=list)


def _material_color_tex(lib, avatar, mat_idx: int):
    """(base color 0-255 RGB tuple, texture path or None) for an avatar material."""
    color = (200, 184, 168)
    tex_path = None
    if mat_idx is not None and mat_idx >= 0:
        mat = lib.idtx_avatar_get_material(avatar, mat_idx)
        if mat:
            rgba = (c_float * 4)()
            lib.idtx_material_get_base_color(mat, rgba)
            color = tuple(int(max(0.0, min(1.0, rgba[k])) * 255) for k in range(3))
            t = lib.idtx_material_get_base_color_texture(mat)
            tex_path = t.decode("utf-8") if t else None
    return color, tex_path


# --------------------------------------------------------------------------
# Skeletal animation: read the clip + a forward-kinematics sampler. Playback
# updates ONLY the per-bone transforms (never re-sends geometry), so a frame is
# a handful of small transform messages, not the whole scene.
# --------------------------------------------------------------------------

def _quat_xyzw_to_mat(q):
    x, y, z, w = q
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w)],
        [2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)],
    ], np.float64)


def _mat_to_quat_wxyz(R):
    t = R[0, 0] + R[1, 1] + R[2, 2]
    if t > 0:
        s = np.sqrt(t + 1.0) * 2
        w, x, y, z = 0.25 * s, (R[2, 1] - R[1, 2]) / s, (R[0, 2] - R[2, 0]) / s, (R[1, 0] - R[0, 1]) / s
    elif R[0, 0] > R[1, 1] and R[0, 0] > R[2, 2]:
        s = np.sqrt(1.0 + R[0, 0] - R[1, 1] - R[2, 2]) * 2
        w, x, y, z = (R[2, 1] - R[1, 2]) / s, 0.25 * s, (R[0, 1] + R[1, 0]) / s, (R[0, 2] + R[2, 0]) / s
    elif R[1, 1] > R[2, 2]:
        s = np.sqrt(1.0 + R[1, 1] - R[0, 0] - R[2, 2]) * 2
        w, x, y, z = (R[0, 2] - R[2, 0]) / s, (R[0, 1] + R[1, 0]) / s, 0.25 * s, (R[1, 2] + R[2, 1]) / s
    else:
        s = np.sqrt(1.0 + R[2, 2] - R[0, 0] - R[1, 1]) * 2
        w, x, y, z = (R[1, 0] - R[0, 1]) / s, (R[0, 2] + R[2, 0]) / s, (R[1, 2] + R[2, 1]) / s, 0.25 * s
    q = np.array([w, x, y, z], np.float64)
    return q / np.linalg.norm(q)


def _slerp(q0, q1, u):  # xyzw
    d = float(np.dot(q0, q1))
    if d < 0:
        q1 = -q1
        d = -d
    if d > 0.9995:
        r = q0 + u * (q1 - q0)
        return r / np.linalg.norm(r)
    th = np.arccos(d)
    s = np.sin(th)
    return (np.sin((1 - u) * th) / s) * q0 + (np.sin(u * th) / s) * q1


@dataclass
class SkeletalClip:
    length: float
    # bone name -> {track_type: (times (K,), values (K,3|4))}; type 0=T,1=R(xyzw),2=S
    tracks: dict


def read_animation(lib, usd_path) -> "SkeletalClip | None":
    """Re-open the file as a scene and read the skeleton node's animation clip.
    The avatar API doesn't carry it; the layer is already cached so this is cheap."""
    scene = lib.idtx_core_import_scene_from_usd(str(usd_path).encode("utf-8"))
    if not scene:
        return None
    try:
        for i in range(lib.idtx_scene_get_node_count(scene)):
            node = lib.idtx_scene_get_node(scene, i)
            if not lib.idtx_node_get_skeleton(node):
                continue
            anim = lib.idtx_node_get_animation(node)
            if not anim:
                continue
            tracks: dict = {}
            for t in range(lib.idtx_anim_get_track_count(anim)):
                bone = (lib.idtx_anim_track_get_bone_name(anim, t) or b"").decode("utf-8")
                typ = lib.idtx_anim_track_get_type(anim, t)
                kc = lib.idtx_anim_track_get_key_count(anim, t)
                if kc == 0:
                    continue
                times = np.empty(kc, np.float64)
                vals = np.empty((kc, 4 if typ == 1 else 3), np.float64)
                for k in range(kc):
                    times[k] = lib.idtx_anim_track_get_key_time(anim, t, k)
                    if typ == 1:
                        q = (c_float * 4)()
                        lib.idtx_anim_track_get_key_quat(anim, t, k, q)
                        vals[k] = (q[0], q[1], q[2], q[3])
                    else:
                        v = (c_float * 3)()
                        lib.idtx_anim_track_get_key_vec3(anim, t, k, v)
                        vals[k] = (v[0], v[1], v[2])
                tracks.setdefault(bone, {})[typ] = (times, vals)
            if tracks:
                return SkeletalClip(float(lib.idtx_anim_get_length(anim)), tracks)
    finally:
        lib.idtx_core_scene_destroy(scene)
    return None


class AnimRig:
    """Forward-kinematics sampler. Given the bones (world bind poses + parents)
    and a clip, sample(t) returns each bone's world (wxyz, position) for viser."""

    def __init__(self, bones, clip: SkeletalClip):
        self.clip = clip
        self.parents = [b.parent for b in bones]
        self.names = [b.name for b in bones]
        # World bind matrices, and rest LOCAL T/R(xyzw)/S decomposed from them.
        self.world_bind = []
        self.rest_T, self.rest_R, self.rest_S = [], [], []
        for b in bones:
            m = np.eye(4)
            m[:3, :3] = _quat_xyzw_to_mat((b.wxyz[1], b.wxyz[2], b.wxyz[3], b.wxyz[0]))
            m[:3, 3] = b.position
            self.world_bind.append(m)
        for i, b in enumerate(bones):
            p = self.parents[i]
            local = np.linalg.inv(self.world_bind[p]) @ self.world_bind[i] if p >= 0 else self.world_bind[i]
            R = local[:3, :3].copy()
            s = np.linalg.norm(R, axis=0)
            s[s == 0] = 1.0
            self.rest_T.append(local[:3, 3].copy())
            self.rest_R.append(_mat_to_quat_wxyz(R / s))  # wxyz
            self.rest_S.append(s)

    @staticmethod
    def _sample(track, t):
        times, vals = track
        if t <= times[0]:
            return vals[0]
        if t >= times[-1]:
            return vals[-1]
        j = int(np.searchsorted(times, t))
        u = (t - times[j - 1]) / (times[j] - times[j - 1])
        return vals[j - 1], vals[j], u

    def sample(self, t):
        out = [None] * len(self.names)
        world = [None] * len(self.names)
        for i in range(len(self.names)):
            ch = self.clip.tracks.get(self.names[i], {})
            # translation
            if 0 in ch:
                r = self._sample(ch[0], t)
                T = r if isinstance(r, np.ndarray) else r[0] + (r[1] - r[0]) * r[2]
            else:
                T = self.rest_T[i]
            # rotation (wxyz rest, xyzw track)
            if 1 in ch:
                r = self._sample(ch[1], t)
                q = r if isinstance(r, np.ndarray) else _slerp(r[0], r[1], r[2])  # xyzw
            else:
                w = self.rest_R[i]
                q = np.array([w[1], w[2], w[3], w[0]])  # wxyz->xyzw
            # scale
            if 2 in ch:
                r = self._sample(ch[2], t)
                S = r if isinstance(r, np.ndarray) else r[0] + (r[1] - r[0]) * r[2]
            else:
                S = self.rest_S[i]
            local = np.eye(4)
            local[:3, :3] = _quat_xyzw_to_mat(q) * S
            local[:3, 3] = T
            p = self.parents[i]
            world[i] = world[p] @ local if (p >= 0 and world[p] is not None) else local
            R = world[i][:3, :3].copy()
            sc = np.linalg.norm(R, axis=0)
            sc[sc == 0] = 1.0
            out[i] = (_mat_to_quat_wxyz(R / sc), world[i][:3, 3].copy())
        return out


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


def _textured_trimesh(md: "MeshData", verts: np.ndarray, faces: np.ndarray,
                      image: Image.Image) -> trimesh.Trimesh:
    """Build a double-sided, albedo-textured trimesh for GLB rendering. Double
    sided so reversed winding never reads as 'inside out'; normals from the core
    drive the shading. `faces` may be a subset of the mesh's triangles."""
    material = trimesh.visual.material.PBRMaterial(
        baseColorTexture=image,
        metallicFactor=0.0,
        roughnessFactor=1.0,
        doubleSided=True,
    )
    # The core hands us canonical glTF UVs (top-left origin, what a direct
    # GLTFLoader expects). trimesh's TextureVisuals uses the bottom-left (OpenGL)
    # convention and re-flips V when it exports the GLB, so feeding glTF UVs
    # straight in renders every texture upside-down. Flip V once here to cancel
    # trimesh's flip.
    uv = md.uvs.copy()
    uv[:, 1] = 1.0 - uv[:, 1]
    visual = trimesh.visual.TextureVisuals(uv=uv, material=material, image=image)
    kwargs = {"vertices": verts, "faces": faces, "visual": visual, "process": False}
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

    # Base color + albedo texture from the mesh's primary bound material.
    color, tex_path = _material_color_tex(lib, avatar, lib.idtx_avatar_get_mesh_material(avatar, i))

    # Geom subsets (UsdGeomSubset): per-material face ranges within this mesh.
    # Each entry is (face_start, face_count, color, tex_path) -- face indices into
    # `faces`, so a multi-material mesh renders one piece per material.
    subsets = []
    sc = lib.idtx_mesh_get_subset_count(mesh)
    if sc > 1:
        for s in range(sc):
            smat = c_int32(); soff = c_int32(); scnt = c_int32()
            lib.idtx_mesh_get_subset(mesh, s, smat, soff, scnt)
            scol, stex = _material_color_tex(lib, avatar, smat.value)
            subsets.append((soff.value // 3, scnt.value // 3, scol, stex))

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
                    uvs=uvs, tex_path=tex_path, blendshapes=blendshapes, subsets=subsets)


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
        self.center = np.zeros(3, np.float32)  # avatar AABB centre (Y-up frame)
        self.radius = 1.0                       # half-diagonal, for camera framing
        self._rig: AnimRig | None = None        # FK sampler for the skeletal clip
        self.anim_length = 0.0                  # clip length in seconds (0 = none)
        self._skinned: list = []                # skinned-mesh handles to drive

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

        # Skeletal animation clip (read via the scene API; the avatar doesn't carry it).
        clip = read_animation(self.lib, usd_path) if nb > 0 else None
        rig = AnimRig(bones, clip) if clip else None

        with self._lock:
            if self._avatar is not None:
                self.lib.idtx_avatar_destroy(self._avatar)
            self._avatar = avatar
            self.name = name
            self._meshes = meshes
            self._bones = bones
            self._rig = rig
            self.anim_length = clip.length if clip else 0.0
            # collect the blend-shape names across meshes for the GUI
            self._weights = {bn: 0.0 for md in meshes for (bn, _) in md.blendshapes}
            # AABB over all verts for camera framing.
            if meshes:
                allv = np.concatenate([m.verts for m in meshes], axis=0)
                lo, hi = allv.min(axis=0), allv.max(axis=0)
                self.center = ((lo + hi) * 0.5).astype(np.float32)
                self.radius = max(float(np.linalg.norm(hi - lo)) * 0.5, 1e-3)
            self._rebuild_scene()
            tris = sum(len(m.faces) for m in meshes)
        return name, len(meshes), tris, nb, len(self._weights)

    def _rebuild_scene(self) -> None:
        """(Re)create scene nodes from current meshes + blend-shape weights.
        Caller holds the lock."""
        # Loading a new USD REPLACES the scene. Drop the whole avatar subtree by
        # its fixed root so a freshly loaded file never overlaps the previous
        # avatar (which read as "scrambled" — two avatars' meshes interpenetrating).
        # Removing the root group clears every child regardless of the prior
        # avatar's name; the per-node handles are a fallback for older viser.
        try:
            self.server.scene.remove_by_name("/avatar")
        except Exception:
            for node in self._nodes:
                node.remove()
        self._nodes.clear()
        self._skinned.clear()
        bones = getattr(self, "_bones", [])
        bone_wxyzs = [b.wxyz for b in bones]
        bone_pos = [b.position for b in bones]
        # viser's add_mesh_skinned resolves exactly 4 influences per vertex, so a
        # rig with fewer than 4 bones (e.g. a 2-bone test) needs weightless
        # identity padding bones; the dense skin is padded to match.
        skin_pad = max(0, 4 - len(bones))
        if skin_pad:
            bone_wxyzs = bone_wxyzs + [np.array([1.0, 0.0, 0.0, 0.0])] * skin_pad
            bone_pos = bone_pos + [np.zeros(3)] * skin_pad
        has_anim = self._rig is not None

        def add_skinned(path, md, verts, faces, color):
            sk = md.skin
            if skin_pad:
                sk = np.concatenate([sk, np.zeros((sk.shape[0], skin_pad), np.float32)], axis=1)
            handle = self.server.scene.add_mesh_skinned(
                path, vertices=verts, faces=faces,
                bone_wxyzs=bone_wxyzs, bone_positions=bone_pos,
                skin_weights=sk, color=color, material="toon3", side="double")
            self._skinned.append(handle)
            return handle

        def add_piece(path, md, verts, faces, color, tex_path):
            """Render one mesh piece (whole mesh or one UsdGeomSubset range)."""
            skinned = md.skin is not None and bones
            # viser's GPU-skinning primitive (add_mesh_skinned) carries only a flat
            # colour — texturing a skinned mesh needs a glTF path we intentionally
            # avoid (OpenUSD-native). So when the avatar has an animation clip, skin
            # it (it plays, flat-coloured); only a STATIC skinned mesh keeps its
            # albedo via the textured trimesh path.
            if skinned and has_anim:
                return add_skinned(path, md, verts, faces, color)
            image = _load_texture(tex_path)
            if image is not None and md.uvs is not None:
                # Textured: actual albedo, double-sided (winding-proof).
                return self.server.scene.add_mesh_trimesh(
                    path, _textured_trimesh(md, verts, faces, image))
            if skinned:
                return add_skinned(path, md, verts, faces, color)
            return self.server.scene.add_mesh_simple(
                path, vertices=verts, faces=faces, color=color, side="double")

        for md in self._meshes:
            verts = md.verts
            if md.blendshapes:  # apply morph deltas CPU-side
                verts = verts.copy()
                for bn, deltas in md.blendshapes:
                    w = self._weights.get(bn, 0.0)
                    if w != 0.0:
                        verts = verts + w * deltas
            path = f"/avatar/{md.name}"
            if len(md.subsets) > 1:
                # Multi-material mesh (UsdGeomSubset): one piece per material range.
                for si, (fstart, fcount, scol, stex) in enumerate(md.subsets):
                    sub_faces = md.faces[fstart:fstart + fcount]
                    if len(sub_faces) == 0:
                        continue
                    self._nodes.append(
                        add_piece(f"{path}/mat{si}", md, verts, sub_faces, scol, stex))
            else:
                self._nodes.append(add_piece(path, md, verts, md.faces, md.color, md.tex_path))

    def set_anim_time(self, t: float) -> None:
        """Pose the skeleton at time `t`. Updates ONLY the per-bone transforms on
        each skinned mesh — a frame is a handful of small transform messages, the
        geometry is never re-sent."""
        with self._lock:
            rig = self._rig
            handles = list(self._skinned)
        if rig is None or not handles:
            return
        poses = rig.sample(float(t))
        for handle in handles:
            hb = handle.bones
            for i in range(min(len(hb), len(poses))):
                wxyz, pos = poses[i]
                hb[i].wxyz = wxyz
                hb[i].position = pos

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

    # Frame the avatar for every client that connects: a front, slightly raised
    # Y-up view sized to the avatar's AABB, so it isn't tiny/off-centre on open.
    @server.on_client_connect
    def _frame(client: viser.ClientHandle) -> None:
        c = host.center
        r = host.radius
        client.camera.up_direction = (0.0, 1.0, 0.0)
        client.camera.position = (float(c[0]), float(c[1]) + r * 0.25, float(c[2]) + r * 2.6)
        client.camera.look_at = (float(c[0]), float(c[1]), float(c[2]))
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

    # Animation controls. Rebuilt per load so the time slider spans the clip; the
    # playback thread advances time and drives only the bones (no geometry re-send).
    anim_ctl: dict = {"folder": server.gui.add_folder("Animation"), "play": None, "time": None}

    def rebuild_anim_gui() -> None:
        anim_ctl["folder"].remove()
        folder = server.gui.add_folder("Animation")
        anim_ctl["folder"] = folder
        with folder:
            if host.anim_length <= 0.0:
                server.gui.add_text("(no animation)", initial_value="", disabled=True)
                anim_ctl["play"] = None
                anim_ctl["time"] = None
                return
            anim_ctl["play"] = server.gui.add_checkbox("Play", initial_value=True)
            ts = server.gui.add_slider(
                "Time (s)", min=0.0, max=max(round(host.anim_length, 3), 0.01),
                step=0.01, initial_value=0.0)
            anim_ctl["time"] = ts

            @ts.on_update
            def _(_e, _ts=ts) -> None:
                host.set_anim_time(_ts.value)

    def do_load(path: Path, label: str) -> None:
        nonlocal shapes_folder
        name, nmesh, tris, nb, nshapes = host.load(path)
        anim = f", anim {host.anim_length:.1f}s" if host.anim_length > 0 else ""
        status.value = f"{label}: {name} — {nmesh} mesh, {tris} tris, {nb} bones, {nshapes} shapes{anim}"
        shapes_folder = rebuild_blendshape_gui()
        rebuild_anim_gui()
        print(f"[idtx] {status.value}")

    def _playback() -> None:
        dt = 1.0 / 30.0
        while True:
            time.sleep(dt)
            play, ts = anim_ctl["play"], anim_ctl["time"]
            if ts is None or play is None or not play.value or host.anim_length <= 0.0:
                continue
            t = ts.value + dt
            if t > host.anim_length:
                t = 0.0
            host.set_anim_time(t)
            ts.value = t  # move the slider to match

    threading.Thread(target=_playback, daemon=True).start()

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
