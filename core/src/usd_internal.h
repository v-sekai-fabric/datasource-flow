/**************************************************************************/
/*  usd_internal.h                                                        */
/**************************************************************************/
/* Copyright 2026 The openusd-fabric authors / V-Sekai contributors.      */
/* SPDX-License-Identifier: Apache-2.0 OR MPL-2.0                         */
/**************************************************************************/

// Core-internal helpers shared between the USD readers (idtx_import_usd.cpp,
// idtx_scene.cpp / the FlatTree converter). Not part of the public C ABI.

#pragma once

#include <pxr/usd/usd/prim.h>

#include "idtx_core/idtx_core.h"

namespace idtx::core::detail {

// Read a UsdShadeMaterial prim into an idtx_material_t (UsdPreviewSurface base
// color / metallic / roughness, plus the VSekaiMToonAPI overlay). Caller owns
// the returned handle. Defined in idtx_import_usd.cpp.
idtx_material_t* read_material(pxr::UsdPrim const& prim);

}  // namespace idtx::core::detail
