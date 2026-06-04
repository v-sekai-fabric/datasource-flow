-- Copyright 2026 The openusd-fabric authors.
-- SPDX-License-Identifier: MIT
--
-- Lake project for the openusd-fabric proof + emission work. Mirrors the
-- TOOL_cloth_dynamics layout so contributors moving between repos do not
-- relearn the structure:
--
--   Fabric/Schema/     schema-validation constraint specs (CHI-251)
--   Fabric/VrmUpgrade/ VRM 0.x -> 1.0 upgrade tables (CHI-252)
--   Fabric/Mesh/       tris-to-quads ILP and any mesh-side compute (CHI-253)
--
-- The emit_artifacts exe walks those modules and writes:
--   * Python validators that the Blender hook imports
--   * JSON/C++ upgrade tables that idtx-flow consumes
--   * Slang compute shaders dispatched from idtx-flow
--
-- Each emitted artifact has a paired native_decide proof in its module
-- that pins its exact bytes to a Lean string literal; the proof builds
-- only when the emission matches, so CI catches spec/artifact drift.

import Lake
open Lake DSL

package Fabric where

require LeanSlang from git
  "https://github.com/V-Sekai-fire/lean-slang.git" @ "v0.0.5"

@[default_target] lean_lib Fabric where

lean_exe emit_artifacts where
  root := `EmitArtifacts
