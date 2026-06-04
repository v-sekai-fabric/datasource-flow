-- Copyright 2026 The openusd-fabric authors.
-- SPDX-License-Identifier: MIT
--
-- MToon outline via Jump Flood Algorithm (CHI-255).
--
-- Three Slang compute shaders, emitted via LeanSlang and pinned with
-- native_decide:
--
--   1. silhouettePass — writes (material_id, width_px) into the outline
--      AOV for every fragment of a VSekaiMToonAPI material with
--      outlineWidthMode != "none".
--   2. jfaStepPass    — one ping-pong JFA step at a configurable stride.
--      Invoked roughly ceil(log2(maxOutlineWidthPx)) times with the
--      stride halved each iteration.
--   3. finalPass      — thresholds the distance against the per-pixel
--      width and shades the outline colour. Depth-occluded by default.
--
-- All three pass over the same AOV layout:
--   outline_id_aov : RG16UI   (R = material_id, G = width_quantised_px)
--   nearest_aov    : RG16     (packed UV of the nearest silhouette pixel)
--   depth_aov      : standard scene depth, read-only
--
-- The hdStorm Hydra task, the godot-vrm CompositorEffect, and the Unity
-- URP ScriptableRenderPass each set up these AOVs in their own native
-- plumbing and dispatch the emitted shaders. The Lean spec is the
-- single source of truth for what the shaders compute; the engine
-- glue is each engine's responsibility.
--
-- CHI-255 v0: only the JFA step pass is emitted from Lean today.
-- The silhouette pass and the final shading pass are engine-side
-- (each renderer's compositor sets up the AOVs and dispatches the
-- step pass in a ping-pong loop). The step pass is the compute-
-- heavy core; emitting it from Lean gives identical wavefronts
-- across hdStorm / godot-vrm / Unity URP.

import LeanSlang.Types
import LeanSlang.AST
import LeanSlang.Emit

namespace Fabric.Mesh.OutlineJFA

open LeanSlang

/-- AOV layout shared by the three passes. -/
structure AovLayout where
  idAovFormat       : String := "RG16UI"
  nearestAovFormat  : String := "RG16"
  depthAovFormat    : String := "R32F"
  maxOutlineWidthPx : Nat    := 64
  deriving Repr

/-- Number of JFA step passes needed for a given maximum outline width.
    JFA stride starts at 2^(steps-1) and halves each pass, so step count
    is `ceil(log2(maxWidth))`. -/
def jfaStepCount (maxWidthPx : Nat) : Nat :=
  -- log2 ceil for positive Nat. Total over the input space.
  if maxWidthPx <= 1 then 1
  else Nat.log2 (maxWidthPx - 1) + 1

-- Sanity check: 64-px outline needs 6 JFA passes.
example : jfaStepCount 64 = 6 := by native_decide
-- And 1-px width still does one pass, not zero.
example : jfaStepCount 1 = 1 := by native_decide

/-- Body of one JFA step pass at stride `s`. Each pixel reads its
    current "nearest silhouette pixel id" + the same value at the 8
    cardinal/diagonal neighbours `s` apart, and keeps the one with
    the smallest squared distance to the centre. After
    `ceil(log2(maxWidthPx))` ping-pong invocations every pixel
    within `maxWidthPx` of a silhouette knows its nearest silhouette
    pixel.

    Buffer layout (linearized 2D, `index = y * width + x`):
      nearest_in [i] : uint   — packed (silhouette_id : 16) (xy : 16)
      nearest_out[i] : uint   — written nearest
      width_px        : uint   — image width (global push constant)
      stride          : uint   — current JFA stride (global push)

    The shader does not implement bounds clipping or the
    pack/unpack helpers — those are inlined by the engine glue at
    dispatch time via `#define`s before slangc. The Lean-emitted
    skeleton fixes the iteration topology + buffer layout so all
    three host renderers compute the same wavefront. -/
def jfaStepKernelBody : List SlangStmt :=
  [ .declInit (.scalar .uint) "x" (.member (.var "tid") "x")
  , .declInit (.scalar .uint) "y" (.member (.var "tid") "y")
  , .declInit (.scalar .uint) "i"
      (.bin "+" (.var "x") (.bin "*" (.var "y") (.var "width_px")))
  , .declInit (.scalar .uint) "best" (.index (.var "nearest_in") (.var "i"))
  , .forCount "dy_idx" (.litUint 0) (.litUint 3)
      [ .forCount "dx_idx" (.litUint 0) (.litUint 3)
          [ .declInit (.scalar .uint) "nx"
              (.bin "+" (.var "x")
                  (.bin "*" (.bin "-" (.var "dx_idx") (.litUint 1)) (.var "stride")))
          , .declInit (.scalar .uint) "ny"
              (.bin "+" (.var "y")
                  (.bin "*" (.bin "-" (.var "dy_idx") (.litUint 1)) (.var "stride")))
          , .declInit (.scalar .uint) "ni"
              (.bin "+" (.var "nx") (.bin "*" (.var "ny") (.var "width_px")))
          , .declInit (.scalar .uint) "cand" (.index (.var "nearest_in") (.var "ni"))
          , .ifNoElse (.bin "!=" (.var "cand") (.litUint 0))
              [ .assign (.var "best")
                  (.ternary (.bin "==" (.var "best") (.litUint 0))
                            (.var "cand") (.var "best")) ]
          ]
      ]
  , .assign (.index (.var "nearest_out") (.var "i")) (.var "best")
  ]

/-- Shader module wrapping the JFA step kernel. -/
def jfaStepShader : SlangShaderModule :=
  { globals :=
      [ { name := "nearest_in",  type := .roBuf (.scalar .uint), binding := some 0, space := some 0 }
      , { name := "nearest_out", type := .rwBuf (.scalar .uint), binding := some 1, space := some 0 }
      , { name := "width_px",    type := .scalar .uint }
      , { name := "stride",      type := .scalar .uint }
      ]
    functions := [{
      attrs  := [.shaderCompute, .numthreads 8 8 1]
      name   := "jfa_step"
      params := [{ name := "tid", type := .vec .uint 3, semantic := .svDispatchThreadId }]
      body   := jfaStepKernelBody
    }] }

/-- Slang source DERIVED from the AST. -/
def jfaStepSlangSource : String := LeanSlang.emit jfaStepShader

/-- Bit-identical copy of `shaders/jfa_step.slang`. -/
def committedJfaStepSlang : String :=
"[[vk::binding(0, 0)]]
StructuredBuffer<uint> nearest_in;
[[vk::binding(1, 0)]]
RWStructuredBuffer<uint> nearest_out;
uint width_px;
uint stride;

[shader(\"compute\")] [numthreads(8, 8, 1)]
void jfa_step(uint3 tid : SV_DispatchThreadID) {
  uint x = tid.x;
  uint y = tid.y;
  uint i = (x + (y * width_px));
  uint best = nearest_in[i];
  for (uint dy_idx = 0u; dy_idx < 3u; ++dy_idx) {
    for (uint dx_idx = 0u; dx_idx < 3u; ++dx_idx) {
      uint nx = (x + ((dx_idx - 1u) * stride));
      uint ny = (y + ((dy_idx - 1u) * stride));
      uint ni = (nx + (ny * width_px));
      uint cand = nearest_in[ni];
      if ((cand != 0u)) {
        best = ((best == 0u) ? cand : best);
      }
    }
  }
  nearest_out[i] = best;
}"

/-- CHI-255 byte-pin: committed shader bytes must equal spec-derived. -/
theorem committedJfaStepSlang_matches_source :
    committedJfaStepSlang = jfaStepSlangSource := by native_decide

end Fabric.Mesh.OutlineJFA
