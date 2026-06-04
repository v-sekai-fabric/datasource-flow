-- Copyright 2026 The openusd-fabric authors.
-- SPDX-License-Identifier: MIT
--
-- Umbrella module. Importing `Fabric` pulls in every spec sub-module so
-- downstream tooling (the emit_artifacts exe, host-diff harnesses) gets
-- the full proof surface from one import.

import Fabric.Schema
import Fabric.VrmUpgrade
import Fabric.Mesh
import Fabric.Serialization
