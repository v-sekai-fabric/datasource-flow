"""
Runtime validator for the V-Sekai codeless USD plugin.

Copyright 2026 The openusd-fabric authors.
SPDX-License-Identifier: MIT

Opens a fixture stage with PXR_PLUGINPATH_NAME pointing at schema/ and
asserts:

* The three API-schema TfTypes are registered (plugin loaded).
* prim.HasAPI(...) reports True on the fixture's flagged prims and
  False on the negative-case prims.
* The v_sekai:* attributes the fixture sets are readable and round-trip
  to the documented value.

Run standalone, separately from usdchecker, because usdchecker validates
USD syntax and composition but does not exercise the plugin's HasAPI
machinery. Both gates need to pass for the schema to be load-bearing.

Usage:

    python scripts/check_schemas.py [--schema-dir schema] \
        [--fixture tests/fixtures/minimal_with_schemas.usda]

Exits non-zero on any assertion failure; intended for CI.
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent


def _set_plugin_path(schema_dir: Path) -> None:
    schema_dir = schema_dir.resolve()
    if not (schema_dir / "plugInfo.json").exists():
        raise SystemExit(f"plugInfo.json not found under {schema_dir}")
    current = os.environ.get("PXR_PLUGINPATH_NAME", "")
    parts = [p for p in current.split(os.pathsep) if p]
    if str(schema_dir) not in parts:
        parts.insert(0, str(schema_dir))
        os.environ["PXR_PLUGINPATH_NAME"] = os.pathsep.join(parts)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("--schema-dir", default=str(REPO_ROOT / "schema"))
    parser.add_argument("--fixture",
                        default=str(REPO_ROOT / "tests" / "fixtures" /
                                    "minimal_with_schemas.usda"))
    args = parser.parse_args()

    # Plugin path must be set before pxr is imported.
    _set_plugin_path(Path(args.schema_dir))

    from pxr import Tf, Usd

    failures: list[str] = []

    def expect(cond: bool, message: str) -> None:
        if not cond:
            failures.append(message)

    # 1. Plugin registration. USD 25.x exposes codeless schemas as TfType
    #    entries; USD 26.x deliberately does not (only schemaRegistry-based
    #    lookup works). We check both paths so the script is portable
    #    across both stacks.
    registry = Usd.SchemaRegistry()
    for type_name in (
        "VSekaiMToonAPI",
        "VSekaiMToonScssExtensionAPI",
        "VSekaiSpringBoneAPI",
        "VSekaiSpringBoneColliderAPI",
    ):
        ok = False
        # Path 1: USD 25.x — TfType lookup.
        if not Tf.Type.FindByName(type_name).isUnknown:
            ok = True
        # Path 2: USD 26.x+ — schemaRegistry identifies applied APIs.
        if not ok and registry.IsAppliedAPISchema(type_name):
            ok = True
        # Path 3: ultimate fallback — try ApplyAPI on a throwaway prim.
        # If the schema is genuinely missing, this raises; catch + record.
        if not ok:
            try:
                tmp_stage = Usd.Stage.CreateInMemory()
                tmp_prim = tmp_stage.DefinePrim("/_probe", "Material")
                tmp_prim.ApplyAPI(type_name)
                if tmp_prim.HasAPI(type_name):
                    ok = True
            except Exception:
                pass
        expect(
            ok,
            f"Schema {type_name!r} not registered — check schema/plugInfo.json"
            " (needs schemaIdentifier per Types entry) and that"
            " PXR_PLUGINPATH_NAME includes the schema directory.",
        )

    # 2. Fixture round-trip — HasAPI on the flagged prims, attributes read back.
    stage = Usd.Stage.Open(args.fixture)
    expect(stage is not None, f"Could not open fixture {args.fixture}")
    if stage is None:
        for line in failures:
            print(f"FAIL: {line}", file=sys.stderr)
        return 1

    face = stage.GetPrimAtPath("/Avatar/Materials/Face_MToon")
    body = stage.GetPrimAtPath("/Avatar/Materials/Body_PBR")
    spring = stage.GetPrimAtPath("/Avatar/Skel/SpringBoneRoot")
    collider = stage.GetPrimAtPath("/Avatar/Skel/ChestCollider")

    expect(face.HasAPI("VSekaiMToonAPI"),
           "Face_MToon should have VSekaiMToonAPI applied")
    expect(not body.HasAPI("VSekaiMToonAPI"),
           "Body_PBR should not have VSekaiMToonAPI applied")
    expect(spring.HasAPI("VSekaiSpringBoneAPI"),
           "SpringBoneRoot should have VSekaiSpringBoneAPI applied")
    expect(collider.HasAPI("VSekaiSpringBoneColliderAPI"),
           "ChestCollider should have VSekaiSpringBoneColliderAPI applied")

    shade = face.GetAttribute("v_sekai:mtoon:shadeColorFactor").Get()
    expect(shade is not None and tuple(round(c, 3) for c in shade) ==
           (0.85, 0.75, 0.7),
           f"Face_MToon shadeColorFactor expected (0.85, 0.75, 0.7), got {shade}")

    stiffness = spring.GetAttribute("v_sekai:springBone:stiffness").Get()
    expect(stiffness is not None and abs(stiffness - 0.8) < 1e-6,
           f"SpringBoneRoot stiffness expected 0.8, got {stiffness}")

    shape = collider.GetAttribute("v_sekai:springBone:collider:shape").Get()
    expect(shape == "capsule",
           f"ChestCollider shape expected 'capsule', got {shape!r}")

    if failures:
        for line in failures:
            print(f"FAIL: {line}", file=sys.stderr)
        return 1

    print("openusd-fabric: all V-Sekai schema checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
