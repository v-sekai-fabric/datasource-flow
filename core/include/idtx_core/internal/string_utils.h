// Copyright 2026 V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// Internal C++ helpers — NOT part of the C ABI. Consumed only by other
// libidtx_core translation units. Engine adapters (Godot, Unity) should
// never include this header.

#ifndef IDTX_CORE_INTERNAL_STRING_UTILS_H
#define IDTX_CORE_INTERNAL_STRING_UTILS_H

#include <string>

namespace idtx::core {

// Convert an arbitrary UTF-8 string into a USD-prim-name-safe token:
//   * empty input -> "Unnamed"
//   * leading digit -> prefixed with underscore
//   * non-[A-Za-z0-9_] chars replaced with underscore
// Pure function — no I/O, no globals, no engine types.
std::string sanitise_prim_name(std::string const& in);

}  // namespace idtx::core

#endif  // IDTX_CORE_INTERNAL_STRING_UTILS_H
