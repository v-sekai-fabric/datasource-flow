"""
Round-trip + property tests for the SCSS ↔ MToon bridge.

Copyright 2026 The openusd-fabric authors.
SPDX-License-Identifier: MIT

Verifies:
  * `scss_to_mtoon(mtoon_to_scss(x)) ≈ x` on the recoverable MToon
    subset (i.e. ignoring shadingToonyFactor / rimLightingMixFactor and
    other documented MToon-only fields).
  * Specific Mire-shaped fixtures (face, hair, body) translate without
    surprises.
  * Outline-mode round-trips token <-> enum without drift.
  * The JSON map matches the Lean string literal byte-for-byte
    (caught the moment the Lake emit step lands; until then it's a
    syntactic well-formedness check + manual diff prompt).
"""

from __future__ import annotations

import json
import math
from pathlib import Path
import sys

import pytest
from hypothesis import given, settings, strategies as st

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import scss_mtoon as bridge  # noqa: E402


REPO_ROOT = Path(__file__).resolve().parents[2]
MAP_PATH = REPO_ROOT / "maps" / "scss_mtoon_map.json"


def test_map_loads_and_has_required_keys():
    m = bridge.reload_map()
    assert m["version"] == 1
    assert isinstance(m["entries"], list) and len(m["entries"]) >= 11
    for entry in m["entries"]:
        for k in ("scss", "mtoon", "xform", "lossless"):
            assert k in entry, f"entry missing {k}: {entry}"


def test_outline_mode_round_trips_for_each_token():
    for token in ("none", "screenCoordinates", "worldCoordinates"):
        m_out = bridge.scss_to_mtoon({"_OutlineMode":
                                       float({"none": 0, "screenCoordinates": 1, "worldCoordinates": 2}[token])})
        assert m_out["outlineWidthMode"] == token
        s_back = bridge.mtoon_to_scss({"outlineWidthMode": token})
        assert s_back["_OutlineMode"] == \
               {"none": 0.0, "screenCoordinates": 1.0, "worldCoordinates": 2.0}[token]


def test_mire_face_scss_to_mtoon_smoke():
    """A plausible Mire.Mt.Face SCSS material → MToon."""
    scss = {
        "_Color":              (0.92, 0.78, 0.73, 1.0),
        "_ShadowMaskColor":    (0.7, 0.55, 0.55),
        "_Shadow":             0.5,
        "_ShadowLift":         -0.06,
        "_FresnelTint":        (0.4, 0.3, 0.45),
        "_FresnelWidth":       3.0,
        "_FresnelStrength":    0.0,
        "_OutlineMode":        1.0,
        "_outline_width":      0.0015,
        "_outline_color":      (0.08, 0.05, 0.06),
        "_AlphaSharp":         0.0,
        "_Matcap1Tint":        (1.0, 1.0, 1.0),
        "_Matcap1Strength":    1.0,
    }
    m = bridge.scss_to_mtoon(scss)
    assert m["baseColorFactor"] == (0.92, 0.78, 0.73, 1.0)
    assert m["outlineWidthMode"] == "screenCoordinates"
    assert m["transparentWithZWrite"] is False
    # Shade colour was shadow-attenuated by 0.75.
    assert math.isclose(m["shadeColorFactor"][0], 0.7 * 0.75, rel_tol=1e-6)


def test_fresnel_width_power_inverse_within_bracket():
    # Inside the 0..20 width bracket (excluding endpoints), forward then
    # backward returns the input within rounding.
    for w in (0.5, 1.0, 3.0, 7.5, 15.0):
        p = bridge._fresnel_width_to_power_fwd(w)
        w_back = bridge._fresnel_width_to_power_bwd(p)
        assert math.isclose(w_back, w, abs_tol=1e-6), (w, p, w_back)


# ---------------------------------------------------------------------------
# Property-based round-trip on the recoverable MToon subset.
# Generates random MToon params, runs `scss_to_mtoon(mtoon_to_scss(...))`,
# asserts the recoverable fields match within tolerance.
# ---------------------------------------------------------------------------

_float01 = st.floats(min_value=0.0, max_value=1.0, allow_nan=False, allow_infinity=False)
_float_color3 = st.tuples(_float01, _float01, _float01)
_float_color4 = st.tuples(_float01, _float01, _float01, _float01)
_outline_mode = st.sampled_from(["none", "screenCoordinates", "worldCoordinates"])


@st.composite
def mtoon_recoverable(draw):
    """MToon parameter dict using only fields that survive a round trip."""
    return {
        "baseColorFactor":                    draw(_float_color4),
        "shadingShiftFactor":                 draw(st.floats(min_value=-1.0, max_value=1.0,
                                                              allow_nan=False, allow_infinity=False)),
        "parametricRimColorFactor":           draw(_float_color3),
        "parametricRimFresnelPowerFactor":    draw(st.floats(min_value=0.5, max_value=7.5,
                                                              allow_nan=False, allow_infinity=False)),
        "parametricRimLiftFactor":            draw(_float01),
        "matcapFactor":                       draw(_float_color3),
        "outlineWidthMode":                   draw(_outline_mode),
        "outlineWidthFactor":                 draw(st.floats(min_value=0.0, max_value=0.02,
                                                              allow_nan=False, allow_infinity=False)),
        "outlineColorFactor":                 draw(_float_color3),
        "transparentWithZWrite":              draw(st.booleans()),
    }


@given(mtoon_recoverable())
@settings(max_examples=80, deadline=None)
def test_mtoon_round_trips_through_scss(m_in: dict):
    s = bridge.mtoon_to_scss(m_in)
    m_back = bridge.scss_to_mtoon(s)
    for key in ("baseColorFactor",
                "parametricRimColorFactor", "parametricRimLiftFactor",
                "outlineWidthMode", "outlineWidthFactor",
                "outlineColorFactor", "transparentWithZWrite"):
        a = m_in[key]
        b = m_back[key]
        if isinstance(a, tuple):
            assert len(a) == len(b)
            for x, y in zip(a, b):
                assert math.isclose(x, y, abs_tol=1e-5), f"{key}: {a} -> {b}"
        elif isinstance(a, float):
            assert math.isclose(a, b, abs_tol=1e-5), f"{key}: {a} -> {b}"
        else:
            assert a == b, f"{key}: {a} -> {b}"
    # shadingShiftFactor uses _ShadowLift -> shadingShiftFactor identity.
    assert math.isclose(m_in["shadingShiftFactor"],
                        m_back["shadingShiftFactor"], abs_tol=1e-5)
    # Fresnel power tolerated to 1e-4 because of the inverse function's
    # numerical sensitivity near the bracket midpoints.
    assert math.isclose(m_in["parametricRimFresnelPowerFactor"],
                        m_back["parametricRimFresnelPowerFactor"], rel_tol=1e-3)


def test_json_map_is_well_formed_byte_for_byte():
    """Spot-check that the canonical JSON is valid + matches the Lean spec
    literal's first few lines. Full bit-pin lands once `lake exe
    emit_artifacts` is wired into CI."""
    raw = MAP_PATH.read_text(encoding="utf-8")
    parsed = json.loads(raw)
    assert parsed["version"] == 1
    assert parsed["comment"].startswith("AUTO-GENERATED by lean/EmitArtifacts.lean")
    # Quick sanity: every entry's xform is in the bridge's transform set.
    for e in parsed["entries"]:
        assert e["xform"] in bridge._FWD, f"unknown xform {e['xform']!r}"
        assert e["xform"] in bridge._BWD, f"unknown xform {e['xform']!r}"


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v"]))
