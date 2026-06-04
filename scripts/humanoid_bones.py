"""
VRM humanoid bone enum + 0.x <-> 1.0 mapping (CHI-252).

Copyright 2026 The openusd-fabric authors.
SPDX-License-Identifier: MIT

Loads maps/humanoid_bones_map.json (emitted by
lean/Fabric/VrmUpgrade/HumanoidBones.lean) and exposes:

  * VRM1_HUMANOID_BONES       — the canonical 55-entry enum
  * VRM1_REQUIRED_BONES       — the subset VRM 1.0 marks required
  * vrm0_to_vrm1_bone(name)   — Optional[str]
  * vrm1_to_vrm0_bone(name)   — Optional[str]

Today the 0.x and 1.0 names are identical for every bone (the spec
change in 1.0 was structural, not nominal). The functions exist as the
seam where any future divergence lands.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Optional

_MAP_PATH = (Path(__file__).resolve().parent.parent
             / "maps" / "humanoid_bones_map.json")


def _load() -> dict:
    return json.loads(_MAP_PATH.read_text(encoding="utf-8"))


_DATA = _load()
_VRM0_TO_VRM1: dict[str, str] = {e["vrm0"]: e["vrm1"] for e in _DATA["bones"]}
_VRM1_TO_VRM0: dict[str, str] = {e["vrm1"]: e["vrm0"] for e in _DATA["bones"]}
VRM1_HUMANOID_BONES: list[str] = [e["vrm1"] for e in _DATA["bones"]]
VRM1_REQUIRED_BONES: list[str] = [e["vrm1"] for e in _DATA["bones"] if e["required"]]


def vrm0_to_vrm1_bone(name: str) -> Optional[str]:
    return _VRM0_TO_VRM1.get(name)


def vrm1_to_vrm0_bone(name: str) -> Optional[str]:
    return _VRM1_TO_VRM0.get(name)


def is_required_in_vrm1(name: str) -> bool:
    return name in VRM1_REQUIRED_BONES
