/**************************************************************************/
/*  IdtxSceneGodotBuilder.h                                               */
/**************************************************************************/
/* Copyright 2026 V-Sekai contributors.                                   */
/* SPDX-License-Identifier: Apache-2.0 OR MPL-2.0                         */
/**************************************************************************/

// Builds a Godot Node3D tree from an idtx_scene handed across the libidtx_core
// C ABI. This REPLACES the templated UsdGodotStageConverter: the USD reading +
// prim conversion now happens once inside libidtx_core (idtx_scene.h), and this
// host-side walker just maps the engine-neutral node tree to Godot entities.
// NO OpenUSD (pxr) is touched here — that is the whole point (CHI: disconnect
// OpenUSD from hosts). Up-axis / metersPerUnit are applied at the root here,
// exactly as the old ConvertStagePostProcess did.

#pragma once

#include <vector>

#include <godot_cpp/classes/node3d.hpp>

struct idtx_scene;  // opaque (idtx_core/idtx_scene.h)

namespace idtxflow {

// Walk `scene`, build the Godot node hierarchy, and return the root nodes
// (caller adds them to the tree + sets owners). Returns empty on null/empty.
std::vector<godot::Node3D*> BuildGodotNodesFromScene(idtx_scene* scene);

}  // namespace idtxflow
