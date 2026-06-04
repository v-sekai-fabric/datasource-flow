-- Copyright 2026 The openusd-fabric authors.
-- SPDX-License-Identifier: MIT
--
-- VRM 0.x -> 1.0 upgrade tables (CHI-252). Each table is a Lean total
-- function over the input space; the emit exe writes the JSON/C++ table
-- idtx-flow consumes, and a native_decide proof pins the emitted bytes.
--
-- Phase 1 deliverable is the skeleton plus the first concrete table
-- (SCSS <-> MToon, the bridge V-Sekai needs for existing VRChat avatars).
-- The humanoid bone map, springbone field map, and look-at strategy
-- map land as the CHI-252 design comments stabilise.

import Fabric.VrmUpgrade.ScssMToon
import Fabric.VrmUpgrade.HumanoidBones
import Fabric.VrmUpgrade.SpringBoneFields
