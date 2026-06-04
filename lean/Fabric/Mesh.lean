-- Copyright 2026 The openusd-fabric authors.
-- SPDX-License-Identifier: MIT
--
-- Mesh-side specs (CHI-253) plus per-pixel render compute (CHI-255).
-- Hosts the tris-to-quads ILP, the JFA outline algorithm, and any
-- other GPU compute that goes through the LeanSlang DSL.

import Fabric.Mesh.OutlineJFA
import Fabric.Mesh.TrisToQuadsGPU
