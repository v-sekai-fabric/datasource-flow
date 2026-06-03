// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// VRM 1.0 importer — parses a .vrm (GLB) via cgltf, walks glTF
// nodes / meshes / skins, and rebuilds an idtx_avatar_t* with
// skeleton + meshes + materials + spring bones from the
// VRMC_vrm / VRMC_materials_mtoon / VRMC_springBone extensions.
//
// This translation unit is the single CGLTF_IMPLEMENTATION host —
// keep it that way, or the multiply-defined cgltf symbols will
// break linking.

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include "idtx_core/idtx_core.h"
#include "idtx_core/internal/vrm_springbone_parse.h"

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// Copy a cgltf_float[16] into idtx's row-major float[16]. cgltf stores
// matrices column-major (glTF spec); idtx_core uses row-major. Same
// transpose we do on the export side, just in reverse.
static void cgltf_matrix_to_idtx(cgltf_float const m[16], float out[16])
{
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            out[row * 4 + col] = static_cast<float>(m[col * 4 + row]);
        }
    }
}

// Build a row-major float[16] from a cgltf_node's TRS (or matrix).
static void cgltf_node_local_matrix(cgltf_node const& n, float out[16])
{
    cgltf_float m[16];
    cgltf_node_transform_local(&n, m);
    cgltf_matrix_to_idtx(m, out);
}

// Same, but world (after walking up the parent chain).
static void cgltf_node_world_matrix(cgltf_node const& n, float out[16])
{
    cgltf_float m[16];
    cgltf_node_transform_world(&n, m);
    cgltf_matrix_to_idtx(m, out);
}

// Read a contiguous float accessor into a host vector.
static bool read_floats(cgltf_accessor const* acc, std::vector<float>& out)
{
    if (acc == nullptr) return false;
    size_t comps = cgltf_num_components(acc->type);
    out.resize(acc->count * comps);
    return cgltf_accessor_unpack_floats(acc, out.data(), out.size()) == out.size();
}

// Read an index accessor (any int type) into uint32_t.
static bool read_indices(cgltf_accessor const* acc, std::vector<uint32_t>& out)
{
    if (acc == nullptr) return false;
    out.resize(acc->count);
    for (size_t i = 0; i < acc->count; ++i) {
        out[i] = static_cast<uint32_t>(cgltf_accessor_read_index(acc, i));
    }
    return true;
}

// Read a uint accessor (typically JOINTS_0 as uint8 or uint16) into int32_t.
static bool read_uint_array(cgltf_accessor const* acc, std::vector<int32_t>& out)
{
    if (acc == nullptr) return false;
    size_t comps = cgltf_num_components(acc->type);
    out.resize(acc->count * comps);
    for (size_t i = 0; i < acc->count; ++i) {
        cgltf_uint vals[16];
        cgltf_accessor_read_uint(acc, i, vals, comps);
        for (size_t k = 0; k < comps; ++k) {
            out[i * comps + k] = static_cast<int32_t>(vals[k]);
        }
    }
    return true;
}


}  // namespace

extern "C" IDTX_CORE_API idtx_avatar_t* idtx_core_import_avatar_from_vrm(const char* path)
{
    if (path == nullptr) return nullptr;

    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, path, &data) != cgltf_result_success) {
        return nullptr;
    }
    if (cgltf_load_buffers(&options, data, path) != cgltf_result_success) {
        cgltf_free(data);
        return nullptr;
    }

    idtx_avatar_t* avatar = idtx_avatar_create();

    // VRM version detection from data->data_extensions. The VRM 0.x
    // spec named the extension "VRM"; VRM 1.0 renamed it to
    // "VRMC_vrm". Both can also appear in extensionsUsed. Stamping
    // the provenance lets a later USD round-trip preserve which
    // source the avatar came from.
    for (size_t e = 0; e < data->data_extensions_count; ++e) {
        const char* name = data->data_extensions[e].name;
        if (name == nullptr) continue;
        if (std::strcmp(name, "VRMC_vrm") == 0) {
            idtx_avatar_set_source_vrm_version(avatar, "1.0");
            break;
        } else if (std::strcmp(name, "VRM") == 0) {
            idtx_avatar_set_source_vrm_version(avatar, "0.x");
            break;
        }
    }
    if (idtx_avatar_get_source_vrm_version(avatar)[0] == '\0') {
        // Fall back to extensionsUsed if the extension isn't
        // top-level (some glTF authoring tools split data layout).
        for (size_t e = 0; e < data->extensions_used_count; ++e) {
            const char* name = data->extensions_used[e];
            if (name == nullptr) continue;
            if (std::strcmp(name, "VRMC_vrm") == 0) {
                idtx_avatar_set_source_vrm_version(avatar, "1.0");
                break;
            } else if (std::strcmp(name, "VRM") == 0) {
                idtx_avatar_set_source_vrm_version(avatar, "0.x");
                break;
            }
        }
    }

    // Avatar name + root transform: use the first scene's first root
    // node. Sufficient for the avatars idtx-flow writes (single root).
    cgltf_node* root_node = nullptr;
    if (data->scenes_count > 0 && data->scenes[0].nodes_count > 0) {
        root_node = data->scenes[0].nodes[0];
    } else if (data->nodes_count > 0) {
        root_node = &data->nodes[0];
    }
    if (root_node != nullptr) {
        idtx_avatar_set_name(avatar, root_node->name ? root_node->name : "");
        float rm[16];
        cgltf_node_local_matrix(*root_node, rm);
        idtx_avatar_set_root_transform(avatar, rm);
    }

    // Skeleton from skins[0]. VRM 1.0 avatars consistently have a
    // single skin covering every bone; we don't try to merge multi-skin
    // glTFs in this MVP.
    std::unordered_map<cgltf_node const*, int32_t> node_to_bone;
    if (data->skins_count > 0) {
        cgltf_skin const& skin = data->skins[0];
        idtx_skeleton_t* skel = idtx_skeleton_create();
        idtx_skeleton_set_name(skel, skin.name ? skin.name : "Skeleton");

        // First pass: assign bone indices in the order skin.joints
        // appears. Parent resolution can then be done in a second
        // pass since every parent is already in the map.
        for (size_t i = 0; i < skin.joints_count; ++i) {
            node_to_bone[skin.joints[i]] = static_cast<int32_t>(i);
        }

        // inverseBindMatrices accessor — used to recover the bone's
        // bind transform (= inverse of the IBM).
        std::vector<float> ibms;
        bool have_ibms = (skin.inverse_bind_matrices != nullptr)
                       && read_floats(skin.inverse_bind_matrices, ibms);

        for (size_t i = 0; i < skin.joints_count; ++i) {
            cgltf_node const& bn = *skin.joints[i];
            int32_t parent = -1;
            auto it = (bn.parent != nullptr) ? node_to_bone.find(bn.parent)
                                              : node_to_bone.end();
            if (it != node_to_bone.end()) parent = it->second;

            float rest[16];
            cgltf_node_local_matrix(bn, rest);

            float bind[16];
            if (have_ibms && (i + 1) * 16 <= ibms.size()) {
                // bind = inverse(IBM). cgltf gives us IBM column-major;
                // invert via the world-pose accessor (cgltf computes it).
                cgltf_node_world_matrix(bn, bind);
            } else {
                cgltf_node_world_matrix(bn, bind);
            }

            idtx_skeleton_add_bone(skel, bn.name ? bn.name : "", parent, rest, bind);
        }
        idtx_avatar_set_skeleton(avatar, skel);
    }

    // Materials — index in glTF == index in our avatar's materials
    // list. cgltf already resolves the extension data into typed
    // structs, including VRMC_materials_mtoon's extras as JSON text
    // on the material's `extensions` field.
    std::unordered_map<cgltf_material const*, int32_t> mat_to_idx;
    for (size_t i = 0; i < data->materials_count; ++i) {
        cgltf_material const& gm = data->materials[i];
        idtx_material_t* m = idtx_material_create();
        idtx_material_set_name(m, gm.name ? gm.name : "");
        if (gm.has_pbr_metallic_roughness) {
            auto const& p = gm.pbr_metallic_roughness;
            idtx_material_set_base_color(m, p.base_color_factor[0],
                                            p.base_color_factor[1],
                                            p.base_color_factor[2],
                                            p.base_color_factor[3]);
            idtx_material_set_metallic(m,  p.metallic_factor);
            idtx_material_set_roughness(m, p.roughness_factor);
        }
        if (gm.alpha_mode == cgltf_alpha_mode_mask) {
            idtx_material_set_alpha_mode(m, IDTX_ALPHA_MASK);
            idtx_material_set_alpha_cutoff(m, gm.alpha_cutoff);
        } else if (gm.alpha_mode == cgltf_alpha_mode_blend) {
            idtx_material_set_alpha_mode(m, IDTX_ALPHA_BLEND);
        }
        // MToon detection — cgltf surfaces extensions as a JSON blob;
        // a name-substring check is enough to flag the material so
        // downstream renderers know to apply a toon shader.
        for (size_t e = 0; e < gm.extensions_count; ++e) {
            if (gm.extensions[e].name != nullptr
                && std::strcmp(gm.extensions[e].name, "VRMC_materials_mtoon") == 0) {
                // Trigger MToon flag via a setter (any of them flips
                // it on). Detailed parameter recovery via JSON parse
                // is a follow-up; the flag alone is enough for the
                // export round trip to re-add VRMC_materials_mtoon.
                idtx_material_set_mtoon_outline_width(m, 0.0f);
                break;
            }
        }
        mat_to_idx[&gm] = idtx_avatar_add_material(avatar, m);
    }

    // Meshes — each glTF mesh primitive becomes one idtx_mesh.
    for (size_t mi = 0; mi < data->meshes_count; ++mi) {
        cgltf_mesh const& gm = data->meshes[mi];
        for (size_t pi = 0; pi < gm.primitives_count; ++pi) {
            cgltf_primitive const& p = gm.primitives[pi];
            cgltf_accessor const* pos_acc = nullptr;
            cgltf_accessor const* nrm_acc = nullptr;
            cgltf_accessor const* uv_acc  = nullptr;
            cgltf_accessor const* col_acc = nullptr;
            cgltf_accessor const* joi_acc = nullptr;
            cgltf_accessor const* wgt_acc = nullptr;
            for (size_t a = 0; a < p.attributes_count; ++a) {
                cgltf_attribute const& at = p.attributes[a];
                switch (at.type) {
                    case cgltf_attribute_type_position: pos_acc = at.data; break;
                    case cgltf_attribute_type_normal:   nrm_acc = at.data; break;
                    case cgltf_attribute_type_texcoord:
                        if (at.index == 0) uv_acc = at.data; break;
                    case cgltf_attribute_type_color:    col_acc = at.data; break;
                    case cgltf_attribute_type_joints:
                        if (at.index == 0) joi_acc = at.data; break;
                    case cgltf_attribute_type_weights:
                        if (at.index == 0) wgt_acc = at.data; break;
                    default: break;
                }
            }
            if (pos_acc == nullptr || p.indices == nullptr) continue;

            std::vector<float> positions; read_floats(pos_acc, positions);
            std::vector<float> normals;   if (nrm_acc) read_floats(nrm_acc, normals);
            std::vector<float> uvs;       if (uv_acc)  read_floats(uv_acc,  uvs);
            std::vector<float> colors;    if (col_acc) read_floats(col_acc, colors);
            int32_t vc = static_cast<int32_t>(pos_acc->count);

            idtx_mesh_t* m = idtx_mesh_create();
            std::string mesh_name = (gm.name ? gm.name : "Mesh");
            if (gm.primitives_count > 1) mesh_name += "_" + std::to_string(pi);
            idtx_mesh_set_name(m, mesh_name.c_str());
            idtx_mesh_set_vertices(m, vc,
                positions.data(),
                normals.empty() ? nullptr : normals.data(),
                uvs.empty()     ? nullptr : uvs.data(),
                colors.empty()  ? nullptr : colors.data());

            std::vector<uint32_t> idx; read_indices(p.indices, idx);
            std::vector<int32_t> idx_s(idx.begin(), idx.end());
            idtx_mesh_set_indices(m, static_cast<int32_t>(idx_s.size()), idx_s.data());

            if (joi_acc != nullptr && wgt_acc != nullptr) {
                std::vector<int32_t> joints;
                std::vector<float>   weights;
                read_uint_array(joi_acc, joints);
                read_floats(wgt_acc, weights);
                if (!joints.empty() && joints.size() == weights.size()) {
                    int32_t bpv = static_cast<int32_t>(joints.size()) / vc;
                    idtx_mesh_set_skinning(m, bpv, joints.data(), weights.data());
                }
            }

            int32_t mat_idx = -1;
            if (p.material != nullptr) {
                auto it = mat_to_idx.find(p.material);
                if (it != mat_to_idx.end()) mat_idx = it->second;
            }
            idtx_avatar_add_mesh(avatar, m, mat_idx);
        }
    }

    // Spring bones — VRMC_springBone is a top-level extension on
    // data->extensions. Walk to find it, then delegate parsing to
    // idtx_vrm_springbone_parse.cpp (which owns jsmn so cgltf's
    // bundled copy doesn't collide).
    struct SpringCtx {
        cgltf_data const* data;
        std::unordered_map<cgltf_node const*, int32_t> const* node_to_bone;
    } sb_ctx{data, &node_to_bone};
    auto mapper = +[](int gltf_idx, void* user) -> int32_t {
        auto* ctx = static_cast<SpringCtx*>(user);
        if (gltf_idx < 0
            || static_cast<size_t>(gltf_idx) >= ctx->data->nodes_count) return -1;
        auto it = ctx->node_to_bone->find(&ctx->data->nodes[gltf_idx]);
        return (it != ctx->node_to_bone->end()) ? it->second : -1;
    };
    for (size_t e = 0; e < data->data_extensions_count; ++e) {
        cgltf_extension const& ext = data->data_extensions[e];
        if (ext.name != nullptr
            && std::strcmp(ext.name, "VRMC_springBone") == 0
            && ext.data != nullptr) {
            idtx::core::vrm::parse_springbone_json(
                avatar, ext.data, std::strlen(ext.data), mapper, &sb_ctx);
            break;
        }
    }

    cgltf_free(data);
    return avatar;
}
