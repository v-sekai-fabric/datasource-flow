# idtx-flow as Lean 4 kernels core with host adapters

Date: 2026-06-06
Scope: investigation only. This is follow-on architecture work after CHI-340's local MCP + Sandbox smoke loop; it must not become a CHI-340 first-proof dependency.

## Question

Can `v-sekai-multiplayer-fabric/idtx-flow` use Lean 4 as the kernel/spec/code-generation core while keeping runtime hosts on a host-adapters pattern, with no Lean runtime library linked into Godot/Unity/CLI/Blender?

Short answer: yes. The repository already has most of the shape.

## Existing evidence in this checkout

Repository: `E:\idtx-flow`
Branch: `v-sekai`

Lean/codegen surface:

- `openusd-fabric/lean/lakefile.lean`
  - build-time Lean package only
  - `lean_lib Fabric`
  - `lean_exe emit_artifacts`
  - depends on `LeanSlang @ v0.0.5`
- `openusd-fabric/lean/EmitArtifacts.lean`
  - emits validators, maps, and shaders into an output tree
- `openusd-fabric/lean/Fabric/Mesh/OutlineJFA.lean`
  - LeanSlang AST -> Slang source
  - `native_decide` byte-pin against committed source literal
- `openusd-fabric/lean/Fabric/Mesh/TrisToQuadsGPU.lean`
  - mesh-side kernel source for tris-to-quads matching
- `openusd-fabric/lean/Fabric/Serialization/GodotScn.lean`
  - Godot binary scene writer kernel source

Runtime/core surface:

- `core/include/idtx_core/idtx_core.h`
  - engine-agnostic C ABI
  - primitive/opaque handle API only; no Godot, Unity, STL, or Lean in ABI
- `core/include/idtx_core/idtx_scene.h`
  - engine-neutral converted USD stage tree
  - no pxr/OpenUSD types cross the host boundary
- `core/src/idtx_export_scn.cpp`
  - delegates to slangc-generated C++ only when `IDTX_GODOT_SCN_AVAILABLE` is defined
  - returns fallback code 99 when the generated writer is unavailable
- `source/idtx_core_loader.h`
  - GDExtension loads `libidtx_core` dynamically instead of linking OpenUSD into the host
- `source/converter/IdtxSceneGodotBuilder.h/.cpp`
  - Godot host adapter walks the neutral `idtx_scene_t` and builds Godot nodes
- `source/nodes/UsdStageNode3D.cpp`
  - opens USD through `idtx_core_import_scene_from_usd`, then hands the returned scene to the Godot adapter
- `scons/godot_scn_writers.py` and `scons/idtxcore.py`
  - include committed `core/share/godot_scn/godot_scn.cpp` in `libidtx_core` when the Slang prelude is found
  - no Lean runtime objects are linked into the host-facing core

Comparable patterns requested by the user:

- `E:\TOOL_cloth_dynamics\lean\EmitShaders.lean`
  - registry of LeanSlang shader modules -> writes `<name>.slang`
- `E:\TOOL_cloth_dynamics\lean\Cloth\SlangCodegen\Spmv.lean`
  - LeanSlang AST, expected emitted string, `native_decide` examples
- `E:\lean-predictive-bvh\PredictiveBVH\Codegen\CodeGen.lean`
  - Lean/AmoLean expressions -> generated C header / production kernel artifacts

## Tool verification run

From `E:\idtx-flow\openusd-fabric\lean`:

```sh
lake build
lake exe emit_artifacts /e/idtx-flow/.tmp_emit_probe
```

Result: success.

Emitted files:

- `.tmp_emit_probe/schema/spring_bone_ranges_validator.py`
- `.tmp_emit_probe/schema/spring_bone_ranges_validator.hpp`
- `.tmp_emit_probe/maps/scss_mtoon_map.json`
- `.tmp_emit_probe/maps/humanoid_bones_map.json`
- `.tmp_emit_probe/maps/springbone_fields_map.json`
- `.tmp_emit_probe/shaders/tris_to_quads_match.slang`
- `.tmp_emit_probe/shaders/jfa_step.slang`
- `.tmp_emit_probe/shaders/godot_scn.slang`

Additional test probe:

```sh
python -m pytest \
  openusd-fabric/tests/lean/test_lake_builds.py \
  openusd-fabric/tests/slang_validate/test_outline_jfa.py \
  openusd-fabric/tests/slang_validate/test_tris_to_quads_gpu.py -q
```

Result: 18 passed, 1 failed.

The failure is in `test_property_jfa_converged_state_is_idempotent`, with a Hypothesis counterexample where a converged JFA state changes after an additional stride=1 pass at the far image boundary. That is useful investigation evidence: the Lean emission loop works, but the outline JFA reference/property still needs tightening before it should be treated as a hardened kernel.

## Architecture interpretation

The clean version of the pattern is:

```text
Lean 4 kernel/spec/codegen modules
        |
        | lake build + native_decide byte-pins
        v
emit_artifacts / shader/header/map generation
        |
        | checked-in generated artifacts + drift tests
        v
libidtx_core C ABI
        |
        | opaque handles, primitive arrays, no host engine types
        v
host adapters
  - Godot GDExtension adapter
  - Unity P/Invoke adapter
  - CLI adapter
  - Blender/Python adapter
```

Lean is therefore a build-time kernel/spec authority, not a runtime dependency.

Important distinction:

- There may still be a build-time `lean_lib Fabric` so `lake build` and `emit_artifacts` can import modules sanely.
- There should be no Lean runtime library linked into `libidtx_core`, Godot, Unity, Blender, or the CLI.
- Generated artifacts should be committed or otherwise available to ordinary host builds so contributors without Lean can still build the host/core path.

## What is already aligned

1. C ABI boundary is already in the right direction.
   `idtx_core.h` and `idtx_scene.h` keep host-facing interfaces primitive and opaque.

2. Godot adapter already exists.
   `IdtxSceneGodotBuilder` is a real host adapter: it converts neutral `idtx_scene_t` nodes into Godot `Node3D` / meshes / materials / skeletons.

3. OpenUSD is already centralized.
   `idtx_scene.h` explicitly says OpenUSD stays inside `libidtx_core`; hosts do not receive `pxr` objects.

4. Shader/codegen loop already mirrors TOOL_cloth_dynamics.
   `EmitArtifacts.lean` has the same registry-and-write loop as `TOOL_cloth_dynamics/lean/EmitShaders.lean`.

5. Gall's-law friendly fallback exists.
   `.scn` export degrades to return code 99 if the generated writer path is not configured, rather than making all core/host loading fail.

## Gaps and risks

1. Some generated-to-runtime drift is not fully closed.
   `core/src/vrm_humanoid_bones.cpp` says the Lean source and JSON are not enforced against the C++ mirror today. That should become an emitted header or generated C++ file.

2. The Lean artifact registry is mixed.
   It emits JSON, Python, HPP, and Slang. This is fine, but each artifact class needs its own host-diff or compilation gate.

3. JFA hardening is incomplete.
   The current property test failure means the JFA reference/property boundary should be fixed before promoting OutlineJFA as a trusted core kernel.

4. `GodotScn.lean` currently prints a large preview during build.
   That is useful while developing, noisy for CI and host contributors. Consider moving the preview behind a debug executable or test.

5. Host adapter contracts need to be written as explicit ABIs.
   Godot has a concrete adapter. Unity/CLI/Blender should consume the same neutral C ABI, not each reinvent partial core logic.

## Product loop framing

The technical architecture should serve the smallest replayable creative loop:

1. Friends create a tiny art-game.
2. They play/observe it and capture friction.
3. We patch the art-game or tools.
4. We replay the improved version.
5. Repeat.

This makes idtx-flow valuable only insofar as it shortens that loop. Lean kernels, generated artifacts, and host adapters should therefore be selected by whether they remove concrete friction from tiny local art-game creation/replay, not by abstract completeness.

## Recommended minimal steel thread

Follow Gall's law: do not migrate all of idtx-flow at once. Pick the smallest artifact that can improve the create -> observe -> patch -> replay cycle.

### Step 1: promote one tiny existing kernel

Use the existing humanoid-bone map or spring-bone-ranges validator, not JFA, as the first hardened Lean-kernel artifact.

Reason: maps/validators are deterministic, small, and host-visible without GPU complexity.

Acceptance:

- `lake build` passes.
- `lake exe emit_artifacts build/` emits the artifact.
- committed artifact matches emitted artifact byte-for-byte.
- runtime core consumes the generated artifact without a manually mirrored copy.
- host adapter does not link Lean.

### Step 2: close one C++ mirror drift

Replace the manually mirrored humanoid bone list in `core/src/vrm_humanoid_bones.cpp` with an emitted generated header/source from `Fabric.VrmUpgrade.HumanoidBones`.

This is the best first code change because the file itself documents the drift risk.

### Step 3: add a host-adapter smoke test

Add one local test that proves:

```text
Lean source -> emitted artifact -> libidtx_core behavior -> Godot/CLI adapter-visible result
```

Keep it CPU-only/headless first.

### Step 4: only then harden shader kernels

For each Slang kernel:

- LeanSlang AST emits source.
- source byte-pin passes.
- `slangc` compiles CPU/CPP and GPU target where available.
- Python or C++ CPU reference agrees on small fixtures.
- host adapter only dispatches or calls generated artifacts.

Promote JFA after its idempotence property is resolved or re-specified.

## Proposed directory contract

```text
openusd-fabric/lean/                 Lean kernel/spec/codegen source
openusd-fabric/build/                ignored local emitted outputs
openusd-fabric/maps/                 committed generated maps
openusd-fabric/schema/               committed USD/schema artifacts
openusd-fabric/shaders/              committed generated shader source
core/include/idtx_core/generated/    committed generated C/C++ headers
core/src/generated/                  committed generated C/C++ source/glue
core/share/godot_scn/                committed slangc-lowered outputs
source/converter/                    Godot host adapter only
unity/IdtxCore/                      Unity host adapter only
```

## Decision recommendation

Use idtx-flow as the Lean 4 kernels core, but define “core” as source-of-truth kernel/spec/codegen, not as a Lean runtime library.

The runtime architecture should remain:

- `libidtx_core`: C ABI + OpenUSD + generated artifacts
- hosts: adapters only
- Lean: build-time proof/codegen authority

Do not make Lean, OpenUSD, or shader compilation a first-loop CHI-340 dependency. Treat this as the next local extraction/hardening lane after the completed MCP + Sandbox smoke loop.

## Addendum (2026-06-10): hexagonal restructure landed

The repo has since been reorganized into top-level hexagonal cluster folders per
<https://v-sekai-multiplayer-fabric.github.io/manuals/decisions/20260610-hexagonal-core-ports-adapters.html>.
Path translations for the directory contract above:

```
core/include/idtx_core/   -> flow/ports/include/idtx_core/   (public C ABI port ring)
core/idtx_core.sigs       -> flow/ports/idtx_core.sigs
core/src/                 -> flow/core/src/
core/share/godot_scn/     -> flow/core/share/godot_scn/
shared/include/idtxflow/  -> flow/core/include/idtxflow/
usd/                      -> flow/core/usd/
source/                   -> flow/adapters/godot/src/
unity/IdtxCore/           -> flow/adapters/unity/IdtxCore/
tools/idtxcli/            -> flow/adapters/cli/
tools/tests/              -> flow/core/spec/
openusd-fabric/           unchanged (build-time spec/codegen cluster)
```
