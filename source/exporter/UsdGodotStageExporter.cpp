// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// UsdGodotStageExporter — thin adapter. The actual USD I/O lives in
// libidtx_core; this file only handles the Godot-side scene walk +
// dispatch. See GodotAvatarBuilder.{h,cpp} for the walker.

#include "UsdGodotStageExporter.h"

#include "GodotAvatarBuilder.h"
#include "idtx_core/idtx_core.h"

#include <godot_cpp/variant/string.hpp>

#include <idtxflow/utils/Logger.h>

#include <string>

namespace idtxflow::exporter
{
    IDTX_LOG_CATEGORY("UsdGodotStageExporter")

    bool ExportSceneToFile(godot::Node3D* root, godot::String const& path,
                           ExportOptions const& opts)
    {
        if (root == nullptr) {
            IDTX_LOG(IDTX_ERROR, "UsdGodotStageExporter: null root node");
            return false;
        }
        std::string std_path = std::string(path.utf8().get_data());

        ::idtx_avatar_t* avatar = BuildIdtxAvatarFromGodotScene(root);
        if (avatar == nullptr) {
            IDTX_LOG(IDTX_ERROR, "UsdGodotStageExporter: avatar build failed");
            return false;
        }

        // Optional tris-to-quads pass (CHI-253 CPU path). Off by
        // default — round-trip fixtures expect tri-soup. The caller
        // turns it on for tri-heavy sources (CSG bakes, etc.) to
        // recover quads in the resulting USDA.
        if (opts.reconstruct_quads) {
            int32_t total_quads = 0;
            int32_t mesh_count  = ::idtx_avatar_get_mesh_count(avatar);
            for (int32_t i = 0; i < mesh_count; ++i) {
                ::idtx_mesh_t* m = ::idtx_avatar_get_mesh(avatar, i);
                int32_t n = ::idtx_mesh_reconstruct_quads(
                    m, opts.reconstruct_quads_planarity_max_degrees);
                if (n > 0) total_quads += n;
            }
            IDTX_LOG(IDTX_INFO,
                "UsdGodotStageExporter: reconstruct-quads formed %d quad(s) across %d mesh(es)",
                total_quads, mesh_count);
        }

        int32_t rc = ::idtx_core_export_avatar_to_usd(avatar, std_path.c_str());
        ::idtx_avatar_destroy(avatar);

        if (rc != 0) {
            IDTX_LOG(IDTX_ERROR, "UsdGodotStageExporter: core export failed (rc=%d) for %s",
                     rc, std_path.c_str());
            return false;
        }
        IDTX_LOG(IDTX_INFO, "UsdGodotStageExporter: wrote %s", std_path.c_str());
        return true;
    }
}
