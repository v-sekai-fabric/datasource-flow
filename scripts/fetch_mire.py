"""
End-to-end integration probe against V-Sekai's `Mille 'Mire' Feuille`.

Copyright 2026 The openusd-fabric authors.
SPDX-License-Identifier: MIT

Downloads (shallow-clones) the mesh-mille-mire-feuille repo, opens
Mire.blend in Blender headless, exports it to USD, runs the V-Sekai
post-export hook, and reports a summary.

The script never commits the .blend or the exported .usda — both are
written under a working directory the user controls (default
`build/mire/`). It is invoked via `pixi run fetch-mire`.

As of the time of writing, the upstream Mire.blend ships **without**
V-Sekai schema markers (no spring_bone1 config, no `mtoon1.enabled`
on any material — the MToon assignments and springbone topology live
in the bundled `.unitypackage` for the Unity / VRChat path). So the
hook is currently a no-op on Mire. The summary surfaces that fact so a
regression on the negative path (hook accidentally stamping schemas it
shouldn't) breaks CI when this probe is wired up.

Once the upstream marker workflow lands (or once the `.unitypackage`
extraction step is written), the same script becomes the positive
integration test.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO_URL = "https://github.com/V-Sekai-fire/mesh-mille-mire-feuille.git"
DEFAULT_WORKDIR = Path("build/mire").resolve()


def clone_or_update(workdir: Path) -> Path:
    """Shallow-clone (or `git pull`) the Mire repo into workdir/repo."""
    repo_dir = workdir / "repo"
    if repo_dir.exists() and (repo_dir / ".git").exists():
        print(f"== updating existing clone at {repo_dir} ==")
        subprocess.run(["git", "-C", str(repo_dir), "pull", "--ff-only"],
                       check=True)
    else:
        workdir.mkdir(parents=True, exist_ok=True)
        if repo_dir.exists():
            shutil.rmtree(repo_dir)
        print(f"== shallow-cloning {REPO_URL} -> {repo_dir} ==")
        subprocess.run(["git", "clone", "--depth", "1", REPO_URL, str(repo_dir)],
                       check=True)
    return repo_dir


def export_via_blender(blend_path: Path, out_usd: Path) -> int:
    """Open the .blend headless and export to USD, then run the hook."""
    blender = os.environ.get("BLENDER", "blender")
    repo_root = Path(__file__).resolve().parent.parent
    hook_path  = repo_root / "blender" / "post_export_hook.py"
    schema_dir = repo_root / "schema"

    # Inline Python: open the .blend, export USD, then exec the hook
    # as a subprocess against the exported file. Single Blender
    # invocation keeps cold-start overhead to one.
    driver = rf"""
import bpy, os, sys
bpy.ops.wm.open_mainfile(filepath=r"{blend_path}")
os.makedirs(r"{out_usd.parent}", exist_ok=True)
bpy.ops.wm.usd_export(
    filepath=r"{out_usd}",
    selected_objects_only=False,
    visible_objects_only=True,
    export_materials=True,
    export_animation=False,
)
print("USD_EXPORT_OK", r"{out_usd}")
"""
    print(f"== exporting {blend_path.name} via Blender ==")
    cmd = [blender, "--background", "--python-expr", driver]
    res = subprocess.run(cmd, capture_output=True, text=True)
    sys.stdout.write(res.stdout)
    sys.stderr.write(res.stderr)
    if "USD_EXPORT_OK" not in res.stdout:
        print("error: Blender did not report a successful USD export",
              file=sys.stderr)
        return 1

    print(f"\n== running V-Sekai post-export hook ==")
    env = os.environ.copy()
    env["PXR_PLUGINPATH_NAME"] = (
        str(schema_dir) + os.pathsep + env.get("PXR_PLUGINPATH_NAME", "")
    )
    res2 = subprocess.run(
        [sys.executable, str(hook_path), "--in", str(out_usd)],
        capture_output=True, text=True, env=env)
    sys.stdout.write(res2.stdout)
    sys.stderr.write(res2.stderr)
    return res2.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("--workdir", type=Path, default=DEFAULT_WORKDIR,
                        help="Where the clone + exported USD live.")
    parser.add_argument("--variant", choices=("pc", "quest"), default="pc",
                        help="`pc` -> Mire.blend, `quest` -> MireQuest.blend.")
    args = parser.parse_args()

    repo = clone_or_update(args.workdir.resolve())
    blend_name = "Mire.blend" if args.variant == "pc" else "MireQuest.blend"
    blend = repo / blend_name
    if not blend.exists():
        print(f"error: {blend} not present in clone", file=sys.stderr)
        return 1
    out_usd = args.workdir / f"{blend.stem}.usda"
    return export_via_blender(blend, out_usd)


if __name__ == "__main__":
    sys.exit(main())
