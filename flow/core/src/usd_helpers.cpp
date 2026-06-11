// Copyright 2026 V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

#include "idtx_core/internal/usd_helpers.h"

#include <pxr/base/tf/token.h>
#include <pxr/base/arch/symbols.h>
#include <pxr/base/plug/registry.h>

#include <filesystem>
#include <set>
#include <sstream>
#include <vector>

namespace idtx::core {

void register_usd_plugins(const char* override_dir)
{
    namespace fs = std::filesystem;

    // Resolve THIS module's own path on disk. ArchGetAddressInfo maps the
    // address of a symbol back to the shared object that defines it, so we get
    // the libidtx_core DLL's location regardless of where the host loaded it
    // from — no absolute build-time path is ever baked in.
    std::string module_path;
    if (!pxr::ArchGetAddressInfo(
            reinterpret_cast<void*>(&register_usd_plugins), &module_path, nullptr, nullptr, nullptr)) {
        return;
    }

    std::error_code ec;
    const fs::path module_dir = fs::path(module_path).parent_path();
    const fs::path repo_root  = module_dir.parent_path().parent_path();  // build/idtx_core -> repo

    // Candidate plugin-resource directories, each holding a plugInfo.json.
    // Listed relative to the module (deployed-next-to-DLL forms) and to the
    // repo (the standalone build/idtx_core form). RegisterPlugins ignores any
    // that do not exist, so it is safe to offer the full superset.
    std::vector<fs::path> candidates;
    if (override_dir != nullptr && override_dir[0] != '\0') {
        candidates.push_back(fs::path(override_dir));
    }
    candidates.push_back(module_dir / "usd");                  // Godot: OpenUSD core tree next to DLL
    candidates.push_back(module_dir / "usd~");                 // Unity: AssetDatabase-ignored tree
    candidates.push_back(module_dir / ".." / "plugin" / "usd"); // Godot: top-level Includes-globs resolver + vSekai
    candidates.push_back(repo_root / "openusd-fabric" / "schema");          // standalone: codeless v_sekai schema
    candidates.push_back(repo_root / "usd" / "plugin" / "idtx_resolver" / "resources");  // standalone: resolver

    std::vector<std::string> dirs;
    for (const fs::path& c : candidates) {
        if (fs::exists(c / "plugInfo.json", ec)) {
            dirs.push_back(fs::weakly_canonical(c, ec).string());
        }
    }
    if (!dirs.empty()) {
        pxr::PlugRegistry::GetInstance().RegisterPlugins(dirs);
    }
}

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
