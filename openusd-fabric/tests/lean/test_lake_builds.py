"""
Cycle 1 gate: `lake build` succeeds under lean/.

Copyright 2026 The openusd-fabric authors.
SPDX-License-Identifier: MIT

Runs the Lean Lake build over the Fabric.* spec modules. Build success
means every native_decide proof verified — the spec is internally
consistent and ready for the emit-and-diff CI step (cycle 2).

Requires elan + lake on PATH; auto-skipped if absent so the
Python-only test job stays green on developer machines without Lean.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

import pytest

LEAN_ROOT = Path(__file__).resolve().parents[2] / "lean"


def _have(cmd: str) -> bool:
    return shutil.which(cmd) is not None


@pytest.mark.skipif(not _have("lake"),
                    reason="lake (elan/Lean toolchain) not on PATH; "
                           "install elan and `lake build` becomes the gate")
def test_lake_build_passes():
    """All Fabric.* modules compile, every native_decide proof checks."""
    assert (LEAN_ROOT / "lakefile.lean").exists(), \
        f"missing lakefile under {LEAN_ROOT}"
    res = subprocess.run(["lake", "build"],
                         cwd=LEAN_ROOT,
                         capture_output=True, text=True)
    if res.returncode != 0:
        sys.stderr.write(res.stdout)
        sys.stderr.write(res.stderr)
    assert res.returncode == 0, "lake build failed; see captured output above"


@pytest.mark.skipif(not _have("lake"), reason="lake not on PATH")
def test_emit_artifacts_runs():
    """`lake exe emit_artifacts` succeeds and produces the JSON map."""
    out_dir = LEAN_ROOT.parent / "build" / "maps"
    if out_dir.exists():
        for p in out_dir.glob("*"):
            p.unlink()
    res = subprocess.run(
        ["lake", "exe", "emit_artifacts", str((LEAN_ROOT.parent / "build").resolve())],
        cwd=LEAN_ROOT, capture_output=True, text=True)
    if res.returncode != 0:
        sys.stderr.write(res.stdout)
        sys.stderr.write(res.stderr)
    assert res.returncode == 0
    emitted = LEAN_ROOT.parent / "build" / "maps" / "scss_mtoon_map.json"
    assert emitted.exists(), f"emit_artifacts did not produce {emitted}"


@pytest.mark.skipif(not _have("lake"), reason="lake not on PATH")
def test_emitted_artifacts_match_committed_copy():
    """Cycle 2 gate: every committed maps/*.json must equal what
    `lake exe emit_artifacts` writes byte-for-byte. Catches the moment
    a Lean spec edit and a manual JSON edit diverge.
    """
    out_dir = LEAN_ROOT.parent / "build" / "maps"
    committed_dir = LEAN_ROOT.parent / "maps"
    # Make sure emit ran; the previous test does so but in case this is
    # invoked in isolation, run it again — idempotent.
    subprocess.run(
        ["lake", "exe", "emit_artifacts", str((LEAN_ROOT.parent / "build").resolve())],
        cwd=LEAN_ROOT, check=True)
    drifted: list[str] = []
    for emitted_file in sorted(out_dir.glob("*.json")):
        committed = committed_dir / emitted_file.name
        if not committed.exists():
            drifted.append(f"{emitted_file.name}: emitted but not committed")
            continue
        if committed.read_bytes() != emitted_file.read_bytes():
            drifted.append(f"{emitted_file.name}: committed != emitted")
    assert not drifted, (
        "Lean spec and committed JSON have diverged. Run "
        "`lake exe emit_artifacts build/` and commit the output, or "
        "fix the Lean spec to match. Drifted:\n  " + "\n  ".join(drifted)
    )


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v"]))
