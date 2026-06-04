"""
Blender helper: import the Macbeth ColorChecker fixture with bindings.

Copyright 2026 The openusd-fabric authors.
SPDX-License-Identifier: MIT

Blender 4.x's USD importer (Blender's bundled USD 25.08, as of writing)
will create UsdShade Material prims correctly when called with
`import_all_materials=True`, but does **not** follow the
`material:binding` rel on geometry — so each imported cube ends up
with empty material slots even though the 24 Materials exist in
`bpy.data.materials`.

This helper does the binding step explicitly, matching by name:
Macbeth patch `P<NN>_<name>` is bound to material `M_<name>`.

Usage from Blender's text editor or `blender --background --python`:

    import bpy
    import sys
    sys.path.insert(0, r"E:/openusd-fabric/scripts")
    import blender_macbeth_import as helper
    helper.import_and_bind(r"E:/openusd-fabric/tests/fixtures/macbeth_colorchecker.usda")

After this runs, every cube has its matching Material in slot 0 and
the viewport's "Material Preview" shading shows the correct patch
colour.
"""

from __future__ import annotations

import re
import sys
from typing import Optional

# `bpy` is only importable from Blender; guarded so this file can also
# be syntax-checked under any Python.
try:
    import bpy  # type: ignore
except ImportError:
    bpy = None  # type: ignore


_PATCH_NAME_RE = re.compile(r"^P\d+_(.+)$")


def _material_name_for_object(obj_name: str) -> Optional[str]:
    m = _PATCH_NAME_RE.match(obj_name)
    if not m:
        return None
    return "M_" + m.group(1)


def import_and_bind(usd_path: str) -> dict[str, int]:
    """Import the USD and bind each cube to its name-matched Material.

    Returns a small counts dict for logging / tests.
    """
    if bpy is None:
        raise RuntimeError("import_and_bind must run inside Blender (bpy missing)")

    bpy.ops.wm.usd_import(
        filepath=usd_path,
        import_materials=True,
        import_all_materials=True,
        import_usd_preview=True,
        mtl_purpose="MTL_FULL",
        property_import_mode="ALL",
    )

    counts = {"objects": 0, "bound": 0, "unmatched": 0}
    for obj in bpy.data.objects:
        if obj.type != "MESH":
            continue
        counts["objects"] += 1
        mat_name = _material_name_for_object(obj.name)
        if mat_name is None:
            continue
        mat = bpy.data.materials.get(mat_name)
        if mat is None:
            counts["unmatched"] += 1
            continue
        if obj.data.materials:
            obj.data.materials[0] = mat
        else:
            obj.data.materials.append(mat)
        counts["bound"] += 1
    return counts


if __name__ == "__main__":
    if bpy is None:
        print("error: run this inside Blender", file=sys.stderr)
        sys.exit(2)
    if len(sys.argv) < 2:
        # When invoked via `blender --background --python script.py -- <usd>`,
        # arguments after `--` survive in sys.argv. Otherwise fall back to
        # the default fixture path.
        path = (r"E:/openusd-fabric/tests/fixtures/macbeth_colorchecker.usda")
    else:
        # Blender forwards everything after `--`
        sep = sys.argv.index("--") if "--" in sys.argv else 0
        rest = sys.argv[sep + 1:] if sep else []
        path = rest[0] if rest else (
            r"E:/openusd-fabric/tests/fixtures/macbeth_colorchecker.usda"
        )
    res = import_and_bind(path)
    print(f"blender_macbeth_import: {res}")
