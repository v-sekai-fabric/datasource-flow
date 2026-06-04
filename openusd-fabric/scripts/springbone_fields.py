"""
VRM 0.x <-> 1.0 springbone field rename + clamp (CHI-252).

Copyright 2026 The openusd-fabric authors.
SPDX-License-Identifier: MIT

Loads maps/springbone_fields_map.json (emitted by
lean/Fabric/VrmUpgrade/SpringBoneFields.lean) and exposes:

  * vrm0_to_vrm1_spring(fields_dict) -> dict
  * vrm1_to_vrm0_spring(fields_dict) -> dict
  * SPRINGBONE_VRM1_DEFAULTS    — per-field default value
  * SPRINGBONE_VRM1_BOUNDS      — per-field (min, max) tuples

Unknown VRM 0.x fields are dropped silently (per the spec, they have no
1.0 representation). Out-of-range values are clamped to the 1.0 bound.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

_MAP_PATH = (Path(__file__).resolve().parent.parent
             / "maps" / "springbone_fields_map.json")
_DATA = json.loads(_MAP_PATH.read_text(encoding="utf-8"))

_RENAME_0_TO_1: dict[str, str] = {e["vrm0"]: e["vrm1"] for e in _DATA["fields"]}
_RENAME_1_TO_0: dict[str, str] = {e["vrm1"]: e["vrm0"] for e in _DATA["fields"]}
_BOUNDS: dict[str, tuple[float | None, float | None]] = {
    e["vrm1"]: (e.get("min"), e.get("max")) for e in _DATA["fields"]
}

SPRINGBONE_VRM1_DEFAULTS: dict[str, float] = {e["vrm1"]: e["default"]
                                              for e in _DATA["fields"]}
SPRINGBONE_VRM1_BOUNDS = _BOUNDS


def _clamp(v: float, lo: float | None, hi: float | None) -> float:
    if lo is not None and v < lo:
        v = lo
    if hi is not None and v > hi:
        v = hi
    return v


def vrm0_to_vrm1_spring(vrm0_fields: dict[str, Any]) -> dict[str, Any]:
    """Translate a flat VRM 0.x springbone field dict to VRM 1.0.

    Renames + clamps to the 1.0 legal range. Unknown 0.x keys are
    dropped silently.
    """
    out: dict[str, Any] = {}
    for k0, v in vrm0_fields.items():
        k1 = _RENAME_0_TO_1.get(k0)
        if k1 is None:
            continue
        lo, hi = _BOUNDS[k1]
        out[k1] = _clamp(float(v), lo, hi)
    return out


def vrm1_to_vrm0_spring(vrm1_fields: dict[str, Any]) -> dict[str, Any]:
    """Translate a flat VRM 1.0 springbone field dict back to VRM 0.x."""
    out: dict[str, Any] = {}
    for k1, v in vrm1_fields.items():
        k0 = _RENAME_1_TO_0.get(k1)
        if k0 is None:
            continue
        lo, hi = _BOUNDS[k1]
        out[k0] = _clamp(float(v), lo, hi)
    return out
