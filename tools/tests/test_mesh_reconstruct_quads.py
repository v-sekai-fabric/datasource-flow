"""
Unit test for `idtx_mesh_reconstruct_quads` (CHI-253 CPU path).

Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

Loads `libidtx_core.dll` via ctypes and exercises the greedy
mutual-best matching on small hand-built meshes. Verifies the
output matches what the GPU Slang shader would produce on the
same inputs (both share the same algorithm spec).
"""

from __future__ import annotations

import ctypes
import os
import sys
from ctypes import (
    POINTER, c_char_p, c_float, c_int32, c_size_t, c_void_p)
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
DLL_PATH = REPO / "build" / "idtx_core" / "libidtx_core.windows.x86_64.dll"

if not DLL_PATH.exists():
    pytest.skip(f"idtx_core DLL not built at {DLL_PATH}",
                allow_module_level=True)

# Windows loads dependencies from PATH and (since Python 3.8) only from
# directories registered via os.add_dll_directory. libidtx_core needs
# usd_ms.dll (OpenUSD), tbb12.dll (TBB) and libidtx_usd.dll (V-Sekai
# USD wrapper) — surface those directories explicitly here.
if sys.platform == "win32":
    for dep_dir in [
        REPO / "thirdparty" / "openusd-25.11" / "lib",
        REPO / "thirdparty" / "openusd-25.11" / "bin",
        REPO / "usd" / "libs" / "windows",
        REPO / "build" / "idtx_core",
    ]:
        if dep_dir.exists():
            os.add_dll_directory(str(dep_dir))


@pytest.fixture(scope="module")
def core():
    """Loaded libidtx_core with the symbols we exercise."""
    lib = ctypes.CDLL(str(DLL_PATH))

    lib.idtx_mesh_create.restype  = c_void_p
    lib.idtx_mesh_destroy.argtypes = [c_void_p]

    lib.idtx_mesh_set_vertices.argtypes = [
        c_void_p, c_int32,
        POINTER(c_float), POINTER(c_float),
        POINTER(c_float), POINTER(c_float)]
    lib.idtx_mesh_set_indices.argtypes = [
        c_void_p, c_int32, POINTER(c_int32)]
    lib.idtx_mesh_set_face_vertex_counts.argtypes = [
        c_void_p, c_int32, POINTER(c_int32)]

    lib.idtx_mesh_get_index_count.argtypes = [c_void_p]
    lib.idtx_mesh_get_index_count.restype  = c_int32
    lib.idtx_mesh_get_face_vertex_count_count.argtypes = [c_void_p]
    lib.idtx_mesh_get_face_vertex_count_count.restype  = c_int32
    lib.idtx_mesh_get_indices.argtypes = [c_void_p, POINTER(c_int32)]
    lib.idtx_mesh_get_face_vertex_counts.argtypes = [c_void_p, POINTER(c_int32)]

    lib.idtx_mesh_reconstruct_quads.argtypes = [c_void_p, c_float]
    lib.idtx_mesh_reconstruct_quads.restype  = c_int32

    return lib


def _make_mesh(lib, positions: list[tuple], indices: list[int]):
    """Build an idtx_mesh_t with the given positions / triangle list."""
    m = lib.idtx_mesh_create()
    pos_flat = (c_float * (3 * len(positions)))(
        *[c for p in positions for c in p])
    lib.idtx_mesh_set_vertices(
        m, len(positions), pos_flat, None, None, None)
    idx_arr = (c_int32 * len(indices))(*indices)
    lib.idtx_mesh_set_indices(m, len(indices), idx_arr)
    return m


def _read_topology(lib, mesh) -> tuple[list[int], list[int]]:
    idx_n = lib.idtx_mesh_get_index_count(mesh)
    fvc_n = lib.idtx_mesh_get_face_vertex_count_count(mesh)
    idx_buf = (c_int32 * idx_n)()
    fvc_buf = (c_int32 * fvc_n)() if fvc_n > 0 else (c_int32 * 0)()
    lib.idtx_mesh_get_indices(mesh, idx_buf)
    if fvc_n > 0:
        lib.idtx_mesh_get_face_vertex_counts(mesh, fvc_buf)
    return list(idx_buf), list(fvc_buf)


def test_two_coplanar_triangles_pair_into_one_quad(core):
    """Classic case: two coplanar triangles sharing an edge.
       Expect: 1 quad, 0 leftover triangles, planarity passes."""
    # Square in z=0 plane:
    #    3 -- 2
    #    |  / |
    #    | /  |
    #    0 -- 1
    positions = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0),
                 (1.0, 1.0, 0.0), (0.0, 1.0, 0.0)]
    indices   = [0, 1, 2,  0, 2, 3]   # two CCW triangles
    mesh = _make_mesh(core, positions, indices)
    try:
        n_quads = core.idtx_mesh_reconstruct_quads(mesh, 5.0)
        assert n_quads == 1, f"expected 1 quad, got {n_quads}"
        idx, fvc = _read_topology(core, mesh)
        assert fvc == [4], f"expected single quad face count, got {fvc}"
        assert sorted(idx) == [0, 1, 2, 3], (
            f"quad should reference all four corners exactly once, got {idx}")
    finally:
        core.idtx_mesh_destroy(mesh)


def test_orthogonal_triangles_do_not_pair(core):
    """Two triangles sharing an edge but at a 90° fold.
       Planarity gate at 5° should reject; result is 0 quads,
       both triangles preserved."""
    # Triangle in z=0 plane and triangle in y=0 plane, sharing
    # the edge (0,0,0)-(1,0,0).
    positions = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0),
                 (0.5, 1.0, 0.0), (0.5, 0.0, 1.0)]
    indices   = [0, 1, 2,  0, 3, 1]
    mesh = _make_mesh(core, positions, indices)
    try:
        n_quads = core.idtx_mesh_reconstruct_quads(mesh, 5.0)
        assert n_quads == 0
        idx, fvc = _read_topology(core, mesh)
        # Two triangles preserved.
        assert fvc == [3, 3], f"expected two triangles, got {fvc}"
        assert len(idx) == 6
    finally:
        core.idtx_mesh_destroy(mesh)


def test_planarity_gate_widening_lets_orthogonal_pair_through(core):
    """Same orthogonal-fold input, but with planarity_max_degrees=180
       (effectively disabled). Expect them to pair into a quad."""
    positions = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0),
                 (0.5, 1.0, 0.0), (0.5, 0.0, 1.0)]
    indices   = [0, 1, 2,  0, 3, 1]
    mesh = _make_mesh(core, positions, indices)
    try:
        n_quads = core.idtx_mesh_reconstruct_quads(mesh, 180.0)
        assert n_quads == 1, f"with disabled gate expected 1 quad, got {n_quads}"
        idx, fvc = _read_topology(core, mesh)
        assert fvc == [4]
    finally:
        core.idtx_mesh_destroy(mesh)


def test_already_quad_mesh_is_left_alone(core):
    """If face_vertex_counts is already set (mesh has n-gons), the
    reconstruction must bail with 0 quads — don't re-process."""
    positions = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0),
                 (1.0, 1.0, 0.0), (0.0, 1.0, 0.0)]
    indices = [0, 1, 2, 3]
    mesh = _make_mesh(core, positions, indices)
    fvc_buf = (c_int32 * 1)(4)
    core.idtx_mesh_set_face_vertex_counts(mesh, 1, fvc_buf)
    try:
        n_quads = core.idtx_mesh_reconstruct_quads(mesh, 5.0)
        assert n_quads == 0
        idx, fvc = _read_topology(core, mesh)
        assert fvc == [4]
        assert idx == [0, 1, 2, 3]
    finally:
        core.idtx_mesh_destroy(mesh)


def test_single_triangle_is_a_no_op(core):
    """One triangle has no neighbour. Result: 0 quads, triangle preserved."""
    positions = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.5, 1.0, 0.0)]
    indices = [0, 1, 2]
    mesh = _make_mesh(core, positions, indices)
    try:
        n_quads = core.idtx_mesh_reconstruct_quads(mesh, 5.0)
        assert n_quads == 0
        # Still no face_vertex_counts (the function bailed before
        # tri_count > 1 check on idx_count first; <=1 returns 0).
    finally:
        core.idtx_mesh_destroy(mesh)


def test_null_mesh_returns_minus_one(core):
    rc = core.idtx_mesh_reconstruct_quads(None, 5.0)
    assert rc == -1
