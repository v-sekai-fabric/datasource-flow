"""
Blender → idtx_core layer-aware USD export hook (CHI-312 #3).

Drives libidtx_core's idtx_core_export_avatar_to_usd_ex from Blender: walk the
scene into an idtx_avatar_t via the flat C ABI (idtx_core_ctypes), then export
NEW_FLAT / OVERLAY / LAYER_ONLY / FLATTEN — the same core the Godot, Unity and
Unreal hosts call. This is the editor-side Blender implementation of the
two-implementations-per-category interop rule.

Two entry points (mirrors post_export_hook.py):

* CLI / headless:
      blender --background path/to/avatar.blend \\
          --python openusd-fabric/blender/idtx_core_export.py -- \\
          --out out.usda --mode overlay --source base.usda
* In-process, from an addon's wm.usd_export post-handler or a panel button:
      from openusd_fabric_blender import idtx_core_export
      idtx_core_export.export_scene_ex("out.usda", mode="overlay", source="base.usda")

The avatar IR builder needs bpy (runs inside Blender). The ctypes binding and
the _ex driver do not — they are unit-tested headless against a synthetic
avatar in tests/blender/test_idtx_core_export.py.

Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
"""

from __future__ import annotations

import argparse
import sys
from ctypes import c_float, c_int32
from pathlib import Path

from . import idtx_core_ctypes as core
from .idtx_core_ctypes import FLATTEN, LAYER_ONLY, NEW_FLAT, OVERLAY

_MODES = {"new_flat": NEW_FLAT, "overlay": OVERLAY, "layer_only": LAYER_ONLY, "flatten": FLATTEN}


def _identity16():
    m = [0.0] * 16
    m[0] = m[5] = m[10] = m[15] = 1.0
    return (c_float * 16)(*m)


def zup_to_yup_matrix():
    """Blender is right-handed Z-up, metres; USD defaults to Y-up. This is the
    -90° rotation about X applied to the avatar root so the export lands Y-up,
    matching Blender's own USD exporter convert_orientation. Row-major, packed
    for idtx_avatar_set_root_transform: (x,y,z) -> (x, z, -y)."""
    m = [
        1.0, 0.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, -1.0, 0.0, 0.0,
        0.0, 0.0, 0.0, 1.0,
    ]
    return (c_float * 16)(*m)


# ---------------------------------------------------------------------------
# Avatar IR builder (requires bpy — runs inside Blender)
# ---------------------------------------------------------------------------

def build_avatar_from_scene(lib, objects=None, convert_up_axis: bool = True):
    """Walk Blender mesh/armature/material data into an idtx_avatar_t.

    Returns an opaque avatar handle (int). Caller owns it — pair with
    lib.idtx_avatar_destroy. Raises RuntimeError if bpy is unavailable.
    """
    try:
        import bpy  # noqa: F401  (only importable inside Blender)
    except ImportError as exc:
        raise RuntimeError(
            "build_avatar_from_scene requires bpy — run inside Blender "
            "(blender --background --python ...)."
        ) from exc

    import bpy

    if objects is None:
        objects = list(bpy.context.scene.objects)

    av = lib.idtx_avatar_create()
    name = (bpy.context.scene.name or "BlenderAvatar").encode("utf-8")
    lib.idtx_avatar_set_name(av, name)
    if convert_up_axis:
        lib.idtx_avatar_set_root_transform(av, zup_to_yup_matrix())

    # Skeleton: first armature in the selection.
    armature = next((o for o in objects if o.type == "ARMATURE"), None)
    if armature is not None:
        skel = lib.idtx_skeleton_create()
        bone_index = {}
        for i, bone in enumerate(armature.data.bones):
            bone_index[bone.name] = i
        for bone in armature.data.bones:
            parent = bone_index.get(bone.parent.name, -1) if bone.parent else -1
            rest = _matrix16(bone.matrix_local)          # armature-space rest
            bind = _matrix16(bone.matrix_local.inverted())  # inverse-bind
            lib.idtx_skeleton_add_bone(skel, bone.name.encode("utf-8"),
                                       c_int32(parent), rest, bind)
        lib.idtx_avatar_set_skeleton(av, skel)

    # Materials (de-duplicated, index map by Blender material).
    mat_index = {}
    for obj in objects:
        if obj.type != "MESH":
            continue
        for slot in obj.material_slots:
            bmat = slot.material
            if bmat is None or bmat in mat_index:
                continue
            mh = lib.idtx_material_create()
            r, g, b, a = _base_color(bmat)
            lib.idtx_material_set_base_color(mh, c_float(r), c_float(g), c_float(b), c_float(a))
            mat_index[bmat] = lib.idtx_avatar_add_material(av, mh)

    # Meshes: triangulated positions + indices (normals/uvs optional).
    for obj in objects:
        if obj.type != "MESH":
            continue
        mesh_data = obj.data
        mesh_data.calc_loop_triangles()
        verts = mesh_data.vertices
        n = len(verts)
        pos = (c_float * (n * 3))()
        for i, v in enumerate(verts):
            pos[i * 3 + 0] = v.co.x
            pos[i * 3 + 1] = v.co.y
            pos[i * 3 + 2] = v.co.z
        tris = mesh_data.loop_triangles
        idx = (c_int32 * (len(tris) * 3))()
        for t, tri in enumerate(tris):
            idx[t * 3 + 0] = tri.vertices[0]
            idx[t * 3 + 1] = tri.vertices[1]
            idx[t * 3 + 2] = tri.vertices[2]
        mh = lib.idtx_mesh_create()
        lib.idtx_mesh_set_vertices(mh, c_int32(n), pos, None, None, None)
        lib.idtx_mesh_set_indices(mh, c_int32(len(tris) * 3), idx)
        # First material slot's avatar index, or -1.
        mi = -1
        if obj.material_slots and obj.material_slots[0].material in mat_index:
            mi = mat_index[obj.material_slots[0].material]
        lib.idtx_avatar_add_mesh(av, mh, c_int32(mi))

    return av


def _matrix16(m):
    """bpy Matrix (row-major 4x4) -> flat c_float[16] row-major."""
    flat = []
    for row in range(4):
        for col in range(4):
            flat.append(m[row][col])
    return (c_float * 16)(*flat)


def _base_color(bmat):
    """Best-effort base color from a Blender material (Principled BSDF if
    present, else diffuse_color)."""
    try:
        if bmat.use_nodes:
            for node in bmat.node_tree.nodes:
                if node.type == "BSDF_PRINCIPLED":
                    c = node.inputs["Base Color"].default_value
                    return float(c[0]), float(c[1]), float(c[2]), float(c[3])
    except Exception:
        pass
    c = getattr(bmat, "diffuse_color", (0.8, 0.8, 0.8, 1.0))
    return float(c[0]), float(c[1]), float(c[2]), float(c[3])


# ---------------------------------------------------------------------------
# Orchestration
# ---------------------------------------------------------------------------

def export_scene_ex(out_path, mode="overlay", source=None, edit_target_id=None,
                    reflect_per_prim=False, convert_up_axis=True, repo_root=None):
    """Build the current Blender scene into an avatar and export it via the
    layer-aware core. Returns the C ABI rc (0 == success)."""
    mode_val = _MODES.get(str(mode).lower())
    if mode_val is None:
        raise ValueError(f"unknown mode '{mode}'; expected one of {sorted(_MODES)}")
    lib = core.load(Path(repo_root) if repo_root else None)
    av = build_avatar_from_scene(lib, convert_up_axis=convert_up_axis)
    try:
        return core.export_avatar_ex(
            lib, av, out_path, mode=mode_val, source_path=source,
            edit_target_id=edit_target_id, reflect_per_prim=reflect_per_prim)
    finally:
        lib.idtx_avatar_destroy(av)


def _user_args():
    if "--" in sys.argv:
        return sys.argv[sys.argv.index("--") + 1:]
    return sys.argv[1:]


def main() -> int:
    parser = argparse.ArgumentParser(description="Blender -> idtx_core USD export")
    parser.add_argument("--out", required=True, help="Output .usd/.usda path.")
    parser.add_argument("--mode", default="overlay", choices=sorted(_MODES),
                        help="Export mode (default: overlay).")
    parser.add_argument("--source", default=None,
                        help="Base stage for overlay/layer_only modes.")
    parser.add_argument("--edit-target-id", default=None)
    parser.add_argument("--reflect-per-prim", action="store_true")
    parser.add_argument("--no-up-convert", action="store_true",
                        help="Skip the Blender Z-up -> USD Y-up root rotation.")
    args = parser.parse_args(_user_args())

    rc = export_scene_ex(
        args.out, mode=args.mode, source=args.source,
        edit_target_id=args.edit_target_id, reflect_per_prim=args.reflect_per_prim,
        convert_up_axis=not args.no_up_convert)
    if rc == 0:
        print(f"openusd-fabric: idtx_core exported {args.mode} -> {args.out}")
    else:
        print(f"error: idtx_core export rc={rc}", file=sys.stderr)
    return 0 if rc == 0 else 1


if __name__ == "__main__":
    # When run as `blender --python idtx_core_export.py`, the package context
    # ('.') is absent; fall back to a direct module import.
    if __package__ in (None, ""):
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        import idtx_core_ctypes as core  # type: ignore  # noqa: F811
    sys.exit(main())
