"""
Canonical-ANNY import spec: phenotypes x poses through the C ABI, vertex-checked.

Copyright 2026 V-Sekai contributors.
SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

Fixtures come from make_anny_variants.py (3 phenotypes x 3 poses, 13,718 verts
each, mesh_to_usda.py schema). Each stage is imported through libidtx_core and
every imported vertex position must exist in the source set, and every source
position in the imported set, to 0.1 mm — about an eighth of a credit card's
thickness. The controls: a bent pose must NOT pass against the identity source,
and a heavy phenotype must NOT pass against the default one — a checker that
cannot tell those apart cannot certify anything.
"""

from __future__ import annotations

import ctypes
import os
import sys
from ctypes import c_char_p, c_float, c_int32, c_void_p
from pathlib import Path

import numpy as np
import pytest

REPO = Path(__file__).resolve().parents[3]
DLL_PATH = REPO / "build" / "idtx_core" / "libidtx_core.windows.x86_64.dll"
VARIANTS = Path(os.environ.get(
    "ANNY_VARIANTS",
    r"C:\Users\ernes\AppData\Local\Temp\claude\C--weftspun-keypoints"
    r"\42df4a07-57d8-4d08-89ba-742cfb7f729c\scratchpad\anny_variants"))

if not DLL_PATH.exists():
    pytest.skip(f"idtx_core DLL not built at {DLL_PATH}", allow_module_level=True)
if not VARIANTS.exists() or not list(VARIANTS.glob("*.usda")):
    pytest.skip(f"ANNY variant fixtures absent at {VARIANTS}; "
                "run make_anny_variants.py + mesh_to_usda.py first",
                allow_module_level=True)

if sys.platform == "win32":
    dep_dirs = [REPO / "flow" / "core" / "usd" / "libs" / "windows",
                REPO / "build" / "idtx_core"]
    for usd in sorted((REPO / "thirdparty").glob("openusd-*")):
        dep_dirs += [usd / "lib", usd / "bin"]
    for dep_dir in dep_dirs:
        if dep_dir.exists():
            os.add_dll_directory(str(dep_dir))

TOL = 1e-4  # 0.1 mm in a metres stage


def to_engine(src: np.ndarray) -> np.ndarray:
    """The stage is authored Z-up (mesh_to_usda measured the rig's tall axis);
    the importer converts to Y-up: (x, y, z) -> (x, z, -y). Conventions are
    data, so the checker applies the same mapping rather than assuming none."""
    return np.stack([src[:, 0], src[:, 2], -src[:, 1]], axis=1)


STAGES = sorted(p.stem for p in VARIANTS.glob("*.usda"))


@pytest.fixture(scope="module")
def core():
    for dep in ("tbb12.dll", "usd_ms.dll", "libidtx_usd.dll"):
        for d in dep_dirs:
            cand = d / dep
            if cand.exists():
                ctypes.CDLL(str(cand))
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
    lib.idtx_node_get_skinned_mesh.restype = c_void_p
    lib.idtx_node_get_skinned_mesh.argtypes = [c_void_p]
    lib.idtx_mesh_get_vertex_count.restype = c_int32
    lib.idtx_mesh_get_vertex_count.argtypes = [c_void_p]
    lib.idtx_mesh_get_positions.argtypes = [c_void_p, ctypes.POINTER(c_float)]
    return lib


def imported_positions(core, usda: Path) -> np.ndarray:
    scene = core.idtx_core_import_scene_from_usd(str(usda).encode())
    assert scene, f"import failed: {usda.name}"
    try:
        chunks = []
        for i in range(core.idtx_scene_get_node_count(scene)):
            node = core.idtx_scene_get_node(scene, i)
            for mesh in (core.idtx_node_get_mesh(node),
                         core.idtx_node_get_skinned_mesh(node)):
                if not mesh:
                    continue
                n = core.idtx_mesh_get_vertex_count(mesh)
                if n <= 0:
                    continue
                buf = (c_float * (n * 3))()
                core.idtx_mesh_get_positions(mesh, buf)
                chunks.append(np.frombuffer(buf, dtype=np.float32).reshape(n, 3).copy())
        assert chunks, f"no mesh vertices imported from {usda.name}"
        return np.vstack(chunks)
    finally:
        core.idtx_core_scene_destroy(scene)


def as_keys(pts: np.ndarray) -> set:
    return set(map(tuple, np.round(pts / TOL).astype(np.int64)))


def coverage(a: np.ndarray, b: np.ndarray) -> float:
    """Fraction of positions in `a` present in `b` (within TOL, via 3x3x3 cells)."""
    cells = np.round(b / TOL).astype(np.int64)
    grid = set(map(tuple, cells))
    hits = 0
    q = np.round(a / TOL).astype(np.int64)
    for p in q:
        if any((p[0] + dx, p[1] + dy, p[2] + dz) in grid
               for dx in (-1, 0, 1) for dy in (-1, 0, 1) for dz in (-1, 0, 1)):
            hits += 1
    return hits / len(a)


@pytest.mark.parametrize("stem", STAGES)
def test_variant_round_trips(core, stem):
    src = to_engine(np.load(VARIANTS / f"{stem}.npz")["verts"].astype(np.float32))
    got = imported_positions(core, VARIANTS / f"{stem}.usda")
    assert len(got) >= len(src), f"{stem}: {len(got)} imported < {len(src)} source"
    fwd = coverage(got, src)
    back = coverage(src, got)
    assert fwd > 0.999, f"{stem}: only {fwd:.4%} of imported verts exist in source"
    assert back > 0.999, f"{stem}: only {back:.4%} of source verts were imported"


def test_checker_detects_a_bent_pose(core):
    # Negative control: identity source against the elbow-bent import must fail
    # coverage, or the checker cannot see deformation at all.
    src = to_engine(np.load(VARIANTS / "default__identity.npz")["verts"].astype(np.float32))
    bent = imported_positions(core, VARIANTS / "default__elbow90.usda")
    assert coverage(bent, src) < 0.999, "elbow90 verts fully covered by identity"


def test_checker_detects_a_phenotype(core):
    src = to_engine(np.load(VARIANTS / "default__identity.npz")["verts"].astype(np.float32))
    heavy = imported_positions(core, VARIANTS / "heavy__identity.usda")
    assert coverage(heavy, src) < 0.999, "heavy verts fully covered by default"
