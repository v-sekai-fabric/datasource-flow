"""
Compile shaders/outline_jfa/*.slang to GLSL for the engine consumers.

Copyright 2026 The openusd-fabric authors.
SPDX-License-Identifier: MIT

Invokes `slangc` once per .slang source under $OUTLINE_JFA_SRC, writing
GLSL outputs under $OUTLINE_JFA_OUT/. The .slang sources are the
single source of truth for the JFA outline algorithm; the engine
consumers (Godot CompositorEffect, Unity Renderer Feature, Hydra task)
read from the emitted directory and never hand-edit GLSL.

Usage (via pixi):

    pixi run -e slang emit-shader-glsl

Direct usage (slangc must be on PATH):

    OUTLINE_JFA_SRC=shaders/outline_jfa \
    OUTLINE_JFA_OUT=build/shaders/outline_jfa \
        python scripts/emit_shader_glsl.py
"""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path


def main() -> int:
    src = Path(os.environ.get("OUTLINE_JFA_SRC", "shaders/outline_jfa"))
    out = Path(os.environ.get("OUTLINE_JFA_OUT", "build/shaders/outline_jfa"))
    out.mkdir(parents=True, exist_ok=True)

    slang_files = sorted(src.glob("*.slang"))
    if not slang_files:
        print(f"no .slang files under {src}", file=sys.stderr)
        return 1

    failures: list[str] = []
    for slang in slang_files:
        glsl = out / (slang.stem + ".glsl")
        # slangc flags:
        #   -target glsl        emit GLSL
        #   -profile glsl_460   compute-shader-capable profile
        #   -stage compute      explicit stage (each kernel here is compute)
        #   -entry main         every kernel uses main()
        #   -o <path>           output file
        cmd = [
            "slangc", str(slang),
            "-target", "glsl",
            "-profile", "glsl_460",
            "-stage", "compute",
            "-entry", "main",
            "-o", str(glsl),
        ]
        print("$", " ".join(cmd))
        try:
            subprocess.run(cmd, check=True)
        except FileNotFoundError:
            print("error: slangc not on PATH; run via `pixi run -e slang` "
                  "or install shader-slang from conda-forge.", file=sys.stderr)
            return 2
        except subprocess.CalledProcessError as exc:
            failures.append(f"{slang}: slangc exited {exc.returncode}")

    if failures:
        for line in failures:
            print(f"FAIL: {line}", file=sys.stderr)
        return 1
    print(f"emitted {len(slang_files)} GLSL files under {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
