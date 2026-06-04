-- Copyright 2026 The openusd-fabric authors.
-- SPDX-License-Identifier: MIT
--
-- `emit_artifacts` -- Lake exe that walks the Fabric.* spec modules and
-- writes every proof-pinned artifact (Python validators, JSON/C++
-- upgrade tables, Slang shaders) to disk under <outDir>/.
--
-- Layout mirrors TOOL_cloth_dynamics/lean/EmitShaders.lean: one
-- registry-of-things list, one IO loop, exit 0 on success. Each entry's
-- `source` is a String literal in the spec module; the spec module
-- carries a native_decide proof binding that literal to the bytes the
-- spec produces, so editing the emitted file fails the build.
--
-- Usage:
--     lake exe emit_artifacts /path/to/output/dir
--
-- The host-diff harness under lean/tests/validate/ then runs the real
-- downstream tool (Python interpreter for validators, slangc for
-- shaders, usdGenSchema for any future schema codegen) against the
-- emitted output and bit-checks against a hand-written CPU reference.

import Fabric

open Fabric

/-- Registry of (relative output path, source bytes) pairs. Add entries
    here as new spec modules land. The path is intentionally explicit
    so the registry doubles as the file-tree manifest. -/
private def artifacts : List (String × String) :=
  [ ( "schema/spring_bone_ranges_validator.py"
    , Fabric.Schema.SpringBoneRanges.pythonValidatorSource )
  , ( "schema/spring_bone_ranges_validator.hpp"
    , Fabric.Schema.SpringBoneRanges.cppValidatorSource )
  , ( "maps/scss_mtoon_map.json"
    , Fabric.VrmUpgrade.ScssMToon.jsonSource )
  , ( "maps/humanoid_bones_map.json"
    , Fabric.VrmUpgrade.HumanoidBones.jsonSource )
  , ( "maps/springbone_fields_map.json"
    , Fabric.VrmUpgrade.SpringBoneFields.jsonSource )
  , ( "shaders/tris_to_quads_match.slang"
    , Fabric.Mesh.TrisToQuadsGPU.slangSource )
  , ( "shaders/jfa_step.slang"
    , Fabric.Mesh.OutlineJFA.jfaStepSlangSource )
  , ( "shaders/godot_scn.slang"
    , Fabric.Serialization.GodotScn.slangSource )
  ]

def main (args : List String) : IO UInt32 := do
  let outDir := args.headD "."
  IO.FS.createDirAll outDir
  for (relPath, source) in artifacts do
    let path := outDir ++ "/" ++ relPath
    let parent := System.FilePath.parent path
    match parent with
    | some p => IO.FS.createDirAll p.toString
    | none   => pure ()
    IO.FS.writeFile path source
    IO.println s!"wrote {path}"
  return 0
