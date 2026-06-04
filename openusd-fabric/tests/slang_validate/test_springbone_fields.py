"""
VRM 0.x -> 1.0 springbone field map (CHI-252).

Copyright 2026 The openusd-fabric authors.
SPDX-License-Identifier: MIT

VRM 1.0 renamed `stiffinessForce` (the original 0.x spec's typo) to
`stiffness`, kept the rest of the per-joint field names, and tightened
the legal ranges. This module's job: a value-clamping rename table.
"""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import springbone_fields as sb  # noqa: E402


def test_stiffness_typo_renamed_and_in_range():
    out = sb.vrm0_to_vrm1_spring({"stiffinessForce": 1.5})
    assert out == {"stiffness": 1.5}


def test_stiffness_clamps_to_vrm1_upper_bound():
    out = sb.vrm0_to_vrm1_spring({"stiffinessForce": 5.0})
    assert out["stiffness"] == 4.0


def test_drag_clamps_to_unit_interval():
    out = sb.vrm0_to_vrm1_spring({"dragForce": 1.5})
    assert out["dragForce"] == 1.0


def test_gravity_power_zero_lower_bound():
    out = sb.vrm0_to_vrm1_spring({"gravityPower": -1.0})
    assert out["gravityPower"] == 0.0


def test_hit_radius_positive_lower_bound():
    out = sb.vrm0_to_vrm1_spring({"hitRadius": 0.0})
    assert out["hitRadius"] > 0.0


def test_unknown_vrm0_field_dropped_silently():
    out = sb.vrm0_to_vrm1_spring({"weirdLegacyField": 42, "dragForce": 0.4})
    assert out == {"dragForce": 0.4}


def test_reverse_path_renames_back_and_clamps():
    out = sb.vrm1_to_vrm0_spring({"stiffness": 2.0})
    assert out == {"stiffinessForce": 2.0}


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v"]))
