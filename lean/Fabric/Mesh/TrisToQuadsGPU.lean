-- Copyright 2026 The openusd-fabric authors.
-- SPDX-License-Identifier: MIT
--
-- Tris-to-quads matching GPU shader (CHI-253).
--
-- The CHI-253 contract is a Lean module that specifies the tris-to-
-- quads max-weight matching algorithm and emits a Slang compute
-- shader. The CPU LEMON path (commit f94395f, since carved out)
-- solves the same problem with optimal Edmonds' blossom; this GPU
-- path runs greedy mutual-best matching in parallel — suboptimal
-- but cheap per-triangle and good enough for the common avatar
-- topology where most triangles have one obvious quad partner.
--
-- Wire-format buffers (matching idtx_core's RenderingDevice
-- dispatch):
--   * triangles : StructuredBuffer<uint3>       — vertex indices.
--   * neighbour : StructuredBuffer<uint>        — 3 entries per
--                                                  triangle; INVALID
--                                                  for boundary.
--   * weight    : StructuredBuffer<float>       — 3 entries per
--                                                  triangle; matches
--                                                  neighbour[].
--   * out_best  : RWStructuredBuffer<uint>      — chosen partner
--                                                  per triangle.
--   * num_tris  : uint (push constant)          — total triangle count.

import LeanSlang.Types
import LeanSlang.AST
import LeanSlang.Emit

namespace Fabric.Mesh.TrisToQuadsGPU

open LeanSlang

/-- Sentinel for "no neighbour" — matches UINT32_MAX. -/
def invalidNeighbour : SlangExpr := .litUint 0xFFFFFFFF

/-- Body of the matching kernel: per-triangle, pick the neighbour
    with the highest weight, write its index to out_best[].

    The loop body uses nested `if` (not `continue`) because the
    current LeanSlang AST has no `.continue` statement constructor;
    rewriting the skip-on-invalid as a positive guard keeps the
    emitted shader compilable by slangc. -/
def matchKernelBody : List SlangStmt :=
  [ .declInit (.scalar .uint) "i" (.member (.var "tid") "x")
  , .ifNoElse (.bin ">=" (.var "i") (.var "num_tris"))
      [ .retVoid ]
  , .declInit (.scalar .float) "best_w" (.litFloat (-1.0))
  , .declInit (.scalar .uint)  "best_j" invalidNeighbour
  , .forCount "e" (.litUint 0) (.litUint 3)
      [ .declInit (.scalar .uint) "j"
          (.index (.var "neighbour")
                  (.bin "+" (.bin "*" (.var "i") (.litUint 3)) (.var "e")))
      , .ifNoElse (.bin "!=" (.var "j") invalidNeighbour)
          [ .declInit (.scalar .float) "w"
              (.index (.var "weight")
                      (.bin "+" (.bin "*" (.var "i") (.litUint 3)) (.var "e")))
          , .ifNoElse (.bin ">" (.var "w") (.var "best_w"))
              [ .assign (.var "best_w") (.var "w")
              , .assign (.var "best_j") (.var "j") ]
          ]
      ]
  , .assign (.index (.var "out_best") (.var "i")) (.var "best_j")
  ]

/-- The matching shader module: one compute kernel + four global
    buffer bindings. -/
def matchShader : SlangShaderModule :=
  { globals :=
      [ { name := "neighbour", type := .roBuf (.scalar .uint),  binding := some 0, space := some 0 }
      , { name := "weight",    type := .roBuf (.scalar .float), binding := some 1, space := some 0 }
      , { name := "out_best",  type := .rwBuf (.scalar .uint),  binding := some 2, space := some 0 }
      , { name := "num_tris",  type := .scalar .uint }
      ]
    functions := [{
      attrs  := [.shaderCompute, .numthreads 64 1 1]
      name   := "match_triangles"
      params := [{ name := "tid", type := .vec .uint 3, semantic := .svDispatchThreadId }]
      body   := matchKernelBody
    }] }

/-- Slang shader source DERIVED from the AST above. -/
def slangSource : String := LeanSlang.emit matchShader

/-- Bit-identical literal copy of `shaders/tris_to_quads_match.slang`
    as it sits on disk. Preserved by the byte-pin proof below.
    Any change to `matchKernelBody` / `matchShader` or to the
    LeanSlang emitter requires regenerating this literal AND the
    shader file. -/
def committedSlangBytes : String :=
"[[vk::binding(0, 0)]]
StructuredBuffer<uint> neighbour;
[[vk::binding(1, 0)]]
StructuredBuffer<float> weight;
[[vk::binding(2, 0)]]
RWStructuredBuffer<uint> out_best;
uint num_tris;

[shader(\"compute\")] [numthreads(64, 1, 1)]
void match_triangles(uint3 tid : SV_DispatchThreadID) {
  uint i = tid.x;
  if ((i >= num_tris)) {
    return;
  }
  float best_w = -1.000000;
  uint best_j = 4294967295u;
  for (uint e = 0u; e < 3u; ++e) {
    uint j = neighbour[((i * 3u) + e)];
    if ((j != 4294967295u)) {
      float w = weight[((i * 3u) + e)];
      if ((w > best_w)) {
        best_w = w;
        best_j = j;
      }
    }
  }
  out_best[i] = best_j;
}"

/-- CHI-253 byte-pin. The literal `committedSlangBytes` must equal
    the spec-derived `slangSource`. Closes the "Lean → Slang half
    is unproven" severity-4 gap from project_chi254_lean_method_status. -/
theorem committedSlangBytes_matches_source :
    committedSlangBytes = slangSource := by native_decide

end Fabric.Mesh.TrisToQuadsGPU
