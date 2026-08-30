"""
USD-to-GLB correspondence per phenotype slider, through the vertex checker.

Copyright 2026 V-Sekai contributors.
SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

Twelve cases from make_phenotype_pairs.py: the default body and each phenotype
axis at 1.0, no combinations. Each case's USD imports through the C ABI and its
GLB loads independently; the two vertex sets must cover each other at 0.1 mm —
an eighth of a credit card. The control: the heavy body's GLB against the
default body's USD import must NOT pass, or the checker cannot see a phenotype.
"""

from __future__ import annotations

import ctypes
import json
import os
import struct
import sys
from ctypes import c_char_p, c_float, c_int32, c_void_p
from pathlib import Path

import numpy as np
import pytest

REPO = Path(__file__).resolve().parents[3]
DLL_PATH = REPO / "build" / "idtx_core" / "libidtx_core.windows.x86_64.dll"
PAIRS = Path(os.environ.get(
    "PHENO_PAIRS",
    r"C:\Users\ernes\AppData\Local\Temp\claude\C--weftspun-keypoints"
    r"\42df4a07-57d8-4d08-89ba-742cfb7f729c\scratchpad\pheno_pairs"))

if not DLL_PATH.exists():
    pytest.skip(f"idtx_core DLL not built at {DLL_PATH}", allow_module_level=True)
if not PAIRS.exists() or not list(PAIRS.glob("*.usda")):
    pytest.skip(f"phenotype pairs absent at {PAIRS}; run make_phenotype_pairs.py",
                allow_module_level=True)

if sys.platform == "win32":
    dep_dirs = [REPO / "flow" / "core" / "usd" / "libs" / "windows",
                REPO / "build" / "idtx_core"]
    for usd in sorted((REPO / "thirdparty").glob("openusd-*")):
        dep_dirs += [usd / "lib", usd / "bin"]
    for dep_dir in dep_dirs:
        if dep_dir.exists():
            os.add_dll_directory(str(dep_dir))

TOL = 1e-4
CASES = sorted(p.stem for p in PAIRS.glob("*.usda"))


def read_glb_positions(path: Path) -> np.ndarray:
    """Minimal GLB POSITION reader — no engine, no importer under test."""
    raw = path.read_bytes()
    assert raw[:4] == b"glTF"
    json_len = struct.unpack_from("<I", raw, 12)[0]
    doc = json.loads(raw[20:20 + json_len])
    bin_off = 20 + json_len + 8
    prim = doc["meshes"][0]["primitives"][0]
    acc = doc["accessors"][prim["attributes"]["POSITION"]]
    view = doc["bufferViews"][acc["bufferView"]]
    start = bin_off + view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    return np.frombuffer(raw, dtype=np.float32,
                         count=acc["count"] * 3, offset=start).reshape(-1, 3)


@pytest.fixture(scope="module")
def core():
    for dep in ("tbb12.dll", "usd_ms.dll", "libidtx_usd.dll"):
        for d in dep_dirs:
            if (d / dep).exists():
                ctypes.CDLL(str(d / dep))
                break
    lib = ctypes.CDLL(str(DLL_PATH))
    lib.idtx_core_import_scene_from_usd.restype = c_void_p
    lib.idtx_core_import_scene_from_usd.argtypes = [c_char_p]
    lib.idtx_core_scene_destroy.argtypes = [c_void_p]
    lib.idtx_scene_get_node_count.restype = c_int32
    lib.idtx_scene_get_node_count.argtypes = [c_void_p]
    lib.idtx_scene_get_node.restype = c_void_p
    lib.idtx_scene_get_node.argtypes = [c_void_p, c_int32]
    lib.idtx_node_get_mesh.restype = c_void_p
    lib.idtx_node_get_mesh.argtypes = [c_void_p]
    lib.idtx_mesh_get_vertex_count.restype = c_int32
    lib.idtx_mesh_get_vertex_count.argtypes = [c_void_p]
    lib.idtx_mesh_get_positions.argtypes = [c_void_p, ctypes.POINTER(c_float)]
    return lib


def usd_positions(core, usda: Path) -> np.ndarray:
    scene = core.idtx_core_import_scene_from_usd(str(usda).encode())
    assert scene, f"import failed: {usda.name}"
    try:
        for i in range(core.idtx_scene_get_node_count(scene)):
            mesh = core.idtx_node_get_mesh(core.idtx_scene_get_node(scene, i))
            if mesh and core.idtx_mesh_get_vertex_count(mesh) > 0:
                n = core.idtx_mesh_get_vertex_count(mesh)
                buf = (c_float * (n * 3))()
                core.idtx_mesh_get_positions(mesh, buf)
                return np.frombuffer(buf, dtype=np.float32).reshape(n, 3).copy()
        raise AssertionError(f"no mesh in {usda.name}")
    finally:
        core.idtx_core_scene_destroy(scene)


def coverage(a: np.ndarray, b: np.ndarray) -> float:
    grid = set(map(tuple, np.round(b / TOL).astype(np.int64)))
    q = np.round(a / TOL).astype(np.int64)
    hits = sum(1 for p in q
               if any((p[0]+dx, p[1]+dy, p[2]+dz) in grid
                      for dx in (-1, 0, 1) for dy in (-1, 0, 1) for dz in (-1, 0, 1)))
    return hits / len(a)


@pytest.mark.parametrize("case", CASES)
def test_usd_and_glb_agree(core, case):
    u = usd_positions(core, PAIRS / f"{case}.usda")
    g = read_glb_positions(PAIRS / f"{case}.glb")
    fwd, back = coverage(u, g), coverage(g, u)
    assert fwd > 0.999, f"{case}: only {fwd:.4%} of USD-imported verts exist in the GLB"
    assert back > 0.999, f"{case}: only {back:.4%} of GLB verts exist in the USD import"


def test_checker_sees_a_phenotype(core):
    # Control: the heavy GLB against the default USD import must fail, or a
    # green board proves nothing about the sliders.
    u = usd_positions(core, PAIRS / "default.usda")
    g = read_glb_positions(PAIRS / "ph_weight.glb")
    assert coverage(g, u) < 0.999, "heavy GLB fully covered by the default body"
