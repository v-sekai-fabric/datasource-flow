// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

#include "idtx_core/internal/string_utils.h"

#include <cctype>

namespace idtx::core {

std::string sanitise_prim_name(std::string const& in)
{
    if (in.empty()) return "Unnamed";
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        if (i == 0 && std::isdigit(static_cast<unsigned char>(c))) {
            out.push_back('_');
        }
        out.push_back(ok ? c : '_');
    }
    return out;
}

}  // namespace idtx::core
