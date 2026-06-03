// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// VRM 1.0 serializer — writes a glTF binary (GLB) with the VRMC_vrm
// extension. Imports follow the same structure in reverse.

#include "idtx_core/idtx_core.h"
#include "idtx_core/internal/json_writer.h"
#include "idtx_core/internal/usd_helpers.h"
#include "idtx_core/internal/vrm_humanoid_bones.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace idtx::core::detail {

// -----------------------------------------------------------------
// GLB container — 12-byte header + 1..N chunks.
//   header: magic(0x46546C67) version(2) length(total bytes)
//   chunk:  length(payload bytes) type(JSON=0x4E4F534A, BIN=0x004E4942)
//   payloads padded to 4-byte alignment with 0x20 (JSON) or 0x00 (BIN).
// -----------------------------------------------------------------

static constexpr uint32_t GLB_MAGIC      = 0x46546C67;  // "glTF"
static constexpr uint32_t GLB_VERSION    = 2;
static constexpr uint32_t CHUNK_JSON     = 0x4E4F534A;  // "JSON"
static constexpr uint32_t CHUNK_BIN      = 0x004E4942;  // "BIN\0"

static void pad_to_4(std::vector<uint8_t>& v, uint8_t pad)
{
    while ((v.size() % 4) != 0) v.push_back(pad);
}

// Append `count` bytes from `src` to `bin`, returning the byte offset
// at which they were written.
static uint32_t append_to_bin(std::vector<uint8_t>& bin, void const* src, size_t count)
{
    uint32_t offset = static_cast<uint32_t>(bin.size());
    auto const* p = static_cast<uint8_t const*>(src);
    bin.insert(bin.end(), p, p + count);
    // 4-byte align inside the BIN chunk — accessor offsets must be
    // multiples of their component size; padding here keeps subsequent
    // float/int accessors aligned for free.
    while ((bin.size() % 4) != 0) bin.push_back(0);
    return offset;
}

static bool write_glb(
    std::string const& json,
    std::vector<uint8_t> const& bin,
    char const* path)
{
    std::vector<uint8_t> j(json.begin(), json.end());
    pad_to_4(j, 0x20);  // pad JSON chunk with spaces
    std::vector<uint8_t> b = bin;
    pad_to_4(b, 0x00);

    uint32_t json_len = static_cast<uint32_t>(j.size());
    uint32_t bin_len  = static_cast<uint32_t>(b.size());

    uint32_t total = 12  // header
                   + 8 + json_len
                   + (bin_len > 0 ? 8 + bin_len : 0);

    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    auto u32 = [&](uint32_t v) {
        f.write(reinterpret_cast<char const*>(&v), 4);
    };
    u32(GLB_MAGIC);
    u32(GLB_VERSION);
    u32(total);

    u32(json_len);
    u32(CHUNK_JSON);
    f.write(reinterpret_cast<char const*>(j.data()), j.size());

    if (bin_len > 0) {
        u32(bin_len);
        u32(CHUNK_BIN);
        f.write(reinterpret_cast<char const*>(b.data()), b.size());
    }
    return f.good();
}

}  // namespace idtx::core::detail

extern "C" IDTX_CORE_API int32_t idtx_core_export_avatar_to_vrm(
    const idtx_avatar_t* avatar,
    const char* path)
{
    if (avatar == nullptr || path == nullptr) return 1;

    // ----- BIN chunk accumulator + accessor/bufferView descriptors -----
    std::vector<uint8_t> bin;

    struct BufferView { uint32_t byte_offset; uint32_t byte_length; int target; };
    struct Accessor   { int buffer_view; int component_type; int count;
                        std::string type; bool has_min_max;
                        float min3[3]; float max3[3]; };

    std::vector<BufferView> buffer_views;
    std::vector<Accessor>   accessors;

    // glTF constants
    constexpr int CT_UINT16  = 5123;
    constexpr int CT_UINT32  = 5125;
    constexpr int CT_FLOAT   = 5126;
    constexpr int TARGET_ARRAY_BUFFER         = 34962;
    constexpr int TARGET_ELEMENT_ARRAY_BUFFER = 34963;

    // Per-mesh accessor planning. Index into mesh_primitives is the
    // glTF mesh index; each entry stores accessor indices for its
    // attributes + indices accessor.
    // glTF spec: JOINTS_N + WEIGHTS_N attributes are VEC4 only. To
    // carry more than 4 bones-per-vertex, the writer emits one
    // (JOINTS_n, WEIGHTS_n) pair per 4-bone set — JOINTS_0/WEIGHTS_0,
    // JOINTS_1/WEIGHTS_1, ... for as many sets as the source mesh's
    // bones-per-vertex requires (`ceil(bpv/4)` sets, padded with
    // zero-weight entries when bpv % 4 != 0). Pure passthrough; the
    // consumer engine decides whether to use all sets, downsample,
    // or pick the dominant weights.
    struct PrimitivePlan {
        int positions_accessor = -1;
        int normals_accessor   = -1;
        int uvs_accessor       = -1;
        std::vector<int> joints_accessor;
        std::vector<int> weights_accessor;
        int indices_accessor   = -1;
    };
    int32_t mesh_count = idtx_avatar_get_mesh_count(avatar);
    std::vector<PrimitivePlan> mesh_primitives(mesh_count);

    for (int32_t mi = 0; mi < mesh_count; ++mi) {
        auto* mh = idtx_avatar_get_mesh(avatar, mi);
        if (mh == nullptr) continue;
        int32_t vc = idtx_mesh_get_vertex_count(mh);
        int32_t ic = idtx_mesh_get_index_count(mh);
        if (vc <= 0 || ic <= 0) continue;

        // positions
        std::vector<float> pos(static_cast<size_t>(vc) * 3);
        idtx_mesh_get_positions(mh, pos.data());
        uint32_t off = idtx::core::detail::append_to_bin(bin, pos.data(), pos.size() * sizeof(float));
        Accessor a;
        a.buffer_view = static_cast<int>(buffer_views.size());
        a.component_type = CT_FLOAT;
        a.count = vc;
        a.type = "VEC3";
        a.has_min_max = true;
        a.min3[0] = a.min3[1] = a.min3[2] =  1e30f;
        a.max3[0] = a.max3[1] = a.max3[2] = -1e30f;
        for (int32_t v = 0; v < vc; ++v) {
            for (int c = 0; c < 3; ++c) {
                float x = pos[v * 3 + c];
                if (x < a.min3[c]) a.min3[c] = x;
                if (x > a.max3[c]) a.max3[c] = x;
            }
        }
        buffer_views.push_back({off, static_cast<uint32_t>(pos.size() * sizeof(float)), TARGET_ARRAY_BUFFER});
        mesh_primitives[mi].positions_accessor = static_cast<int>(accessors.size());
        accessors.push_back(a);

        // normals (optional)
        if (idtx_mesh_has_normals(mh)) {
            std::vector<float> nrm(static_cast<size_t>(vc) * 3);
            idtx_mesh_get_normals(mh, nrm.data());
            uint32_t no = idtx::core::detail::append_to_bin(bin, nrm.data(), nrm.size() * sizeof(float));
            Accessor an; an.buffer_view = static_cast<int>(buffer_views.size());
            an.component_type = CT_FLOAT; an.count = vc; an.type = "VEC3"; an.has_min_max = false;
            buffer_views.push_back({no, static_cast<uint32_t>(nrm.size() * sizeof(float)), TARGET_ARRAY_BUFFER});
            mesh_primitives[mi].normals_accessor = static_cast<int>(accessors.size());
            accessors.push_back(an);
        }

        // uvs (optional)
        if (idtx_mesh_has_uvs(mh)) {
            std::vector<float> uvs(static_cast<size_t>(vc) * 2);
            idtx_mesh_get_uvs(mh, uvs.data());
            uint32_t uo = idtx::core::detail::append_to_bin(bin, uvs.data(), uvs.size() * sizeof(float));
            Accessor au; au.buffer_view = static_cast<int>(buffer_views.size());
            au.component_type = CT_FLOAT; au.count = vc; au.type = "VEC2"; au.has_min_max = false;
            buffer_views.push_back({uo, static_cast<uint32_t>(uvs.size() * sizeof(float)), TARGET_ARRAY_BUFFER});
            mesh_primitives[mi].uvs_accessor = static_cast<int>(accessors.size());
            accessors.push_back(au);
        }

        // Skinning. Pure passthrough: emit one (JOINTS_n, WEIGHTS_n)
        // VEC4 pair per 4-bone set, for `ceil(src_bpv / 4)` sets.
        // Per-vertex bones in the source are taken verbatim — no
        // top-K selection, no renormalisation. The consumer engine
        // (Godot, UniVRM, Babylon) decides how many sets it uses
        // and whether to downsample. The final partial set (when
        // src_bpv % 4 != 0) is zero-padded with bone=0, weight=0.
        int32_t src_bpv = idtx_mesh_get_bones_per_vertex(mh);
        if (src_bpv > 0) {
            std::vector<int32_t> bi_src(static_cast<size_t>(vc) * src_bpv);
            std::vector<float>   wt_src(static_cast<size_t>(vc) * src_bpv);
            idtx_mesh_get_bone_indices(mh, bi_src.data());
            idtx_mesh_get_weights(mh, wt_src.data());

            int set_count = (src_bpv + 3) / 4;   // ceil(src_bpv / 4)
            for (int s = 0; s < set_count; ++s) {
                std::vector<uint16_t> bi_set(static_cast<size_t>(vc) * 4, 0);
                std::vector<float>    wt_set(static_cast<size_t>(vc) * 4, 0.0f);
                for (int32_t v = 0; v < vc; ++v) {
                    for (int k = 0; k < 4; ++k) {
                        int src_idx = s * 4 + k;
                        if (src_idx >= src_bpv) break;
                        int32_t bone = bi_src[v * src_bpv + src_idx];
                        bi_set[v * 4 + k] =
                            (bone < 0 || bone > 0xFFFF) ? 0 : static_cast<uint16_t>(bone);
                        wt_set[v * 4 + k] = wt_src[v * src_bpv + src_idx];
                    }
                }
                uint32_t jo = idtx::core::detail::append_to_bin(bin, bi_set.data(), bi_set.size() * sizeof(uint16_t));
                Accessor aj; aj.buffer_view = static_cast<int>(buffer_views.size());
                aj.component_type = CT_UINT16; aj.count = vc; aj.type = "VEC4"; aj.has_min_max = false;
                buffer_views.push_back({jo, static_cast<uint32_t>(bi_set.size() * sizeof(uint16_t)), TARGET_ARRAY_BUFFER});
                mesh_primitives[mi].joints_accessor.push_back(static_cast<int>(accessors.size()));
                accessors.push_back(aj);

                uint32_t wo = idtx::core::detail::append_to_bin(bin, wt_set.data(), wt_set.size() * sizeof(float));
                Accessor aw; aw.buffer_view = static_cast<int>(buffer_views.size());
                aw.component_type = CT_FLOAT; aw.count = vc; aw.type = "VEC4"; aw.has_min_max = false;
                buffer_views.push_back({wo, static_cast<uint32_t>(wt_set.size() * sizeof(float)), TARGET_ARRAY_BUFFER});
                mesh_primitives[mi].weights_accessor.push_back(static_cast<int>(accessors.size()));
                accessors.push_back(aw);
            }
        }

        // indices (uint32 — glTF doesn't have uint16/uint32 ambiguity
        // when we always pick uint32 for the export). glTF only
        // supports triangle topology; if the source mesh carries
        // n-gons via face_vertex_counts, fan-triangulate them here
        // so the emitted index buffer is always a multiple of 3.
        std::vector<int32_t> idx(static_cast<size_t>(ic));
        idtx_mesh_get_indices(mh, idx.data());

        int32_t fvc_count = idtx_mesh_get_face_vertex_count_count(mh);
        std::vector<uint32_t> idxu;
        if (fvc_count > 0) {
            std::vector<int32_t> fvc(static_cast<size_t>(fvc_count));
            idtx_mesh_get_face_vertex_counts(mh, fvc.data());
            idxu.reserve(static_cast<size_t>(ic));   // upper bound
            int32_t cursor = 0;
            for (int32_t f = 0; f < fvc_count; ++f) {
                int32_t n = fvc[f];
                if (n < 3 || cursor + n > ic) {
                    cursor += (n > 0 ? n : 0);
                    continue;   // skip degenerate / out-of-bounds faces
                }
                // Fan triangulation: (v0,v1,v2), (v0,v2,v3), ...
                uint32_t v0 = static_cast<uint32_t>(idx[cursor]);
                for (int32_t k = 1; k + 1 < n; ++k) {
                    idxu.push_back(v0);
                    idxu.push_back(static_cast<uint32_t>(idx[cursor + k]));
                    idxu.push_back(static_cast<uint32_t>(idx[cursor + k + 1]));
                }
                cursor += n;
            }
        } else {
            // No face_vertex_counts → caller asserted tri-soup. An
            // index count that isn't a multiple of 3 here is a bug
            // upstream (importer / authoring tool) — silently trimming
            // would discard real geometry, so we refuse and fail the
            // mesh emission instead. The caller can re-import with
            // face_vertex_counts set (then the n-gon triangulation
            // branch above kicks in).
            if (ic % 3 != 0) {
                std::fprintf(stderr,
                    "idtx_vrm: mesh %d has %d indices (not a multiple "
                    "of 3) and no face_vertex_counts — refusing to emit "
                    "corrupt glTF. Set face_vertex_counts so the writer "
                    "can triangulate properly.\n", mi, ic);
                return -1;
            }
            idxu.assign(idx.begin(), idx.end());
        }

        uint32_t io = idtx::core::detail::append_to_bin(bin, idxu.data(), idxu.size() * sizeof(uint32_t));
        Accessor ai; ai.buffer_view = static_cast<int>(buffer_views.size());
        ai.component_type = CT_UINT32;
        ai.count = static_cast<int32_t>(idxu.size());
        ai.type = "SCALAR"; ai.has_min_max = false;
        buffer_views.push_back({io, static_cast<uint32_t>(idxu.size() * sizeof(uint32_t)), TARGET_ELEMENT_ARRAY_BUFFER});
        mesh_primitives[mi].indices_accessor = static_cast<int>(accessors.size());
        accessors.push_back(ai);
    }

    // Per-mesh node indices — assigned after the bone nodes so the
    // glTF node table layout is: [root, bones..., meshes...].
    // mesh_node_index[mi] is the glTF node index that wraps mesh mi
    // (or -1 if the mesh was empty / skipped).
    std::vector<int32_t> mesh_node_index(mesh_count, -1);

    // ---------------------------------------------------------------
    auto* skel = idtx_avatar_get_skeleton(avatar);
    int32_t bone_count = (skel != nullptr) ? idtx_skeleton_get_bone_count(skel) : 0;

    // Compute IBM accessor up-front — must land in the accessors vector
    // before the JSON for the accessors array is emitted. (Earlier
    // versions appended it inside the skins emit block, which wrote
    // accessor index 0 into skins while the accessors[] array was
    // still empty in the JSON output.)
    int ibm_accessor_index = -1;
    if (bone_count > 0) {
        std::vector<float> ibms(static_cast<size_t>(bone_count) * 16);
        for (int32_t i = 0; i < bone_count; ++i) {
            float bind[16]; idtx_skeleton_get_bone_bind(skel, i, bind);
            pxr::GfMatrix4d m = idtx::core::float16_to_gf_matrix(bind);
            idtx::core::gf_matrix_to_float16(m.GetInverse(), &ibms[i * 16]);
        }
        uint32_t ibm_off = idtx::core::detail::append_to_bin(
            bin, ibms.data(), ibms.size() * sizeof(float));
        Accessor a_ibm;
        a_ibm.buffer_view = static_cast<int>(buffer_views.size());
        a_ibm.component_type = CT_FLOAT;
        a_ibm.count = bone_count;
        a_ibm.type = "MAT4";
        a_ibm.has_min_max = false;
        buffer_views.push_back({ibm_off, static_cast<uint32_t>(ibms.size() * sizeof(float)), 0});
        ibm_accessor_index = static_cast<int>(accessors.size());
        accessors.push_back(a_ibm);
    }
    int32_t root_node_index = 0;
    auto bone_node_index = [&](int32_t bone) -> int32_t {
        return (bone < 0) ? root_node_index : (1 + bone);
    };

    int32_t mat_count_total = idtx_avatar_get_material_count(avatar);

    // Texture deduplication: each unique URI gets one image / one
    // texture entry. Materials reference textures by index. Empty
    // strings ("") mean "no texture" and don't get registered.
    std::vector<std::string> texture_uris;
    auto register_texture = [&](const char* uri) -> int32_t {
        if (uri == nullptr || uri[0] == '\0') return -1;
        for (size_t i = 0; i < texture_uris.size(); ++i) {
            if (texture_uris[i] == uri) return static_cast<int32_t>(i);
        }
        texture_uris.emplace_back(uri);
        return static_cast<int32_t>(texture_uris.size() - 1);
    };

    // Pre-scan materials to register all referenced textures so the
    // material output loop can resolve indices.
    std::vector<int32_t> mat_base_tex(mat_count_total, -1);
    std::vector<int32_t> mat_normal_tex(mat_count_total, -1);
    for (int32_t mi = 0; mi < mat_count_total; ++mi) {
        auto* mat = idtx_avatar_get_material(avatar, mi);
        if (mat == nullptr) continue;
        mat_base_tex[mi]   = register_texture(idtx_material_get_base_color_texture(mat));
        mat_normal_tex[mi] = register_texture(idtx_material_get_normal_texture(mat));
    }

    // For humanoid mapping, walk the bones once and try fuzzy-match
    // each against the VRM 1.0 humanoid canon (case + non-alphanumeric
    // insensitive). Map keys are CANONICAL VRM names ("hips",
    // "leftUpperLeg", ...); values are the bone indices in the
    // idtx_skeleton. First-match-wins so duplicate-name authoring
    // doesn't poison the table.
    std::unordered_map<std::string, int32_t> humanoid_to_bone;
    if (skel != nullptr) {
        for (int32_t i = 0; i < bone_count; ++i) {
            const char* bn = idtx_skeleton_get_bone_name(skel, i);
            const char* canon = idtx::core::vrm::match_humanoid_bone(bn);
            if (canon != nullptr && humanoid_to_bone.find(canon) == humanoid_to_bone.end()) {
                humanoid_to_bone[canon] = i;
            }
        }
    }

    idtx::core::JsonWriter j;
    j.begin_object();
        j.key("asset"); j.begin_object();
            j.key("version");   j.string("2.0");
            j.key("generator"); j.string("idtx-flow / libidtx_core");
        j.end_object();

        // extensionsUsed — VRMC_vrm is unconditional; VRMC_materials_mtoon
        // is added only if any avatar material is MToon-flagged.
        bool any_mtoon = false;
        for (int32_t mi = 0; mi < mat_count_total; ++mi) {
            auto* mat = idtx_avatar_get_material(avatar, mi);
            if (mat != nullptr && idtx_material_is_mtoon(mat)) { any_mtoon = true; break; }
        }
        int32_t chain_count    = idtx_avatar_get_spring_chain_count(avatar);
        int32_t collider_count = idtx_avatar_get_spring_collider_count(avatar);
        bool any_spring = (chain_count > 0 || collider_count > 0);

        // VRMC_vrm only when the skeleton has every required humanoid
        // bone; otherwise we'd emit a malformed humanoid.humanBones
        // that downstream importers reject. Same gate as the
        // extensions block below.
        static const char* const kVrmRequiredBonesUsed[] = {
            "hips", "spine", "head",
            "leftUpperLeg",  "leftLowerLeg",  "leftFoot",
            "rightUpperLeg", "rightLowerLeg", "rightFoot",
            "leftUpperArm",  "leftLowerArm",  "leftHand",
            "rightUpperArm", "rightLowerArm", "rightHand",
        };
        bool can_emit_vrmc_vrm = true;
        for (const char* required : kVrmRequiredBonesUsed) {
            if (humanoid_to_bone.find(required) == humanoid_to_bone.end()) {
                can_emit_vrmc_vrm = false;
                break;
            }
        }

        j.key("extensionsUsed"); j.begin_array();
            if (can_emit_vrmc_vrm) j.string("VRMC_vrm");
            if (any_mtoon)         j.string("VRMC_materials_mtoon");
            if (any_spring)        j.string("VRMC_springBone");
        j.end_array();

        // Scene + scenes
        j.key("scene"); j.integer(0);
        j.key("scenes"); j.begin_array();
            j.begin_object();
                j.key("nodes"); j.begin_array(); j.integer(root_node_index); j.end_array();
            j.end_object();
        j.end_array();

        // Compute mesh node indices up front: nodes after the bones.
        // Walk in order; skip meshes that had no primitive data.
        int32_t next_node = 1 + bone_count;
        for (int32_t mi = 0; mi < mesh_count; ++mi) {
            if (mesh_primitives[mi].positions_accessor < 0) continue;
            mesh_node_index[mi] = next_node++;
        }

        // Nodes — root first, then bones, then meshes.
        j.key("nodes"); j.begin_array();
            // Root node — its children are the orphan bones + mesh nodes.
            j.begin_object();
                j.key("name"); j.string(idtx_avatar_get_name(avatar));
                std::vector<int32_t> root_children;
                for (int32_t i = 0; i < bone_count; ++i) {
                    if (idtx_skeleton_get_bone_parent(skel, i) < 0) {
                        root_children.push_back(bone_node_index(i));
                    }
                }
                for (int32_t mi = 0; mi < mesh_count; ++mi) {
                    if (mesh_node_index[mi] >= 0) root_children.push_back(mesh_node_index[mi]);
                }
                if (!root_children.empty()) {
                    j.key("children");
                    j.int_array(root_children.data(), root_children.size());
                }
            j.end_object();

            // One node per bone, with children = bones whose parent
            // is this bone, and a local rest transform so the skeleton
            // poses correctly. Without the matrix every bone sits at
            // origin, IBM × identity ≠ rest pose, and the mesh skews
            // to a spike radiating from origin (the classic Blender→
            // Godot import artifact).
            //
            // idtx_skeleton stores `rest` as the joint-LOCAL transform
            // (relative to parent — UsdSkel `restTransforms` semantics).
            // That maps directly to glTF node `matrix` (also local TRS).
            // USD row-major flat == glTF column-major flat for the
            // transposed math convention they share, so we pass the
            // 16 floats through verbatim. Skip emission for an
            // identity rest (saves bytes; glTF default is identity).
            auto is_identity = [](const float m[16]) {
                for (int i = 0; i < 16; ++i) {
                    float expected = (i % 5 == 0) ? 1.0f : 0.0f;
                    if (std::abs(m[i] - expected) > 1e-6f) return false;
                }
                return true;
            };
            for (int32_t i = 0; i < bone_count; ++i) {
                j.begin_object();
                    j.key("name"); j.string(idtx_skeleton_get_bone_name(skel, i));
                    float rest[16];
                    idtx_skeleton_get_bone_rest(skel, i, rest);
                    if (!is_identity(rest)) {
                        j.key("matrix"); j.float_array(rest, 16);
                    }
                    std::vector<int32_t> children;
                    for (int32_t k = 0; k < bone_count; ++k) {
                        if (idtx_skeleton_get_bone_parent(skel, k) == i) {
                            children.push_back(bone_node_index(k));
                        }
                    }
                    if (!children.empty()) {
                        j.key("children");
                        j.int_array(children.data(), children.size());
                    }
                j.end_object();
            }

            // One node per mesh. Has `mesh` index into meshes[] and,
            // when there's a skeleton + the mesh has skinning, a `skin`
            // reference (always skin 0 in this single-skin MVP).
            for (int32_t mi = 0; mi < mesh_count; ++mi) {
                if (mesh_node_index[mi] < 0) continue;
                auto* mh = idtx_avatar_get_mesh(avatar, mi);
                j.begin_object();
                    j.key("name");
                    j.string(mh != nullptr ? idtx_mesh_get_name(mh) : "Mesh");
                    j.key("mesh"); j.integer(mi);
                    // Reference the single skin whenever the mesh
                    // has ANY per-vertex skinning data (bpv > 0).
                    // Previously the gate was bpv == 4, which dropped
                    // skin for every mesh whose source export used
                    // a non-4 bones-per-vertex (= almost every
                    // Blender export beyond a trivial rig).
                    if (bone_count > 0 && mh != nullptr
                        && idtx_mesh_get_bones_per_vertex(mh) > 0) {
                        j.key("skin"); j.integer(0);
                    }
                j.end_object();
            }
        j.end_array();

        // meshes
        if (!mesh_primitives.empty()) {
            j.key("meshes"); j.begin_array();
            for (int32_t mi = 0; mi < mesh_count; ++mi) {
                auto const& mp = mesh_primitives[mi];
                if (mp.positions_accessor < 0) continue;
                j.begin_object();
                    j.key("name");
                    auto* mh = idtx_avatar_get_mesh(avatar, mi);
                    j.string(mh != nullptr ? idtx_mesh_get_name(mh) : "");
                    j.key("primitives"); j.begin_array();
                        j.begin_object();
                            j.key("attributes"); j.begin_object();
                                j.key("POSITION"); j.integer(mp.positions_accessor);
                                if (mp.normals_accessor >= 0) {
                                    j.key("NORMAL"); j.integer(mp.normals_accessor);
                                }
                                if (mp.uvs_accessor >= 0) {
                                    j.key("TEXCOORD_0"); j.integer(mp.uvs_accessor);
                                }
                                // JOINTS_N/WEIGHTS_N — one VEC4 pair
                                // per 4-bone set (passthrough; the
                                // consumer decides how many sets to
                                // honour).
                                for (size_t s = 0; s < mp.joints_accessor.size(); ++s) {
                                    char key[16];
                                    std::snprintf(key, sizeof(key), "JOINTS_%zu", s);
                                    j.key(key); j.integer(mp.joints_accessor[s]);
                                }
                                for (size_t s = 0; s < mp.weights_accessor.size(); ++s) {
                                    char key[16];
                                    std::snprintf(key, sizeof(key), "WEIGHTS_%zu", s);
                                    j.key(key); j.integer(mp.weights_accessor[s]);
                                }
                            j.end_object();
                            if (mp.indices_accessor >= 0) {
                                j.key("indices"); j.integer(mp.indices_accessor);
                            }
                            int32_t mat_index = idtx_avatar_get_mesh_material(avatar, mi);
                            if (mat_index >= 0 && mat_index < mat_count_total) {
                                j.key("material"); j.integer(mat_index);
                            }
                            j.key("mode"); j.integer(4);  // TRIANGLES
                        j.end_object();
                    j.end_array();
                j.end_object();
            }
            j.end_array();
        }

        // materials
        if (mat_count_total > 0) {
            j.key("materials"); j.begin_array();
            for (int32_t mi = 0; mi < mat_count_total; ++mi) {
                auto* mat = idtx_avatar_get_material(avatar, mi);
                j.begin_object();
                    if (mat != nullptr) {
                        j.key("name"); j.string(idtx_material_get_name(mat));
                        float rgba[4]; idtx_material_get_base_color(mat, rgba);
                        j.key("pbrMetallicRoughness"); j.begin_object();
                            j.key("baseColorFactor"); j.float_array(rgba, 4);
                            j.key("metallicFactor");  j.number(idtx_material_get_metallic(mat));
                            j.key("roughnessFactor"); j.number(idtx_material_get_roughness(mat));
                            if (mat_base_tex[mi] >= 0) {
                                j.key("baseColorTexture"); j.begin_object();
                                    j.key("index"); j.integer(mat_base_tex[mi]);
                                j.end_object();
                            }
                        j.end_object();
                        if (mat_normal_tex[mi] >= 0) {
                            j.key("normalTexture"); j.begin_object();
                                j.key("index"); j.integer(mat_normal_tex[mi]);
                            j.end_object();
                        }

                        idtx_alpha_mode_t am = idtx_material_get_alpha_mode(mat);
                        if (am == IDTX_ALPHA_MASK) {
                            j.key("alphaMode"); j.string("MASK");
                            j.key("alphaCutoff"); j.number(idtx_material_get_alpha_cutoff(mat));
                        } else if (am == IDTX_ALPHA_BLEND) {
                            j.key("alphaMode"); j.string("BLEND");
                        }

                        if (idtx_material_is_mtoon(mat)) {
                            j.key("extensions"); j.begin_object();
                                j.key("VRMC_materials_mtoon"); j.begin_object();
                                    j.key("specVersion"); j.string("1.0");
                                    float shade[3]; idtx_material_get_mtoon_shade_color(mat, shade);
                                    float rim[3];   idtx_material_get_mtoon_rim_color(mat, rim);
                                    j.key("shadeColorFactor");        j.float_array(shade, 3);
                                    j.key("parametricRimColorFactor"); j.float_array(rim, 3);
                                    j.key("outlineWidthMode"); j.string("worldCoordinates");
                                    j.key("outlineWidthFactor");
                                    j.number(idtx_material_get_mtoon_outline_width(mat));
                                j.end_object();
                            j.end_object();
                        }
                    }
                j.end_object();
            }
            j.end_array();
        }

        // accessors
        if (!accessors.empty()) {
            j.key("accessors"); j.begin_array();
            for (auto const& a : accessors) {
                j.begin_object();
                    j.key("bufferView");    j.integer(a.buffer_view);
                    j.key("componentType"); j.integer(a.component_type);
                    j.key("count");         j.integer(a.count);
                    j.key("type");          j.string(a.type);
                    if (a.has_min_max) {
                        j.key("min"); j.float_array(a.min3, 3);
                        j.key("max"); j.float_array(a.max3, 3);
                    }
                j.end_object();
            }
            j.end_array();
        }

        // bufferViews
        if (!buffer_views.empty()) {
            j.key("bufferViews"); j.begin_array();
            for (auto const& bv : buffer_views) {
                j.begin_object();
                    j.key("buffer");     j.integer(0);
                    j.key("byteOffset"); j.integer(bv.byte_offset);
                    j.key("byteLength"); j.integer(bv.byte_length);
                    j.key("target");     j.integer(bv.target);
                j.end_object();
            }
            j.end_array();
        }

        // textures + images — one image per unique URI, one texture
        // per image (no sampler customisation in this MVP — UniVRM
        // defaults are sensible).
        if (!texture_uris.empty()) {
            j.key("images"); j.begin_array();
            for (auto const& uri : texture_uris) {
                j.begin_object();
                    j.key("uri"); j.string(uri);
                j.end_object();
            }
            j.end_array();

            j.key("textures"); j.begin_array();
            for (size_t i = 0; i < texture_uris.size(); ++i) {
                j.begin_object();
                    j.key("source"); j.integer(static_cast<int64_t>(i));
                j.end_object();
            }
            j.end_array();
        }

        // buffers — single embedded BIN chunk if any geometry data was written
        if (!bin.empty()) {
            j.key("buffers"); j.begin_array();
                j.begin_object();
                    j.key("byteLength"); j.integer(static_cast<int64_t>(bin.size()));
                j.end_object();
            j.end_array();
        }

        // skins — single skin covering all bones. inverseBindMatrices
        // accessor was computed before the JSON emission started so
        // accessors[] already contains it.
        //
        // `skin.skeleton` is OPTIONAL per glTF 2.0 (defaulting to "the
        // joints with no parent in the joint array are the roots"),
        // but Blender's glTF importer historically requires it for
        // proper armature reconstruction — without it, Blender imports
        // every joint as a stand-alone armature root with no
        // hierarchy. VRM 1.0 also recommends it. Set it to the
        // topmost joint (idtx_skeleton's bone[0], because UsdSkel
        // guarantees parents-precede-children in joints[] — the first
        // bone is always the unique root).
        if (bone_count > 0) {
            int32_t skel_root_bone = 0;
            for (int32_t i = 0; i < bone_count; ++i) {
                if (idtx_skeleton_get_bone_parent(skel, i) < 0) {
                    skel_root_bone = i;
                    break;
                }
            }
            j.key("skins"); j.begin_array();
                j.begin_object();
                    if (ibm_accessor_index >= 0) {
                        j.key("inverseBindMatrices"); j.integer(ibm_accessor_index);
                    }
                    j.key("skeleton"); j.integer(bone_node_index(skel_root_bone));
                    j.key("joints"); j.begin_array();
                        for (int32_t i = 0; i < bone_count; ++i) j.integer(bone_node_index(i));
                    j.end_array();
                j.end_object();
            j.end_array();
        }

        j.key("extensions"); j.begin_object();
            // VRMC_vrm requires humanoid.humanBones to carry every
            // VRM 1.0 required bone (hips, spine, head, all four
            // limbs' upper/lower/foot/hand). If the source skeleton
            // doesn't supply them, emitting a partial humanoid
            // violates the spec schema — better to skip the whole
            // VRMC_vrm extension and ship a "glTF without VRM"
            // than to ship a malformed VRM that downstream tools
            // (UniVRM, Godot godot-vrm) reject loudly.
            static const char* const kVrmRequiredBones[] = {
                "hips", "spine", "head",
                "leftUpperLeg",  "leftLowerLeg",  "leftFoot",
                "rightUpperLeg", "rightLowerLeg", "rightFoot",
                "leftUpperArm",  "leftLowerArm",  "leftHand",
                "rightUpperArm", "rightLowerArm", "rightHand",
            };
            bool humanoid_complete = true;
            for (const char* required : kVrmRequiredBones) {
                if (humanoid_to_bone.find(required) == humanoid_to_bone.end()) {
                    humanoid_complete = false;
                    break;
                }
            }
            if (humanoid_complete) {
                j.key("VRMC_vrm"); j.begin_object();
                    j.key("specVersion"); j.string("1.0");
                    j.key("meta"); j.begin_object();
                        j.key("name"); j.string(idtx_avatar_get_name(avatar));
                        j.key("version"); j.string("1.0");
                        j.key("authors"); j.begin_array(); j.string("idtx-flow"); j.end_array();
                        j.key("licenseUrl"); j.string("https://vrm.dev/licenses/1.0/");
                    j.end_object();
                    j.key("humanoid"); j.begin_object();
                        j.key("humanBones"); j.begin_object();
                            for (auto const& kv : humanoid_to_bone) {
                                j.key(kv.first.c_str());
                                j.begin_object();
                                    j.key("node"); j.integer(bone_node_index(kv.second));
                                j.end_object();
                            }
                        j.end_object();
                    j.end_object();
                j.end_object();
            }

            // VRMC_springBone — one colliderGroup per chain (containing
            // the chain's referenced colliders), so each spring references
            // its own group by index. Simpler than a global colliderGroups
            // table and matches the per-chain ownership model.
            if (any_spring) {
                j.key("VRMC_springBone"); j.begin_object();
                    j.key("specVersion"); j.string("1.0");
                    if (collider_count > 0) {
                        j.key("colliders"); j.begin_array();
                        for (int32_t i = 0; i < collider_count; ++i) {
                            auto* col = idtx_avatar_get_spring_collider(avatar, i);
                            j.begin_object();
                                int32_t b = idtx_spring_collider_get_attached_bone(col);
                                j.key("node"); j.integer(bone_node_index(b));
                                j.key("shape"); j.begin_object();
                                    float off[3]; idtx_spring_collider_get_offset(col, off);
                                    float rad = idtx_spring_collider_get_radius(col);
                                    if (idtx_spring_collider_get_shape(col) == IDTX_COLLIDER_CAPSULE) {
                                        float tail[3]; idtx_spring_collider_get_tail(col, tail);
                                        j.key("capsule"); j.begin_object();
                                            j.key("offset"); j.float_array(off,  3);
                                            j.key("radius"); j.number(rad);
                                            j.key("tail");   j.float_array(tail, 3);
                                        j.end_object();
                                    } else {
                                        j.key("sphere"); j.begin_object();
                                            j.key("offset"); j.float_array(off, 3);
                                            j.key("radius"); j.number(rad);
                                        j.end_object();
                                    }
                                j.end_object();
                            j.end_object();
                        }
                        j.end_array();
                    }

                    // colliderGroups: only emit for chains that have at
                    // least one collider — empty `colliders` arrays
                    // violate the VRMC_springBone spec
                    // (colliderGroup.colliders minItems=1). Track
                    // chain_idx → group_idx so each spring references
                    // the right group below.
                    std::vector<int32_t> chain_to_group(chain_count, -1);
                    if (chain_count > 0) {
                        int32_t group_idx = 0;
                        bool any_group = false;
                        for (int32_t i = 0; i < chain_count; ++i) {
                            auto* chain = idtx_avatar_get_spring_chain(avatar, i);
                            if (idtx_spring_chain_get_collider_count(chain) > 0) {
                                if (!any_group) {
                                    j.key("colliderGroups"); j.begin_array();
                                    any_group = true;
                                }
                                j.begin_object();
                                    j.key("name"); j.string(idtx_spring_chain_get_name(chain));
                                    j.key("colliders"); j.begin_array();
                                        int32_t cc = idtx_spring_chain_get_collider_count(chain);
                                        for (int32_t k = 0; k < cc; ++k) {
                                            j.integer(idtx_spring_chain_get_collider(chain, k));
                                        }
                                    j.end_array();
                                j.end_object();
                                chain_to_group[i] = group_idx++;
                            }
                        }
                        if (any_group) j.end_array();
                    }

                    // springs: skip chains with empty joint list
                    // (schema requires springs[i].joints to be
                    // minItems=1). The "any spring with joints"
                    // check below also controls whether the
                    // springs[] array gets opened at all.
                    if (chain_count > 0) {
                        bool any_emitted = false;
                        for (int32_t i = 0; i < chain_count; ++i) {
                            auto* chain = idtx_avatar_get_spring_chain(avatar, i);
                            int32_t jc = idtx_spring_chain_get_joint_count(chain);
                            if (jc <= 0) continue;
                            if (!any_emitted) {
                                j.key("springs"); j.begin_array();
                                any_emitted = true;
                            }
                            j.begin_object();
                                j.key("name"); j.string(idtx_spring_chain_get_name(chain));
                                j.key("joints"); j.begin_array();
                                    float gdir[3]; idtx_spring_chain_get_gravity_dir(chain, gdir);
                                    float stiff = idtx_spring_chain_get_stiffness(chain);
                                    float drag  = idtx_spring_chain_get_drag(chain);
                                    float gp    = idtx_spring_chain_get_gravity_power(chain);
                                    float hr    = idtx_spring_chain_get_hit_radius(chain);
                                    for (int32_t k = 0; k < jc; ++k) {
                                        int32_t b = idtx_spring_chain_get_joint(chain, k);
                                        j.begin_object();
                                            j.key("node"); j.integer(bone_node_index(b));
                                            j.key("hitRadius");    j.number(hr);
                                            j.key("stiffness");    j.number(stiff);
                                            j.key("gravityPower"); j.number(gp);
                                            j.key("gravityDir");   j.float_array(gdir, 3);
                                            j.key("dragForce");    j.number(drag);
                                        j.end_object();
                                    }
                                j.end_array();
                                if (chain_to_group[i] >= 0) {
                                    j.key("colliderGroups"); j.begin_array();
                                        j.integer(chain_to_group[i]);
                                    j.end_array();
                                }
                            j.end_object();
                        }
                        if (any_emitted) j.end_array();
                    }
                j.end_object();
            }
        j.end_object();
    j.end_object();

    if (!idtx::core::detail::write_glb(j.str(), bin, path)) return 3;
    return 0;
}

// idtx_core_import_avatar_from_vrm lives in idtx_vrm_import.cpp — that
// TU hosts the CGLTF_IMPLEMENTATION and walks the parsed glTF tree
// into our handles.
