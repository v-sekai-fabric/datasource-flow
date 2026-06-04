// Copyright 2026 V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// Internal C++ USD helpers — not part of the C ABI. Used by core's own
// translation units when emitting / parsing USD stages.

#ifndef IDTX_CORE_INTERNAL_USD_HELPERS_H
#define IDTX_CORE_INTERNAL_USD_HELPERS_H

#include <pxr/base/gf/matrix4d.h>
#include <pxr/usd/sdf/path.h>

#include <set>
#include <string>

namespace idtx::core {

// Convert a row-major float[16] (the C ABI matrix layout used by
// idtx_skeleton / idtx_avatar) into a USD GfMatrix4d.
//
// idtx_core uses the same convention as godot::Transform3D for its
// stored matrices: row-major basis + translation in the last row's
// first three entries. USD GfMatrix4d is also row-major homogeneous
// 4x4, so the copy is direct.
pxr::GfMatrix4d float16_to_gf_matrix(const float matrix[16]);

// Inverse: pack a USD GfMatrix4d into a row-major float[16] for return
// to the C ABI surface.
void gf_matrix_to_float16(pxr::GfMatrix4d const& m, float out_matrix[16]);

// Build a child SdfPath under `parent`, suffixing with _2, _3, ... if
// `desired_name` collides with an existing sibling. Pure helper —
// caller supplies the existing-sibling set as a (sorted) lookup.
pxr::SdfPath unique_child_path(
    pxr::SdfPath const& parent,
    std::string const& desired_name,
    std::set<std::string>& siblings_inout);

}  // namespace idtx::core

#endif  // IDTX_CORE_INTERNAL_USD_HELPERS_H
