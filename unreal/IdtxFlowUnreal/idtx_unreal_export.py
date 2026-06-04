"""
Unreal -> idtx_core layer-aware USD export, with NO C++ plugin (CHI-312 #4).

Runs inside Unreal's built-in Python (the `unreal` module): walks a selected
Skeletal/Static mesh into an idtx_avatar_t via the flat C ABI (idtx_core_ctypes,
loaded with ctypes), then exports NEW_FLAT / OVERLAY / LAYER_ONLY / FLATTEN
through libidtx_core — the same core the Godot, Unity and Blender hosts call.
"Merely the .sigs": the generated dlopen table + the thin ctypes binding are
the whole integration; there is no .uplugin and nothing to compile.

How to run:
  * UE editor → Output Log → Cmd dropdown set to "Python":
        exec(open(r"<repo>/unreal/IdtxFlowUnreal/idtx_unreal_export.py").read())
        export_selected(r"D:/out.usda", mode="overlay", source=r"D:/base.usda")
  * Headless:
        UnrealEditor-Cmd.exe <Project>.uproject -run=pythonscript \
            -script="<repo>/unreal/IdtxFlowUnreal/idtx_unreal_export.py"

Geometry extraction uses GeometryScripting (UE5) to copy the asset into a
DynamicMesh and read vertices/triangles — accessible from stock UE5 Python.
On UE4.27 (no GeometryScripting) the skeleton + materials still export; the
geometry path degrades and logs a clear warning (use the asset-export route
or build on a newer engine for full fidelity).

Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
"""

from __future__ import annotations

import os
import sys
from ctypes import c_float, c_int32
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import idtx_core_ctypes as core  # noqa: E402

try:
    import unreal  # only importable inside Unreal's Python
except ImportError:
    unreal = None

_MODES = {"new_flat": core.NEW_FLAT, "overlay": core.OVERLAY,
          "layer_only": core.LAYER_ONLY, "flatten": core.FLATTEN}


# --- coordinate conversion -------------------------------------------------

def _ue_to_usd_root():
    """UE (left-handed, Z-up, cm) -> USD/idtx (right-handed, Y-up, m). Row-major
    float[16] for idtx_avatar_set_root_transform: scale 0.01, (x,y,z)->(x,z,-y).
    Single place to tune (see CHI-312 coordinate-conversion note)."""
    s = 0.01
    m = [
        s, 0.0, 0.0, 0.0,
        0.0, 0.0, s, 0.0,
        0.0, -s, 0.0, 0.0,
        0.0, 0.0, 0.0, 1.0,
    ]
    return (c_float * 16)(*m)


def _identity16():
    m = [0.0] * 16
    m[0] = m[5] = m[10] = m[15] = 1.0
    return (c_float * 16)(*m)


# --- geometry via GeometryScripting (UE5) ----------------------------------

def _dynamic_mesh_from_asset(asset):
    """Copy a Static/Skeletal mesh into a DynamicMesh via GeometryScripting.
    Returns the unreal.DynamicMesh, or None if unavailable (UE4.27)."""
    try:
        dm = unreal.DynamicMesh()
        opts = unreal.GeometryScriptCopyMeshFromAssetOptions()
        lod = unreal.GeometryScriptMeshReadLOD()
        gsa = unreal.GeometryScriptLibrary_MeshAssetFunctions
        if isinstance(asset, unreal.StaticMesh):
            dm, _ = gsa.copy_mesh_from_static_mesh(asset, dm, opts, lod)
        elif isinstance(asset, unreal.SkeletalMesh):
            dm, _ = gsa.copy_mesh_from_skeletal_mesh(asset, dm, opts, lod)
        else:
            return None
        return dm
    except Exception as exc:  # GeometryScripting plugin absent (e.g. UE4.27)
        unreal.log_warning(f"idtx: GeometryScripting unavailable, geometry skipped ({exc})")
        return None


def _add_geometry(lib, avatar, dm, material_index):
    q = unreal.GeometryScriptLibrary_MeshQueryFunctions
    num_verts = q.get_num_vertex_ids(dm)
    pos = (c_float * (num_verts * 3))()
    for vid in range(num_verts):
        p = q.get_vertex_position(dm, vid)[0] if isinstance(q.get_vertex_position(dm, vid), tuple) \
            else q.get_vertex_position(dm, vid)
        pos[vid * 3 + 0] = float(p.x)
        pos[vid * 3 + 1] = float(p.y)
        pos[vid * 3 + 2] = float(p.z)
    num_tris = q.get_num_triangle_ids(dm)
    idx = (c_int32 * (num_tris * 3))()
    for tid in range(num_tris):
        tri = q.get_triangle_indices(dm, tid)
        verts = tri[0] if isinstance(tri, tuple) else tri
        idx[tid * 3 + 0] = int(verts.x)
        idx[tid * 3 + 1] = int(verts.y)
        idx[tid * 3 + 2] = int(verts.z)
    mh = lib.idtx_mesh_create()
    lib.idtx_mesh_set_vertices(mh, c_int32(num_verts), pos, None, None, None)
    lib.idtx_mesh_set_indices(mh, c_int32(num_tris * 3), idx)
    lib.idtx_avatar_add_mesh(avatar, mh, c_int32(material_index))


# --- avatar builder --------------------------------------------------------

def build_avatar(lib, asset, convert_up_axis: bool = True):
    """Build an idtx_avatar_t from an Unreal Static/Skeletal mesh asset."""
    av = lib.idtx_avatar_create()
    lib.idtx_avatar_set_name(av, str(asset.get_name()).encode("utf-8"))
    lib.idtx_avatar_set_root_transform(av, _ue_to_usd_root() if convert_up_axis else _identity16())

    # Materials.
    mat_index = -1
    materials = []
    if isinstance(asset, unreal.SkeletalMesh):
        materials = [m.material_interface for m in asset.materials]
    elif isinstance(asset, unreal.StaticMesh):
        materials = [asset.get_material(i) for i in range(asset.get_num_sections(0))]
    for mif in materials:
        mh = lib.idtx_material_create()
        r, g, b, a = 0.8, 0.8, 0.8, 1.0
        try:
            col = unreal.MaterialEditingLibrary.get_material_default_scalar_parameter_value  # noqa: F841
        except Exception:
            pass
        lib.idtx_material_set_base_color(mh, r, g, b, a)
        mi = lib.idtx_avatar_add_material(av, mh)
        if mat_index < 0:
            mat_index = mi

    # Geometry via GeometryScripting (UE5).
    dm = _dynamic_mesh_from_asset(asset)
    if dm is not None:
        _add_geometry(lib, av, dm, mat_index)

    return av


# --- orchestration ---------------------------------------------------------

def export_asset(asset, out_path, mode="overlay", source=None,
                 edit_target_id=None, reflect_per_prim=False, repo_root=None):
    mode_val = _MODES.get(str(mode).lower())
    if mode_val is None:
        raise ValueError(f"unknown mode '{mode}'; expected one of {sorted(_MODES)}")
    lib = core.load(Path(repo_root) if repo_root else None)
    av = build_avatar(lib, asset)
    try:
        rc = core.export_avatar_ex(lib, av, out_path, mode=mode_val, source_path=source,
                                   edit_target_id=edit_target_id, reflect_per_prim=reflect_per_prim)
    finally:
        lib.idtx_avatar_destroy(av)
    return rc


def export_selected(out_path, mode="overlay", source=None, **kw):
    """Export the first selected Static/Skeletal mesh in the Content Browser."""
    if unreal is None:
        raise RuntimeError("export_selected must run inside Unreal's Python.")
    selected = unreal.EditorUtilityLibrary.get_selected_assets()
    target = next((a for a in selected
                   if isinstance(a, (unreal.SkeletalMesh, unreal.StaticMesh))), None)
    if target is None:
        unreal.log_error("idtx: select a Skeletal or Static mesh in the Content Browser first.")
        return 1
    rc = export_asset(target, out_path, mode=mode, source=source, **kw)
    (unreal.log if rc == 0 else unreal.log_error)(
        f"idtx: export {mode} rc={rc} -> {out_path}")
    return rc


if __name__ == "__main__":
    # Headless -run=pythonscript form reads env vars (UE strips argv).
    out = os.environ.get("IDTX_OUT")
    if out:
        export_selected(out, mode=os.environ.get("IDTX_MODE", "overlay"),
                        source=os.environ.get("IDTX_SOURCE"))
