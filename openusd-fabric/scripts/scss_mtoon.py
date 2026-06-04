"""
Bidirectional SCSS ↔ VRM 1.0 MToon parameter bridge.

Copyright 2026 The openusd-fabric authors.
SPDX-License-Identifier: MIT

Consumes maps/scss_mtoon_map.json (the canonical mapping table) and
applies the named per-entry value transforms to translate a material
parameter dict in either direction:

    scss_to_mtoon(scss_params: dict) -> dict
    mtoon_to_scss(mtoon_params: dict) -> dict

The transforms (`xform` field on each entry) mirror the Lean spec in
lean/Fabric/VrmUpgrade/ScssMToon.lean exactly. Adding a new mapping is a
one-line edit to the Lean spec + emit step; both consumers (this module
and the Unity C# helper) pick it up via the JSON.

Used by:
  * blender/post_export_hook.py on the reverse path (Godot -> USD with
    VRM 1.0 upgrade applied at the boundary) to translate SCSS material
    parameters surfaced as Blender custom properties into the
    VSekaiMToonAPI schema attributes.
  * CI: round-trip tests in tests/slang_validate/test_scss_mtoon.py.
"""

from __future__ import annotations

import json
import math
from pathlib import Path
from typing import Any

_MAP_PATH_DEFAULT = (Path(__file__).resolve().parent.parent
                     / "maps" / "scss_mtoon_map.json")


def _load_map(map_path: Path = _MAP_PATH_DEFAULT) -> dict[str, Any]:
    with open(map_path, encoding="utf-8") as f:
        return json.load(f)


# ---------------------------------------------------------------------------
# Value transforms. Each transform is a pair (forward, backward); the
# forward direction takes SCSS-side native value(s) and returns MToon
# native value(s); backward is the inverse (with documented lossy bits).
# ---------------------------------------------------------------------------

def _color4_identity_fwd(rgba: tuple) -> tuple:
    return tuple(float(c) for c in rgba)
def _color4_identity_bwd(rgba: tuple) -> tuple:
    return tuple(float(c) for c in rgba)

def _color3_identity_fwd(rgb: tuple) -> tuple:
    return tuple(float(c) for c in rgb[:3])
def _color3_identity_bwd(rgb: tuple) -> tuple:
    return tuple(float(c) for c in rgb[:3])

def _float_identity_fwd(v: float) -> float:
    return float(v)
def _float_identity_bwd(v: float) -> float:
    return float(v)

# SCSS shade lift: _ShadowMaskColor * (1 - _Shadow*0.5) -> shadeColorFactor
def _color3_via_shadow_fwd(scss_color: tuple, scss_shadow: float = 0.5) -> tuple:
    f = 1.0 - float(scss_shadow) * 0.5
    return tuple(float(c) * f for c in scss_color[:3])
def _color3_via_shadow_bwd(mtoon_shade: tuple) -> tuple:
    # The Lean spec pins _Shadow=0.5 on the reverse path; multiplication
    # factor becomes 0.75. We invert by dividing, capping at 1.0.
    f = 0.75
    return tuple(min(1.0, float(c) / f) for c in mtoon_shade[:3])

# SCSS fresnel width <-> MToon Fresnel power: 8 / (1 + w), inverse.
def _fresnel_width_to_power_fwd(w: float) -> float:
    w = max(0.0, min(20.0, float(w)))
    if w <= 0.0:
        return 8.0
    if w >= 20.0:
        return 0.25
    return 8.0 / (1.0 + w)

def _fresnel_width_to_power_bwd(p: float) -> float:
    p = max(0.25, min(8.0, float(p)))
    if p <= 0.25:
        return 20.0
    if p >= 8.0:
        return 0.0
    return (8.0 / p) - 1.0

# SCSS matcap1Tint * matcap1Strength -> matcapFactor; backward pins strength=1.0
def _color3_times_strength_fwd(tint: tuple, strength: float = 1.0) -> tuple:
    s = float(strength)
    return tuple(float(c) * s for c in tint[:3])
def _color3_times_strength_bwd(matcap: tuple) -> tuple:
    return tuple(float(c) for c in matcap[:3])  # strength pinned at 1.0

# Outline mode enum / token.
_OUTLINE_MODE_TOKENS = ["none", "screenCoordinates", "worldCoordinates"]
def _outline_mode_enum_fwd(scss_enum: float) -> str:
    i = int(round(float(scss_enum)))
    return _OUTLINE_MODE_TOKENS[i] if 0 <= i < len(_OUTLINE_MODE_TOKENS) else "screenCoordinates"
def _outline_mode_enum_bwd(mtoon_token: str) -> float:
    try:
        return float(_OUTLINE_MODE_TOKENS.index(mtoon_token))
    except ValueError:
        return 1.0  # default to screenCoordinates

# Alpha sharp enum <-> z-write bool. SCSS: 0=dithered, 1=cutout, 2=zwrite.
def _alpha_sharp_to_zwrite_fwd(scss_alpha_sharp: float) -> bool:
    return float(scss_alpha_sharp) >= 1.5
def _alpha_sharp_to_zwrite_bwd(z: bool) -> float:
    return 2.0 if z else 0.0


_FWD = {
    "color4_identity":         _color4_identity_fwd,
    "color3_identity":         _color3_identity_fwd,
    "float_identity":          _float_identity_fwd,
    "color3_via_shadow":       _color3_via_shadow_fwd,
    "fresnel_width_to_power":  _fresnel_width_to_power_fwd,
    "color3_times_strength":   _color3_times_strength_fwd,
    "outline_mode_enum":       _outline_mode_enum_fwd,
    "alpha_sharp_to_zwrite":   _alpha_sharp_to_zwrite_fwd,
}

_BWD = {
    "color4_identity":         _color4_identity_bwd,
    "color3_identity":         _color3_identity_bwd,
    "float_identity":          _float_identity_bwd,
    "color3_via_shadow":       _color3_via_shadow_bwd,
    "fresnel_width_to_power":  _fresnel_width_to_power_bwd,
    "color3_times_strength":   _color3_times_strength_bwd,
    "outline_mode_enum":       _outline_mode_enum_bwd,
    "alpha_sharp_to_zwrite":   _alpha_sharp_to_zwrite_bwd,
}


import _upgrade_table as _runner  # noqa: E402


# Per-xform extras the forward path needs (color3_via_shadow needs
# `_Shadow`; color3_times_strength needs `_Matcap1Strength`). Wired so
# the shared runner can splat them as kwargs.
_FWD_EXTRAS = {
    "color3_via_shadow":     {"scss_shadow":  "_Shadow"},
    "color3_times_strength": {"strength":     "_Matcap1Strength"},
}

# Reverse-direction SCSS knobs the bridge pins to documented defaults.
_BWD_DEFAULTS = {"_Shadow": 0.5, "_Matcap1Strength": 1.0}


def scss_to_mtoon(scss_params: dict[str, Any],
                  *, map_path: Path = _MAP_PATH_DEFAULT) -> dict[str, Any]:
    """Translate a SCSS material parameter dict to its MToon counterpart.

    `scss_params` is keyed by the SCSS property names (`_Color`,
    `_FresnelWidth`, ...). Unknown keys (anything in `lossy_scss_only`)
    are skipped silently. Missing keys keep MToon at the spec default.
    """
    m = _load_map(map_path)
    return _runner.run_forward(scss_params, m["entries"], _FWD,
                                src_key="scss", dst_key="mtoon",
                                extra_inputs=_FWD_EXTRAS)


def mtoon_to_scss(mtoon_params: dict[str, Any],
                  *, map_path: Path = _MAP_PATH_DEFAULT) -> dict[str, Any]:
    """Translate an MToon material parameter dict back to SCSS naming.

    The reverse direction pins SCSS-only knobs (`_Shadow`,
    `_Matcap1Strength`) at the spec-documented defaults so the round
    trip is bit-stable on the recoverable subset.
    """
    m = _load_map(map_path)
    return _runner.run_reverse(mtoon_params, m["entries"], _BWD,
                                src_key="mtoon", dst_key="scss",
                                defaults=_BWD_DEFAULTS)


def reload_map(map_path: Path = _MAP_PATH_DEFAULT) -> dict[str, Any]:
    """Force-reload the JSON map (for tests / interactive use)."""
    return _load_map(map_path)


if __name__ == "__main__":
    import sys
    print(json.dumps(_load_map(), indent=2), file=sys.stdout)
