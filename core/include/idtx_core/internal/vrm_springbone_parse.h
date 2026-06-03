// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// Internal C++ helper: parse a VRMC_springBone JSON blob and
// populate the avatar's spring chains + colliders. Lives in its
// own TU because cgltf (consumed by idtx_vrm_import.cpp) bundles
// its own copy of jsmn and the symbols collide if both are
// pulled into one translation unit.

#ifndef IDTX_CORE_INTERNAL_VRM_SPRINGBONE_PARSE_H
#define IDTX_CORE_INTERNAL_VRM_SPRINGBONE_PARSE_H

#include "idtx_core/idtx_core.h"

#include <cstddef>

namespace idtx::core::vrm {

// `gltf_node_to_bone` maps a glTF node index (as appears in the
// VRMC_springBone JSON) to a bone index in the avatar's skeleton,
// or -1 if the node isn't a bone. `user` is passed through opaquely
// — the importer typically passes its node-to-bone map.
void parse_springbone_json(
    idtx_avatar_t* avatar,
    const char* json,
    size_t json_len,
    int32_t (*gltf_node_to_bone)(int gltf_node_idx, void* user),
    void* user);

}  // namespace idtx::core::vrm

#endif  // IDTX_CORE_INTERNAL_VRM_SPRINGBONE_PARSE_H
