"""
Host-diff harness for the tris-to-quads GPU matching shader (CHI-253).

Copyright 2026 The openusd-fabric authors.
SPDX-License-Identifier: MIT

The Lean spec emits `shaders/tris_to_quads_match.slang` via
`lake exe emit_artifacts`. This harness:

1. Asserts the committed shader file structure matches the AST
   contract (entry-point name, buffer bindings, numthreads).
2. If `slangc` is on PATH, runs it on the emitted shader to confirm
   it compiles to SPIR-V (real downstream-tool gate).

The shader implements greedy mutual-best matching over the triangle
adjacency graph: each thread reads its triangle's three neighbours
and picks the highest-weight one. A separate confirm-quad pass
(future) checks mutual best and emits final quad pairs.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
SHADER_PATH = REPO_ROOT / "shaders" / "tris_to_quads_match.slang"


def test_shader_file_present():
    assert SHADER_PATH.exists(), (
        f"committed shader missing at {SHADER_PATH}; "
        f"run `lake exe emit_artifacts build/` and copy from "
        f"build/shaders/")


def test_shader_has_compute_entry_point():
    """The shader must declare [shader("compute")] and [numthreads(...)]
    on a `match_triangles` entry point."""
    src = SHADER_PATH.read_text(encoding="utf-8")
    assert '[shader("compute")]' in src, "missing compute attribute"
    assert "[numthreads(64, 1, 1)]" in src, "missing numthreads(64,1,1)"
    assert "void match_triangles(" in src, "missing match_triangles entry"


def test_shader_buffers_declared_in_expected_layout():
    """Buffer bindings: (0,0) neighbour, (1,0) weight, (2,0) out_best.
    The integration code on the C++/GDExtension side dispatches with
    these exact slot indices; drift breaks the dispatch."""
    src = SHADER_PATH.read_text(encoding="utf-8")
    assert "[[vk::binding(0, 0)]]" in src
    assert "StructuredBuffer<uint> neighbour;" in src
    assert "[[vk::binding(1, 0)]]" in src
    assert "StructuredBuffer<float> weight;" in src
    assert "[[vk::binding(2, 0)]]" in src
    assert "RWStructuredBuffer<uint> out_best;" in src
    assert "uint num_tris;" in src, (
        "num_tris (triangle count) must be a global uint")


def test_shader_uses_invalid_sentinel_for_boundary():
    """Boundary triangles encode neighbour = 0xFFFFFFFF = 4294967295u.
    The shader must guard against this sentinel before reading weight[]
    (else the boundary case picks up a stale weight)."""
    src = SHADER_PATH.read_text(encoding="utf-8")
    assert "4294967295u" in src, "missing invalid-neighbour sentinel"
    # Guard pattern: the neighbour-valid check must precede the weight read.
    valid_check_idx = src.find("(j != 4294967295u)")
    weight_read_idx = src.find("float w = weight[")
    assert 0 < valid_check_idx < weight_read_idx, (
        "weight[] read must be inside the j != INVALID guard")


@pytest.mark.skipif(shutil.which("slangc") is None,
                    reason="slangc not on PATH; the Lean-side byte-pin and "
                           "the structural checks above stay the gate. "
                           "Install shader-slang to enable the real compiler.")
def test_slangc_compiles_to_spirv(tmp_path: Path):
    """Real downstream-tool gate: slangc must produce SPIR-V."""
    out_spv = tmp_path / "match.spv"
    proc = subprocess.run(
        ["slangc", str(SHADER_PATH),
         "-target", "spirv",
         "-stage", "compute",
         "-entry", "match_triangles",
         "-o", str(out_spv)],
        capture_output=True, text=True, timeout=30)
    assert proc.returncode == 0, (
        f"slangc failed:\n  stdout: {proc.stdout}\n  stderr: {proc.stderr}")
    assert out_spv.exists() and out_spv.stat().st_size > 0


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v"]))
