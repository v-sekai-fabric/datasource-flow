// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// GodotAvatarBuilder — walks a Godot Node3D scene tree and populates
// an idtx_avatar_t* via the engine-agnostic C ABI. This is the thin
// adapter layer that replaces the inline USD-writing code in
// UsdGodotStageExporter.cpp; the actual USD emission lives in
// libidtx_core (see idtx_core_export_avatar_to_usd).

#pragma once

#include <godot_cpp/classes/node3d.hpp>

#include "idtx_core/idtx_core.h"

namespace idtxflow::exporter
{

// Walk `root` and its descendants, populating a freshly-allocated
// idtx_avatar_t*. Ownership of all sub-handles transfers to the
// returned avatar; the caller frees it with idtx_avatar_destroy.
//
// Returns NULL if `root` is NULL.
::idtx_avatar_t* BuildIdtxAvatarFromGodotScene(godot::Node3D* root);

}  // namespace idtxflow::exporter
