"""
TDD test for the layer-aware USD exporter idtx_core_export_avatar_to_usd_ex
(ART idtx-flow #18 — round-trip / overlay / flatten export).

Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

Style mirrors test_mesh_reconstruct_quads.py: load libidtx_core.dll via
ctypes and drive the C ABI directly — no engine, no idtxcli. The test is
self-bootstrapping: it authors a tiny avatar, exports NEW_FLAT to create
a source stage, then exercises the OVERLAY / LAYER_ONLY / FLATTEN modes
against it. Fidelity is asserted IN-PROCESS by re-importing each export
(the most robust check), with usdchecker as an optional structural pass.
"""

from __future__ import annotations

import ctypes
import os
import shutil
import subprocess
import sys
from ctypes import POINTER, c_char_p, c_float, c_int32, c_void_p
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
DLL_PATH = REPO / "build" / "idtx_core" / "libidtx_core.windows.x86_64.dll"
USD_BIN = REPO / "thirdparty" / "openusd-25.11-withPython" / "bin"

if not DLL_PATH.exists():
    pytest.skip(f"idtx_core DLL not built at {DLL_PATH}", allow_module_level=True)

if sys.platform == "win32":
    for dep_dir in [
        REPO / "thirdparty" / "openusd-25.11" / "lib",
        REPO / "thirdparty" / "openusd-25.11" / "bin",
        REPO / "usd" / "libs" / "windows",
        REPO / "build" / "idtx_core",
    ]:
        if dep_dir.exists():
            os.add_dll_directory(str(dep_dir))

# idtx_usd_export_mode_t
NEW_FLAT, OVERLAY, LAYER_ONLY, FLATTEN = 0, 1, 2, 3


class IdtxUsdExportOpts(ctypes.Structure):
    _fields_ = [
        ("mode", c_int32),
        ("source_path", c_char_p),
        ("edit_target_id", c_char_p),
        ("reflect_per_prim", c_int32),
    ]


@pytest.fixture(scope="module")
def core():
    lib = ctypes.CDLL(str(DLL_PATH))

    lib.idtx_core_init.argtypes = [c_char_p]
    lib.idtx_core_init.restype = c_int32

    lib.idtx_avatar_create.restype = c_void_p
    lib.idtx_avatar_destroy.argtypes = [c_void_p]
    lib.idtx_avatar_set_name.argtypes = [c_void_p, c_char_p]
    lib.idtx_avatar_set_skeleton.argtypes = [c_void_p, c_void_p]
    lib.idtx_avatar_add_mesh.argtypes = [c_void_p, c_void_p, c_int32]
    lib.idtx_avatar_add_mesh.restype = c_int32
    lib.idtx_avatar_add_material.argtypes = [c_void_p, c_void_p]
    lib.idtx_avatar_add_material.restype = c_int32
    lib.idtx_avatar_get_mesh_count.argtypes = [c_void_p]
    lib.idtx_avatar_get_mesh_count.restype = c_int32
    lib.idtx_avatar_get_source_usd_path.argtypes = [c_void_p]
    lib.idtx_avatar_get_source_usd_path.restype = c_char_p
    lib.idtx_avatar_set_root_transform.argtypes = [c_void_p, POINTER(c_float)]

    lib.idtx_skeleton_create.restype = c_void_p
    lib.idtx_skeleton_add_bone.argtypes = [
        c_void_p, c_char_p, c_int32, POINTER(c_float), POINTER(c_float)]
    lib.idtx_skeleton_add_bone.restype = c_int32

    lib.idtx_mesh_create.restype = c_void_p
    lib.idtx_mesh_set_vertices.argtypes = [
        c_void_p, c_int32, POINTER(c_float), POINTER(c_float),
        POINTER(c_float), POINTER(c_float)]
    lib.idtx_mesh_set_indices.argtypes = [c_void_p, c_int32, POINTER(c_int32)]

    lib.idtx_material_create.restype = c_void_p
    lib.idtx_material_set_base_color.argtypes = [
        c_void_p, c_float, c_float, c_float, c_float]

    # The symbols under test — referencing them forces a clear failure on
    # an old DLL that predates the layer-aware exporter (red phase).
    lib.idtx_core_export_avatar_to_usd.argtypes = [c_void_p, c_char_p]
    lib.idtx_core_export_avatar_to_usd.restype = c_int32
    lib.idtx_usd_export_opts_init.argtypes = [POINTER(IdtxUsdExportOpts)]
    lib.idtx_core_export_avatar_to_usd_ex.argtypes = [
        c_void_p, c_char_p, POINTER(IdtxUsdExportOpts)]
    lib.idtx_core_export_avatar_to_usd_ex.restype = c_int32
    lib.idtx_core_import_avatar_from_usd.argtypes = [c_char_p]
    lib.idtx_core_import_avatar_from_usd.restype = c_void_p

    lib.idtx_core_init(None)
    return lib


def _identity16():
    m = [0.0] * 16
    m[0] = m[5] = m[10] = m[15] = 1.0
    return (c_float * 16)(*m)


def _make_avatar(lib):
    """A minimal but complete avatar: 1 bone, 1 triangle, 1 material."""
    av = lib.idtx_avatar_create()
    lib.idtx_avatar_set_name(av, b"TestDummy")

    skel = lib.idtx_skeleton_create()
    lib.idtx_skeleton_add_bone(skel, b"Root", -1, _identity16(), _identity16())
    lib.idtx_avatar_set_skeleton(av, skel)

    mesh = lib.idtx_mesh_create()
    pos = (c_float * 9)(0, 0, 0, 1, 0, 0, 0, 1, 0)
    lib.idtx_mesh_set_vertices(mesh, 3, pos, None, None, None)
    idx = (c_int32 * 3)(0, 1, 2)
    lib.idtx_mesh_set_indices(mesh, 3, idx)

    mat = lib.idtx_material_create()
    lib.idtx_material_set_base_color(mat, 0.8, 0.2, 0.2, 1.0)
    mi = lib.idtx_avatar_add_material(av, mat)
    lib.idtx_avatar_add_mesh(av, mesh, mi)
    return av


def _usdchecker(path: Path):
    """Run the upstream usdchecker — the authority on USD validity. Returns
    (ran, ok). usdchecker.exe needs its sibling usd_ms.dll etc. on PATH to
    load, so inject the OpenUSD lib/bin dirs into the child env."""
    exe = USD_BIN / "usdchecker.exe"
    if not exe.exists():
        return False, True
    env = dict(os.environ)
    env["PATH"] = os.pathsep.join([
        str(USD_BIN.parent / "lib"), str(USD_BIN), env.get("PATH", "")])
    try:
        r = subprocess.run([str(exe), str(path)], capture_output=True,
                           text=True, timeout=120, env=env)
        return True, r.returncode == 0
    except Exception:
        return False, True


@pytest.fixture(scope="module")
def source_stage(core, tmp_path_factory):
    """Author the avatar and write a NEW_FLAT source stage once."""
    out = tmp_path_factory.mktemp("usd") / "source.usda"
    av = _make_avatar(core)
    rc = core.idtx_core_export_avatar_to_usd(av, str(out).encode())
    core.idtx_avatar_destroy(av)
    assert rc == 0, f"flat export failed rc={rc}"
    assert out.exists()
    return out


# --- the layer-aware exporter ------------------------------------------

def test_symbols_present(core):
    """Red phase guard: the layer-aware ABI must be exported."""
    for sym in ("idtx_core_export_avatar_to_usd_ex",
                "idtx_usd_export_opts_init",
                "idtx_avatar_set_source_usd_path",
                "idtx_avatar_get_source_usd_path"):
        assert hasattr(core, sym), f"missing export {sym}"


def test_opts_init_defaults(core):
    opts = IdtxUsdExportOpts()
    core.idtx_usd_export_opts_init(ctypes.byref(opts))
    assert opts.mode == NEW_FLAT
    assert opts.source_path is None
    assert opts.reflect_per_prim == 0


def test_import_stamps_source_usd_path(core, source_stage):
    av = core.idtx_core_import_avatar_from_usd(str(source_stage).encode())
    assert av, "import returned NULL"
    prov = core.idtx_avatar_get_source_usd_path(av)
    core.idtx_avatar_destroy(av)
    assert prov, "import did not stamp source_usd_path"
    assert b"source" in prov  # the stage identifier we imported from


def test_ex_flat_matches_flat(core, source_stage, tmp_path):
    """_ex(NEW_FLAT, source=NULL) must equal the legacy flat write."""
    out = tmp_path / "flat_ex.usda"
    av = _make_avatar(core)
    opts = IdtxUsdExportOpts()
    core.idtx_usd_export_opts_init(ctypes.byref(opts))  # NEW_FLAT
    rc = core.idtx_core_export_avatar_to_usd_ex(av, str(out).encode(),
                                                ctypes.byref(opts))
    core.idtx_avatar_destroy(av)
    assert rc == 0
    ran, ok = _usdchecker(out)
    assert ok, "usdchecker failed on _ex NEW_FLAT output"


@pytest.mark.parametrize("mode,name", [
    (FLATTEN, "flatten"),
    (OVERLAY, "overlay"),
    (LAYER_ONLY, "layer_only"),
])
def test_ex_modes_roundtrip(core, source_stage, tmp_path, mode, name):
    """Import the source stage (stamping provenance), re-export under each
    layer mode, then re-import the result and assert the avatar survives."""
    out = tmp_path / f"{name}.usda"
    av = core.idtx_core_import_avatar_from_usd(str(source_stage).encode())
    assert av, "import returned NULL"

    opts = IdtxUsdExportOpts()
    core.idtx_usd_export_opts_init(ctypes.byref(opts))
    opts.mode = mode
    src = str(source_stage).encode()
    opts.source_path = src  # explicit; also falls back to provenance
    rc = core.idtx_core_export_avatar_to_usd_ex(av, str(out).encode(),
                                                ctypes.byref(opts))
    core.idtx_avatar_destroy(av)
    assert rc == 0, f"{name} export failed rc={rc}"
    assert out.exists()

    # In-process fidelity: the composed/flattened result must still yield
    # an avatar with the mesh we authored.
    back = core.idtx_core_import_avatar_from_usd(str(out).encode())
    assert back, f"re-import of {name} output returned NULL"
    n = core.idtx_avatar_get_mesh_count(back)
    core.idtx_avatar_destroy(back)
    assert n >= 1, f"{name}: mesh lost through round-trip (got {n})"

    # Delta artifacts pull the source in by an arc — OVERLAY via a
    # sublayer, LAYER_ONLY via a composition reference.
    if mode == OVERLAY:
        text = out.read_text(errors="replace")
        assert "subLayers" in text, "overlay: expected a subLayers arc to source"
    elif mode == LAYER_ONLY:
        text = out.read_text(errors="replace")
        assert "references" in text, "layer_only: expected a reference arc to source"
        assert "subLayers" not in text, "layer_only: should not use a sublayer"

    ran, ok = _usdchecker(out)
    assert ok, f"usdchecker failed on {name} output"


def test_overlay_unchanged_is_minimal(core, source_stage, tmp_path):
    """An avatar imported from the source and re-exported UNCHANGED must
    author no redundant attribute values — the delta carries only deltas."""
    out = tmp_path / "overlay_min.usda"
    av = core.idtx_core_import_avatar_from_usd(str(source_stage).encode())
    assert av
    opts = IdtxUsdExportOpts()
    core.idtx_usd_export_opts_init(ctypes.byref(opts))
    opts.mode = OVERLAY
    src = str(source_stage).encode()
    opts.source_path = src
    rc = core.idtx_core_export_avatar_to_usd_ex(av, str(out).encode(),
                                                ctypes.byref(opts))
    core.idtx_avatar_destroy(av)
    assert rc == 0
    text = out.read_text(errors="replace")
    # Unchanged opinions must have been subtracted against the source —
    # geometry, material values, AND the material:binding relationship.
    assert "point3f[] points" not in text, "mesh points redundantly re-authored"
    assert "diffuseColor" not in text, "material redundantly re-authored"
    assert "material:binding" not in text, "relationship redundantly re-authored"
    assert "subLayers" in text
    ran, ok = _usdchecker(out)
    assert ok, "usdchecker failed on minimal overlay"


def test_layer_only_uses_reference_arc(core, source_stage, tmp_path):
    """LAYER_ONLY pulls the source in by a composition reference (not a
    sublayer), and the composed result still yields the avatar."""
    out = tmp_path / "layer_only_ref.usda"
    av = core.idtx_core_import_avatar_from_usd(str(source_stage).encode())
    assert av
    opts = IdtxUsdExportOpts()
    core.idtx_usd_export_opts_init(ctypes.byref(opts))
    opts.mode = LAYER_ONLY
    src = str(source_stage).encode()
    opts.source_path = src
    rc = core.idtx_core_export_avatar_to_usd_ex(av, str(out).encode(),
                                                ctypes.byref(opts))
    core.idtx_avatar_destroy(av)
    assert rc == 0
    text = out.read_text(errors="replace")
    assert "references" in text, "layer_only: expected a reference arc"
    assert "subLayers" not in text, "layer_only: should not use a sublayer"
    assert "point3f[] points" not in text, "geometry redundantly authored"
    # Composed via the reference, the avatar still imports back.
    back = core.idtx_core_import_avatar_from_usd(str(out).encode())
    assert back, "re-import of layer_only output returned NULL"
    n = core.idtx_avatar_get_mesh_count(back)
    core.idtx_avatar_destroy(back)
    assert n >= 1, f"layer_only: mesh lost through reference (got {n})"
    ran, ok = _usdchecker(out)
    assert ok, "usdchecker failed on layer_only reference output"


def test_overlay_changed_value_appears_in_delta(core, source_stage, tmp_path):
    """A changed attribute (root transform) lands in the delta; unchanged
    geometry/material still do not."""
    out = tmp_path / "overlay_changed.usda"
    av = core.idtx_core_import_avatar_from_usd(str(source_stage).encode())
    assert av
    # Translate the root by (5, 0, 0) — row-major, translation in last row.
    m = [1.0, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  5.0, 0, 0, 1]
    core.idtx_avatar_set_root_transform(av, (c_float * 16)(*m))
    opts = IdtxUsdExportOpts()
    core.idtx_usd_export_opts_init(ctypes.byref(opts))
    opts.mode = OVERLAY
    src = str(source_stage).encode()
    opts.source_path = src
    rc = core.idtx_core_export_avatar_to_usd_ex(av, str(out).encode(),
                                                ctypes.byref(opts))
    core.idtx_avatar_destroy(av)
    assert rc == 0
    text = out.read_text(errors="replace")
    assert "xformOp:transform" in text, "changed root transform missing from delta"
    assert "5" in text, "changed translation value missing from delta"
    assert "point3f[] points" not in text, "unchanged mesh redundantly authored"
    ran, ok = _usdchecker(out)
    assert ok, "usdchecker failed on changed overlay"
