"""
SCons tool: godot_scn_writers

Wires the slangc-lowered Godot .scn writer outputs into libidtx_core.

The Lean spec at openusd-fabric/lean/Fabric/Serialization/GodotScn.lean
defines a `bake_scn_kernel` compute entry that calls every writer
helper, so slangc keeps the whole library alive through dead-code
elimination. The lowered targets are committed at:

  core/share/godot_scn/godot_scn.cpp   (host-callable C++, 779 lines)
  core/share/godot_scn/godot_scn.metal (Metal kernel, 739 lines)
  core/share/godot_scn/godot_scn.spv   (SPIR-V binary, ~8 KB)
  core/share/godot_scn/godot_scn.spv.txt  (SPIR-V assembly, audit form)

Why three targets and not just CPU:
  CPU   — works HEADLESS. No GPU required. Every CI runner regardless
          of hardware can compile and execute the .cpp. This is also
          what libidtx_core links into the host binary for actual .scn
          writing today.
  Metal — exercises the macOS CI/CD path on Apple Silicon GitHub
          Actions runners. Compiles via `xcrun -sdk macosx metal`.
          Validates that the kernel survives Apple's GPU lowering.
  SPIR-V — normal GPU execution on Windows / Linux via Vulkan, AND
          runs on Mac CI through MoltenVK (Vulkan → Metal translation).
          So Mac CI validates both Metal and Vulkan paths; Linux/Win
          CI validates SPIR-V directly. Three independent lowerings ⇒
          three sanity checks on the Lean spec's well-formedness.

Neither `lake` nor `slangc` needs to be on PATH at build time — the
outputs are in git. A future CI job (TODO) re-runs the pipeline and
diffs against what's committed to catch drift.

SCons wiring:
- Adds core/share/godot_scn/godot_scn.cpp to the libidtx_core source
  list so it compiles into both the shared lib and the static archive.
- Defines IDTX_GODOT_SCN_AVAILABLE so idtx_export_scn.cpp delegates
  to the slangc-emitted bake_scn_kernel.
- Adds the slang prelude include dir (resolved via the `slangc`
  binary's neighbouring `include/` directory) to CPPPATH so
  `<slang-cpp-prelude.h>` resolves at compile time. If slangc is not
  found, falls back to expecting the prelude on the system include
  path — common when slang was installed via package manager.

Usage in SConstruct (after BuildGodotCPP, before BuildIdtxCore):
    env.GenerateGodotScnWriters()
"""
import os
import shutil


def generate(env):
    env.AddMethod(_generate_godot_scn_writers, 'GenerateGodotScnWriters')


def exists(env):
    return True


def _slang_include_dir():
    """Locate slang's `include/` directory (for slang-cpp-prelude.h).

    The host-callable C++ output `#include`s `<slang-cpp-prelude.h>`
    which lives next to the slangc binary as `../include/`. Return
    None if slangc isn't on PATH; the build will then expect the
    prelude to be on the system include path.
    """
    slangc = shutil.which("slangc")
    if slangc is None:
        return None
    bindir = os.path.dirname(os.path.realpath(slangc))
    candidate = os.path.normpath(os.path.join(bindir, "..", "include"))
    if os.path.isfile(os.path.join(candidate, "slang-cpp-prelude.h")):
        return candidate
    return None


def _generate_godot_scn_writers(env):
    print("Checking godot_scn writers (committed slangc outputs)...")

    out_cpp = "core/share/godot_scn/godot_scn.cpp"
    out_h   = "core/share/godot_scn"  # contains godot_scn.metal/.spv/.cpp

    if not os.path.isfile(out_cpp):
        print(f"  [godot_scn] {out_cpp} not in git; .scn export returns 99")
        env['idtx_godot_scn_available'] = False
        return None

    prelude_dir = _slang_include_dir()
    if prelude_dir is None:
        # The committed godot_scn.cpp #includes <slang-cpp-prelude.h>; with
        # no slangc on PATH and the prelude not on the system include path,
        # those translation units cannot compile. Degrade gracefully (the
        # documented contract: idtx_core_export_avatar_to_scn returns 99)
        # rather than breaking the whole libidtx_core build.
        print(f"  [godot_scn] slang prelude not located; disabling .scn writer (export returns 99)")
        env['idtx_godot_scn_available'] = False
        return None
    else:
        print(f"  [godot_scn] slang prelude: {prelude_dir}")

    env['idtx_godot_scn_available']   = True
    env['idtx_godot_scn_cpp']         = out_cpp
    env['idtx_godot_scn_include']     = out_h
    env['idtx_godot_scn_prelude_dir'] = prelude_dir
    print(f"  [godot_scn] OK — using {out_cpp}")
    return out_cpp
