// Copyright 2026 V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

#include "idtx_core/internal/usd_helpers.h"

#include <pxr/base/tf/token.h>

#include <set>
#include <sstream>

namespace idtx::core {

pxr::GfMatrix4d float16_to_gf_matrix(const float matrix[16])
{
    pxr::GfMatrix4d m;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            m[row][col] = static_cast<double>(matrix[row * 4 + col]);
        }
    }
    return m;
}

void gf_matrix_to_float16(pxr::GfMatrix4d const& m, float out_matrix[16])
{
    if (out_matrix == nullptr) return;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            out_matrix[row * 4 + col] = static_cast<float>(m[row][col]);
        }
    }
}

pxr::SdfPath unique_child_path(
    pxr::SdfPath const& parent,
    std::string const& desired_name,
    std::set<std::string>& siblings_inout)
{
    std::string final_name = desired_name;
    for (int n = 2; siblings_inout.count(final_name); ++n) {
        std::ostringstream oss;
        oss << desired_name << "_" << n;
        final_name = oss.str();
    }
    siblings_inout.insert(final_name);
    return parent.AppendChild(pxr::TfToken(final_name));
}

}  // namespace idtx::core
