// Copyright 2026 V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// jsmn-backed parser for the VRMC_springBone extension JSON blob.
// Isolated in its own TU because cgltf (flow/core/libs/cgltf/cgltf.h) bundles
// its own copy of jsmn — including both headers in the same TU
// produces redefinition errors. This file consumes only jsmn; the
// VRM importer (idtx_vrm_import.cpp) calls into it via the
// declaration in internal/vrm_springbone_parse.h.

#define JSMN_STATIC
#include "jsmn.h"

#include "idtx_core/idtx_core.h"
#include "idtx_core/internal/vrm_springbone_parse.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace idtx::core::vrm {

namespace {

bool token_eq(const char* json, jsmntok_t const& tok, const char* s)
{
    return tok.type == JSMN_STRING
        && (int)std::strlen(s) == tok.end - tok.start
        && std::strncmp(json + tok.start, s, tok.end - tok.start) == 0;
}

std::string token_string(const char* json, jsmntok_t const& tok)
{
    return std::string(json + tok.start, tok.end - tok.start);
}

int token_int(const char* json, jsmntok_t const& tok)
{
    return std::atoi(token_string(json, tok).c_str());
}

float token_float(const char* json, jsmntok_t const& tok)
{
    return static_cast<float>(std::atof(token_string(json, tok).c_str()));
}

int skip_subtree(jsmntok_t const* tokens, int ntok, int idx)
{
    if (idx >= ntok) return idx;
    if (tokens[idx].type == JSMN_OBJECT) {
        int n = tokens[idx].size, j = idx + 1;
        for (int k = 0; k < n; ++k) {
            j = skip_subtree(tokens, ntok, j);
            j = skip_subtree(tokens, ntok, j);
        }
        return j;
    }
    if (tokens[idx].type == JSMN_ARRAY) {
        int n = tokens[idx].size, j = idx + 1;
        for (int k = 0; k < n; ++k) j = skip_subtree(tokens, ntok, j);
        return j;
    }
    return idx + 1;
}

int object_find(const char* json, jsmntok_t const* tokens, int ntok, int obj_idx, const char* key)
{
    if (obj_idx >= ntok || tokens[obj_idx].type != JSMN_OBJECT) return -1;
    int n = tokens[obj_idx].size, j = obj_idx + 1;
    for (int k = 0; k < n; ++k) {
        if (j >= ntok) return -1;
        if (token_eq(json, tokens[j], key)) return j + 1;
        j = skip_subtree(tokens, ntok, j);
        j = skip_subtree(tokens, ntok, j);
    }
    return -1;
}

bool array_to_vec3(const char* json, jsmntok_t const* tokens, int ntok, int arr_idx, float out[3])
{
    if (arr_idx < 0 || arr_idx >= ntok || tokens[arr_idx].type != JSMN_ARRAY) return false;
    if (tokens[arr_idx].size < 3) return false;
    int j = arr_idx + 1;
    for (int k = 0; k < 3 && j < ntok; ++k) {
        out[k] = token_float(json, tokens[j]);
        j = skip_subtree(tokens, ntok, j);
    }
    return true;
}

}  // namespace

void parse_springbone_json(
    idtx_avatar_t* avatar,
    const char* json,
    size_t json_len,
    int32_t (*gltf_node_to_bone)(int gltf_node_idx, void* user),
    void* user)
{
    if (avatar == nullptr || json == nullptr || json_len == 0) return;

    jsmn_parser p;
    jsmn_init(&p);
    int needed = jsmn_parse(&p, json, json_len, nullptr, 0);
    if (needed <= 0) return;
    std::vector<jsmntok_t> tokens(needed);
    jsmn_init(&p);
    int ntok = jsmn_parse(&p, json, json_len, tokens.data(), needed);
    if (ntok <= 0) return;

    auto map_node = [&](int gltf_idx) -> int32_t {
        return gltf_node_to_bone(gltf_idx, user);
    };

    // colliders
    int colliders_arr = object_find(json, tokens.data(), ntok, 0, "colliders");
    std::vector<int32_t> collider_idx_map;
    if (colliders_arr >= 0 && tokens[colliders_arr].type == JSMN_ARRAY) {
        int n = tokens[colliders_arr].size;
        int j = colliders_arr + 1;
        for (int k = 0; k < n && j < ntok; ++k) {
            int obj = j;
            int v;
            int attached_bone = -1;
            if ((v = object_find(json, tokens.data(), ntok, obj, "node")) >= 0) {
                attached_bone = map_node(token_int(json, tokens[v]));
            }
            idtx_spring_collider_t* col = idtx_spring_collider_create();
            idtx_spring_collider_set_attached_bone(col, attached_bone);

            int shape  = object_find(json, tokens.data(), ntok, obj, "shape");
            int sphere = (shape >= 0) ? object_find(json, tokens.data(), ntok, shape, "sphere")  : -1;
            int caps   = (shape >= 0) ? object_find(json, tokens.data(), ntok, shape, "capsule") : -1;
            int prim   = (caps >= 0) ? caps : sphere;
            if (prim >= 0) {
                idtx_spring_collider_set_shape(col,
                    caps >= 0 ? IDTX_COLLIDER_CAPSULE : IDTX_COLLIDER_SPHERE);
                float off[3] = {0, 0, 0};
                int off_idx = object_find(json, tokens.data(), ntok, prim, "offset");
                if (off_idx >= 0) array_to_vec3(json, tokens.data(), ntok, off_idx, off);
                idtx_spring_collider_set_offset(col, off[0], off[1], off[2]);
                int r_idx = object_find(json, tokens.data(), ntok, prim, "radius");
                if (r_idx >= 0) idtx_spring_collider_set_radius(col, token_float(json, tokens[r_idx]));
                if (caps >= 0) {
                    float tail[3] = {0, 0, 0};
                    int t_idx = object_find(json, tokens.data(), ntok, prim, "tail");
                    if (t_idx >= 0) array_to_vec3(json, tokens.data(), ntok, t_idx, tail);
                    idtx_spring_collider_set_tail(col, tail[0], tail[1], tail[2]);
                }
            }
            int32_t added = idtx_avatar_add_spring_collider(avatar, col);
            collider_idx_map.push_back(added);
            j = skip_subtree(tokens.data(), ntok, j);
        }
    }

    // colliderGroups: list of int-array lists, indexed by group index.
    int groups_arr = object_find(json, tokens.data(), ntok, 0, "colliderGroups");
    std::vector<std::vector<int32_t>> group_collider_indices;
    if (groups_arr >= 0 && tokens[groups_arr].type == JSMN_ARRAY) {
        int n = tokens[groups_arr].size;
        int j = groups_arr + 1;
        for (int k = 0; k < n && j < ntok; ++k) {
            std::vector<int32_t> cols;
            int cols_idx = object_find(json, tokens.data(), ntok, j, "colliders");
            if (cols_idx >= 0 && tokens[cols_idx].type == JSMN_ARRAY) {
                int m = tokens[cols_idx].size, c = cols_idx + 1;
                for (int u = 0; u < m && c < ntok; ++u) {
                    int vrm_idx = token_int(json, tokens[c]);
                    if (vrm_idx >= 0 && static_cast<size_t>(vrm_idx) < collider_idx_map.size()) {
                        cols.push_back(collider_idx_map[vrm_idx]);
                    }
                    c = skip_subtree(tokens.data(), ntok, c);
                }
            }
            group_collider_indices.push_back(std::move(cols));
            j = skip_subtree(tokens.data(), ntok, j);
        }
    }

    // springs
    int springs_arr = object_find(json, tokens.data(), ntok, 0, "springs");
    if (springs_arr >= 0 && tokens[springs_arr].type == JSMN_ARRAY) {
        int n = tokens[springs_arr].size, j = springs_arr + 1;
        for (int k = 0; k < n && j < ntok; ++k) {
            idtx_spring_chain_t* chain = idtx_spring_chain_create();
            int name_idx = object_find(json, tokens.data(), ntok, j, "name");
            if (name_idx >= 0) {
                idtx_spring_chain_set_name(chain, token_string(json, tokens[name_idx]).c_str());
            }
            int joints_idx = object_find(json, tokens.data(), ntok, j, "joints");
            std::vector<int32_t> bone_idxs;
            float stiff = 1.0f, drag = 0.4f, grav_p = 0.0f, hit_r = 0.02f;
            float gdir[3] = {0, -1, 0};
            if (joints_idx >= 0 && tokens[joints_idx].type == JSMN_ARRAY) {
                int m = tokens[joints_idx].size, jj = joints_idx + 1;
                for (int u = 0; u < m && jj < ntok; ++u) {
                    int node_idx = object_find(json, tokens.data(), ntok, jj, "node");
                    if (node_idx >= 0) {
                        int32_t bi = map_node(token_int(json, tokens[node_idx]));
                        if (bi >= 0) bone_idxs.push_back(bi);
                    }
                    if (u == 0) {
                        int v;
                        if ((v = object_find(json, tokens.data(), ntok, jj, "hitRadius"))    >= 0) hit_r  = token_float(json, tokens[v]);
                        if ((v = object_find(json, tokens.data(), ntok, jj, "stiffness"))    >= 0) stiff  = token_float(json, tokens[v]);
                        if ((v = object_find(json, tokens.data(), ntok, jj, "gravityPower")) >= 0) grav_p = token_float(json, tokens[v]);
                        if ((v = object_find(json, tokens.data(), ntok, jj, "dragForce"))    >= 0) drag   = token_float(json, tokens[v]);
                        if ((v = object_find(json, tokens.data(), ntok, jj, "gravityDir"))   >= 0)
                            array_to_vec3(json, tokens.data(), ntok, v, gdir);
                    }
                    jj = skip_subtree(tokens.data(), ntok, jj);
                }
            }
            idtx_spring_chain_set_dynamics(chain, stiff, drag, grav_p, hit_r);
            idtx_spring_chain_set_gravity_dir(chain, gdir[0], gdir[1], gdir[2]);
            if (!bone_idxs.empty()) {
                idtx_spring_chain_set_joints(chain, static_cast<int32_t>(bone_idxs.size()), bone_idxs.data());
            }
            int cg_idx = object_find(json, tokens.data(), ntok, j, "colliderGroups");
            if (cg_idx >= 0 && tokens[cg_idx].type == JSMN_ARRAY) {
                int m = tokens[cg_idx].size, c = cg_idx + 1;
                for (int u = 0; u < m && c < ntok; ++u) {
                    int gi = token_int(json, tokens[c]);
                    if (gi >= 0 && static_cast<size_t>(gi) < group_collider_indices.size()) {
                        for (int32_t ci : group_collider_indices[gi]) {
                            idtx_spring_chain_add_collider(chain, ci);
                        }
                    }
                    c = skip_subtree(tokens.data(), ntok, c);
                }
            }
            idtx_avatar_add_spring_chain(avatar, chain);
            j = skip_subtree(tokens.data(), ntok, j);
        }
    }
}

}  // namespace idtx::core::vrm
