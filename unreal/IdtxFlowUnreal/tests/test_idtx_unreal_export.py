"""
Headless test for the Unreal idtx_core binding (CHI-312 #4), WITHOUT the
`unreal` module: authors a synthetic avatar via the C ABI and drives
NEW_FLAT / OVERLAY / FLATTEN. Proves the .sigs dlopen + _ex path the UE Python
integration relies on. The `unreal`-module avatar builder is exercised inside
the editor.

Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
"""

from __future__ import annotations

import sys
from ctypes import c_float, c_int32
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "unreal" / "IdtxFlowUnreal"))

import idtx_core_ctypes as core  # noqa: E402

if core.find_library(REPO) is None:
    pytest.skip("libidtx_core not built", allow_module_level=True)


@pytest.fixture(scope="module")
def lib():
    return core.load(REPO)


def _identity16():
    m = [0.0] * 16
    m[0] = m[5] = m[10] = m[15] = 1.0
    return (c_float * 16)(*m)


def _make_avatar(lib):
    av = lib.idtx_avatar_create()
    lib.idtx_avatar_set_name(av, b"UnrealDummy")
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


def test_version(lib):
    assert lib.idtx_core_version()


def test_new_flat_then_overlay(lib, tmp_path):
    src = tmp_path / "source.usda"
    av = _make_avatar(lib)
    assert core.export_avatar_ex(lib, av, src, mode=core.NEW_FLAT) == 0
    lib.idtx_avatar_destroy(av)
    assert src.exists()

    av2 = _make_avatar(lib)
    lib.idtx_avatar_set_source_usd_path(av2, str(src).encode())
    overlay = tmp_path / "overlay.usda"
    assert core.export_avatar_ex(lib, av2, overlay, mode=core.OVERLAY, source_path=src) == 0
    lib.idtx_avatar_destroy(av2)
    assert overlay.exists()
    imported = lib.idtx_core_import_avatar_from_usd(str(overlay).encode())
    assert imported
    lib.idtx_avatar_destroy(imported)


def test_flatten(lib, tmp_path):
    src = tmp_path / "s.usda"
    av = _make_avatar(lib)
    assert core.export_avatar_ex(lib, av, src, mode=core.NEW_FLAT) == 0
    lib.idtx_avatar_destroy(av)
    av2 = _make_avatar(lib)
    lib.idtx_avatar_set_source_usd_path(av2, str(src).encode())
    flat = tmp_path / "flat.usda"
    assert core.export_avatar_ex(lib, av2, flat, mode=core.FLATTEN, source_path=src) == 0
    lib.idtx_avatar_destroy(av2)
    assert flat.exists()
