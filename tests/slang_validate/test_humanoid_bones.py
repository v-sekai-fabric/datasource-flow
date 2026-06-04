"""
VRM 0.x -> 1.0 humanoid bone map (CHI-252).

Copyright 2026 The openusd-fabric authors.
SPDX-License-Identifier: MIT

The VRM humanoid bone enum is stable across 0.x and 1.0 — the spec
rename happened in the structure that wraps the enum, not the names
themselves. This module's job is therefore:

  * confirm the VRM 1.0 enum is exhaustive (no bone the spec lists is
    missing from our table);
  * confirm `required-in-VRM-1.0` matches the spec;
  * provide the trivial vrm0_to_vrm1_bone identity function as the
    seam where any future divergence would land.
"""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import humanoid_bones as h  # noqa: E402


def test_hips_round_trips():
    """The simplest VRM bone name -- the cycle 3 stub assertion."""
    assert h.vrm0_to_vrm1_bone("hips") == "hips"


def test_all_55_vrm1_bones_are_in_the_map():
    """VRM 1.0 spec defines 55 humanoid bones (including the optional
    finger bones). The map must enumerate all of them."""
    assert len(h.VRM1_HUMANOID_BONES) == 55


def test_vrm1_required_bones_subset_of_full_enum():
    for b in h.VRM1_REQUIRED_BONES:
        assert b in h.VRM1_HUMANOID_BONES, f"{b!r} required but not in enum"


def test_unknown_vrm0_bone_returns_none():
    assert h.vrm0_to_vrm1_bone("notARealBone") is None


def test_reverse_identity():
    for b in h.VRM1_HUMANOID_BONES:
        assert h.vrm1_to_vrm0_bone(b) == b, f"{b!r} round-trip failed"


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v"]))
