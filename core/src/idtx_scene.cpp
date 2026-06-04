/**************************************************************************/
/*  idtx_scene.cpp                                                        */
/**************************************************************************/
/* Copyright 2026 V-Sekai contributors.                                   */
/* SPDX-License-Identifier: Apache-2.0 OR MPL-2.0                         */
/**************************************************************************/

// Implementation of idtx_scene.h: open a USD stage, run the upstream converter
// with the in-core FlatTree target, finalize staged mesh data into idtx_mesh
// handles, and expose the result through the flat C ABI. The converter logic
// (StageConverter + Mesh/Skeleton/Material converters) is the upstream code,
// unchanged — only the TargetEngine (FlatTree) is ours.

#include "idtx_core/idtx_scene.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/ar/resolvedPath.h>
#include <pxr/usd/ar/resolverContextBinder.h>

#include "scene/FlatTreeTarget.h"
#include "scene/FlatTreeTypeConverter.h"
#include "scene/FlatTreeStageConverter.h"

namespace S = idtx::core::scene;

// The opaque C handle owns the FlatScene. idtx_node_t* is just a FlatNode* in
// disguise (the converter heap-allocates them; the scene owns their lifetime).
struct idtx_scene {
    S::FlatScene fs;
};

static inline const S::FlatNode* as_node(const idtx_node_t* n) {
    return reinterpret_cast<const S::FlatNode*>(n);
}

namespace {

// Flatten one staged FMeshData into an idtx_mesh_t (positions/normals/uvs/colors
// + indices + optional 4-bone skinning). Returns NULL if there are no verts.
idtx_mesh_t* finalize_mesh(const S::FMeshData& md) {
    if (md.Vertices.empty() || md.Triangles.empty()) return nullptr;
    const int32_t vcount = static_cast<int32_t>(md.Vertices.size());

    std::vector<float> pos(vcount * 3);
    for (int32_t i = 0; i < vcount; ++i) { pos[i*3+0] = md.Vertices[i].x; pos[i*3+1] = md.Vertices[i].y; pos[i*3+2] = md.Vertices[i].z; }

    std::vector<float> nrm;
    if (static_cast<int32_t>(md.Normals.size()) == vcount) {
        nrm.resize(vcount * 3);
        for (int32_t i = 0; i < vcount; ++i) { nrm[i*3+0] = md.Normals[i].x; nrm[i*3+1] = md.Normals[i].y; nrm[i*3+2] = md.Normals[i].z; }
    }
    std::vector<float> uv;
    if (static_cast<int32_t>(md.UVs.size()) == vcount) {
        uv.resize(vcount * 2);
        for (int32_t i = 0; i < vcount; ++i) { uv[i*2+0] = md.UVs[i].x; uv[i*2+1] = md.UVs[i].y; }
    }
    std::vector<float> col;
    if (static_cast<int32_t>(md.VertexColors.size()) == vcount) {
        col.resize(vcount * 4);
        for (int32_t i = 0; i < vcount; ++i) { col[i*4+0] = md.VertexColors[i].r; col[i*4+1] = md.VertexColors[i].g; col[i*4+2] = md.VertexColors[i].b; col[i*4+3] = md.VertexColors[i].a; }
    }

    idtx_mesh_t* mesh = idtx_mesh_create();
    idtx_mesh_set_vertices(mesh, vcount, pos.data(),
                           nrm.empty() ? nullptr : nrm.data(),
                           uv.empty()  ? nullptr : uv.data(),
                           col.empty() ? nullptr : col.data());
    idtx_mesh_set_indices(mesh, static_cast<int32_t>(md.Triangles.size()), md.Triangles.data());

    if (!md.Bones.empty() && static_cast<int32_t>(md.Bones.size()) == vcount * 4)
        idtx_mesh_set_skinning(mesh, 4, md.Bones.data(), md.Weights.data());

    // Blend shapes: scatter the sparse SoA deltas back into the dense per-vertex
    // arrays the C ABI takes, then register each target with its current weight.
    for (const S::FBlendShape& bs : md.BlendShapes) {
        if (bs.indices.empty()) continue;
        const bool has_n = bs.has_normals && bs.nrm_offsets.size() == bs.indices.size();
        std::vector<float> bpos(static_cast<size_t>(vcount) * 3, 0.0f);
        std::vector<float> bnrm;
        if (has_n) bnrm.assign(static_cast<size_t>(vcount) * 3, 0.0f);
        for (size_t k = 0; k < bs.indices.size(); ++k) {
            const int32_t vi = bs.indices[k];
            if (vi < 0 || vi >= vcount) continue;
            bpos[vi*3+0] = bs.pos_offsets[k].x; bpos[vi*3+1] = bs.pos_offsets[k].y; bpos[vi*3+2] = bs.pos_offsets[k].z;
            if (has_n) { bnrm[vi*3+0] = bs.nrm_offsets[k].x; bnrm[vi*3+1] = bs.nrm_offsets[k].y; bnrm[vi*3+2] = bs.nrm_offsets[k].z; }
        }
        idtx_mesh_add_blendshape(mesh, bs.name.c_str(), bs.weight,
                                 bpos.data(), has_n ? bnrm.data() : nullptr);
    }

    return mesh;
}

// Read each material's base-color/normal texture bytes out of the stage's asset
// resolver (usdz members included) into the scene texture table, keyed by the
// same path the material carries. Deduplicates by key.
void extract_textures(S::FlatScene& fs, const pxr::UsdStageRefPtr& stage) {
    pxr::ArResolverContextBinder binder(stage->GetPathResolverContext());
    pxr::ArResolver& resolver = pxr::ArGetResolver();

    auto pull = [&](const char* key) {
        if (!key || !*key) return;
        for (const auto& t : fs.textures) {
            if (t.name == key) return;   // already pulled
        }
        pxr::ArResolvedPath rp(key);
        std::shared_ptr<pxr::ArAsset> asset = resolver.OpenAsset(rp);
        if (!asset) {
            rp = resolver.Resolve(key);
            if (rp) asset = resolver.OpenAsset(rp);
        }
        if (!asset) return;
        const size_t sz = asset->GetSize();
        std::shared_ptr<const char> buf = asset->GetBuffer();
        if (!buf || sz == 0) return;
        S::FTexture tex;
        tex.name = key;
        tex.bytes.assign(reinterpret_cast<const uint8_t*>(buf.get()),
                         reinterpret_cast<const uint8_t*>(buf.get()) + sz);
        fs.textures.push_back(std::move(tex));
    };

    for (idtx_material_t* m : fs.materials) {
        pull(idtx_material_get_base_color_texture(m));
        pull(idtx_material_get_normal_texture(m));
    }
}

// ---------------------------------------------------------------------
// Flat-tree -> avatar adaptation helpers. The scene is a hierarchy (node
// local transforms + skeleton/mesh handles); the avatar is FLAT (one root,
// one skeleton, a mesh bag, no per-node tree). To collapse the tree we BAKE
// each node's world transform W fully into its geometry, so the avatar is
// natively in the converter's Y-up universe frame with NO compensating
// root_transform. (Per design: a root xform is "not converting fully".)
// ---------------------------------------------------------------------

// row-vector 4x4 multiply C = A * B (USD/idtx float16 layout).
void mat16_mul(const float* A, const float* B, float* C) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            C[r*4+c] = A[r*4+0]*B[0*4+c] + A[r*4+1]*B[1*4+c]
                     + A[r*4+2]*B[2*4+c] + A[r*4+3]*B[3*4+c];
}

// World transform of a flat node = local * parent_local * ... * root_local
// (row-vector: world = local_child * ... * local_root).
void node_world(const S::FlatScene& fs, const S::FlatNode* n, float out[16]) {
    float acc[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    const S::FlatNode* cur = n;
    while (cur) {
        float tmp[16];
        mat16_mul(acc, cur->local_transform.m, tmp);
        std::copy_n(tmp, 16, acc);
        cur = (cur->parent >= 0 && cur->parent < (int32_t)fs.nodes.size())
            ? fs.nodes[cur->parent].get() : nullptr;
    }
    std::copy_n(acc, 16, out);
}

// Bake W into a mesh's positions (p' = p*W, w=1) and normals (rotation part,
// w=0). Rigid W => the 3x3 block is the correct normal transform.
void bake_world_into_mesh(idtx_mesh_t* mesh, const float W[16]) {
    if (!mesh) return;
    int32_t vc = idtx_mesh_get_vertex_count(mesh);
    if (vc <= 0) return;
    std::vector<float> pos((size_t)vc * 3);
    idtx_mesh_get_positions(mesh, pos.data());
    for (int32_t v = 0; v < vc; ++v) {
        float* p = &pos[v*3];
        float x = p[0], y = p[1], z = p[2];
        p[0] = x*W[0] + y*W[4] + z*W[8]  + W[12];
        p[1] = x*W[1] + y*W[5] + z*W[9]  + W[13];
        p[2] = x*W[2] + y*W[6] + z*W[10] + W[14];
    }
    std::vector<float> nrm;
    bool has_n = idtx_mesh_has_normals(mesh) != 0;
    if (has_n) {
        nrm.resize((size_t)vc * 3);
        idtx_mesh_get_normals(mesh, nrm.data());
        for (int32_t v = 0; v < vc; ++v) {
            float* nn = &nrm[v*3];
            float x = nn[0], y = nn[1], z = nn[2];
            nn[0] = x*W[0] + y*W[4] + z*W[8];
            nn[1] = x*W[1] + y*W[5] + z*W[9];
            nn[2] = x*W[2] + y*W[6] + z*W[10];
        }
    }
    std::vector<float> uvs, cols;
    if (idtx_mesh_has_uvs(mesh))    { uvs.resize((size_t)vc * 2);  idtx_mesh_get_uvs(mesh, uvs.data()); }
    if (idtx_mesh_has_colors(mesh)) { cols.resize((size_t)vc * 4); idtx_mesh_get_colors(mesh, cols.data()); }
    idtx_mesh_set_vertices(mesh, vc, pos.data(),
                           has_n ? nrm.data() : nullptr,
                           uvs.empty() ? nullptr : uvs.data(),
                           cols.empty() ? nullptr : cols.data());
}

}  // namespace

extern "C" {

IDTX_CORE_API idtx_scene_t* idtx_core_import_scene_from_usd(const char* uri) {
    if (!uri) return nullptr;
    pxr::UsdStageRefPtr stage = pxr::UsdStage::Open(std::string(uri));
    if (!stage) return nullptr;

    auto* scene = new idtx_scene();
    idtxflow::converter::UsdStageConverter<idtxflow::types::TargetEngineFlatTree> converter(&scene->fs, nullptr);
    converter.Convert(stage);

    // Finalize staged mesh data into idtx_mesh handles.
    for (auto& up : scene->fs.nodes) {
        S::FlatNode* n = up.get();
        if (n->kind == IDTX_NODE_MESH)          n->mesh = finalize_mesh(n->mesh_data);
        else if (n->kind == IDTX_NODE_SKELETON) n->skinned_mesh = finalize_mesh(n->mesh_data);
    }

    // Extract referenced texture bytes while the stage (and its asset resolver
    // context) is still open — this is where usdz-internal members resolve. The
    // host loads these raw bytes; it never has to open the usdz package itself.
    extract_textures(scene->fs, stage);
    return scene;
}

IDTX_CORE_API void idtx_core_scene_destroy(idtx_scene_t* scene) {
    if (!scene) return;
    for (auto& up : scene->fs.nodes) {
        if (up->mesh)         idtx_mesh_destroy(up->mesh);
        if (up->skinned_mesh) idtx_mesh_destroy(up->skinned_mesh);
        if (up->skeleton)     idtx_skeleton_destroy(up->skeleton);
    }
    for (idtx_material_t* m : scene->fs.materials) if (m) idtx_material_destroy(m);
    delete scene;
}

// Adapt the converted (Y-up) scene into a flat avatar, TRANSFERRING ownership
// of the geometry handles out of the scene (their slots are nulled so a later
// idtx_core_scene_destroy() won't double-free). Each node's world transform is
// baked fully into its geometry so the avatar needs no root_transform. Only the
// first skeleton is taken (the avatar model is single-skeleton); a multi-skel
// source keeps its other rigs' skinned meshes static. Caller still owns + must
// destroy `scene`. Materials/textures (stage-derived) are handled by the caller.
IDTX_CORE_API idtx_avatar_t* idtx_scene_build_avatar(idtx_scene_t* scene) {
    if (!scene) return nullptr;
    S::FlatScene& fs = scene->fs;
    idtx_avatar_t* avatar = idtx_avatar_create();

    // Identity root: everything is baked into universe (Y-up) geometry below.
    const float I16[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    idtx_avatar_set_root_transform(avatar, I16);

    // Transfer materials first so node material_index stays valid (1:1 order).
    for (size_t i = 0; i < fs.materials.size(); ++i) {
        if (fs.materials[i]) {
            idtx_avatar_add_material(avatar, fs.materials[i]);
            fs.materials[i] = nullptr;  // ownership moved to avatar
        }
    }

    // First skeleton node (most-bones wins, so a 1-bone decoy under a
    // multi-skeleton SkelRoot doesn't shadow the real rig).
    S::FlatNode* skel_node = nullptr;
    for (auto& up : fs.nodes) {
        S::FlatNode* n = up.get();
        if (n->kind != IDTX_NODE_SKELETON || !n->skeleton) continue;
        if (!skel_node ||
            idtx_skeleton_get_bone_count(n->skeleton) >
            idtx_skeleton_get_bone_count(skel_node->skeleton)) {
            skel_node = n;
        }
    }

    if (skel_node) {
        float W[16];
        node_world(fs, skel_node, W);
        idtx_skeleton_t* skel = skel_node->skeleton;
        // Bake W into bind (all joints) + rest (roots). Combined with verts*W
        // below, skinning composes to W*posed = universe, and bind/rest/verts
        // all live in Y-up so an armature aligns whichever it reads.
        int32_t bc = idtx_skeleton_get_bone_count(skel);
        for (int32_t b = 0; b < bc; ++b) {
            float m[16], mw[16];
            idtx_skeleton_get_bone_bind(skel, b, m);
            mat16_mul(m, W, mw);
            idtx_skeleton_set_bone_bind(skel, b, mw);
            if (idtx_skeleton_get_bone_parent(skel, b) < 0) {
                idtx_skeleton_get_bone_rest(skel, b, m);
                mat16_mul(m, W, mw);
                idtx_skeleton_set_bone_rest(skel, b, mw);
            }
        }
        idtx_avatar_set_skeleton(avatar, skel);
        skel_node->skeleton = nullptr;  // ownership moved

        if (skel_node->skinned_mesh) {
            bake_world_into_mesh(skel_node->skinned_mesh, W);
            idtx_avatar_add_mesh(avatar, skel_node->skinned_mesh, skel_node->material_index);
            skel_node->skinned_mesh = nullptr;
        }
    }

    // Static meshes: bake each node's own world transform into its verts.
    for (auto& up : fs.nodes) {
        S::FlatNode* n = up.get();
        if (n->kind != IDTX_NODE_MESH || !n->mesh) continue;
        float Wm[16];
        node_world(fs, n, Wm);
        bake_world_into_mesh(n->mesh, Wm);
        idtx_avatar_add_mesh(avatar, n->mesh, n->material_index);
        n->mesh = nullptr;  // ownership moved
    }

    return avatar;
}

// ---- stage metadata ----
IDTX_CORE_API idtx_axis_t idtx_scene_get_up_axis(const idtx_scene_t* s) { return s ? s->fs.up_axis : IDTX_AXIS_Y; }
IDTX_CORE_API double idtx_scene_get_meters_per_unit(const idtx_scene_t* s) { return s ? s->fs.meters_per_unit : 0.01; }

// ---- tree ----
IDTX_CORE_API int32_t idtx_scene_get_node_count(const idtx_scene_t* s) {
    return s ? static_cast<int32_t>(s->fs.nodes.size()) : 0;
}
IDTX_CORE_API idtx_node_t* idtx_scene_get_node(const idtx_scene_t* s, int32_t i) {
    if (!s || i < 0 || i >= static_cast<int32_t>(s->fs.nodes.size())) return nullptr;
    return reinterpret_cast<idtx_node_t*>(s->fs.nodes[i].get());
}
IDTX_CORE_API int32_t idtx_node_get_parent(const idtx_node_t* n) { return n ? as_node(n)->parent : -1; }
IDTX_CORE_API idtx_node_kind_t idtx_node_get_kind(const idtx_node_t* n) { return as_node(n)->kind; }
IDTX_CORE_API const char* idtx_node_get_name(const idtx_node_t* n) { return as_node(n)->name.c_str(); }
IDTX_CORE_API const char* idtx_node_get_path(const idtx_node_t* n) { return as_node(n)->path.c_str(); }
IDTX_CORE_API void idtx_node_get_local_transform(const idtx_node_t* n, float out[16]) {
    const auto& m = as_node(n)->local_transform.m;
    for (int i = 0; i < 16; ++i) out[i] = m[i];
}

// ---- shared visual payload ----
IDTX_CORE_API int32_t idtx_node_get_material_index(const idtx_node_t* n) { return as_node(n)->material_index; }
IDTX_CORE_API int32_t idtx_node_get_display_color_count(const idtx_node_t* n) {
    return static_cast<int32_t>(as_node(n)->display_rgba.size() / 4);
}
IDTX_CORE_API void idtx_node_get_display_colors(const idtx_node_t* n, float* out_rgba) {
    const auto& d = as_node(n)->display_rgba;
    for (size_t i = 0; i < d.size(); ++i) out_rgba[i] = d[i];
}
IDTX_CORE_API idtx_color_interp_t idtx_node_get_color_interpolation(const idtx_node_t* n) { return as_node(n)->color_interp; }

IDTX_CORE_API int32_t idtx_scene_get_material_count(const idtx_scene_t* s) {
    return s ? static_cast<int32_t>(s->fs.materials.size()) : 0;
}
IDTX_CORE_API idtx_material_t* idtx_scene_get_material(const idtx_scene_t* s, int32_t i) {
    if (!s || i < 0 || i >= static_cast<int32_t>(s->fs.materials.size())) return nullptr;
    return s->fs.materials[i];
}

// ---- texture table (raw encoded image bytes, keyed by material path) ----
static inline const S::FTexture* as_texture(const idtx_texture_t* t) {
    return reinterpret_cast<const S::FTexture*>(t);
}
IDTX_CORE_API int32_t idtx_scene_get_texture_count(const idtx_scene_t* s) {
    return s ? static_cast<int32_t>(s->fs.textures.size()) : 0;
}
IDTX_CORE_API idtx_texture_t* idtx_scene_get_texture(const idtx_scene_t* s, int32_t i) {
    if (!s || i < 0 || i >= static_cast<int32_t>(s->fs.textures.size())) return nullptr;
    return reinterpret_cast<idtx_texture_t*>(const_cast<S::FTexture*>(&s->fs.textures[i]));
}
IDTX_CORE_API const char* idtx_texture_get_name(const idtx_texture_t* t) {
    return t ? as_texture(t)->name.c_str() : "";
}
IDTX_CORE_API int32_t idtx_texture_get_byte_count(const idtx_texture_t* t) {
    return t ? static_cast<int32_t>(as_texture(t)->bytes.size()) : 0;
}
IDTX_CORE_API void idtx_texture_get_bytes(const idtx_texture_t* t, uint8_t* out_bytes) {
    if (!t || !out_bytes) return;
    const auto& b = as_texture(t)->bytes;
    for (size_t i = 0; i < b.size(); ++i) out_bytes[i] = b[i];
}

// ---- per-kind payload ----
IDTX_CORE_API double idtx_node_get_cube_size(const idtx_node_t* n) { return as_node(n)->size; }
IDTX_CORE_API void idtx_node_get_cone(const idtx_node_t* n, double* r, double* h, idtx_axis_t* a) {
    const auto* p = as_node(n); if (r) *r = p->radius; if (h) *h = p->height; if (a) *a = p->axis;
}
IDTX_CORE_API void idtx_node_get_cylinder(const idtx_node_t* n, double* r, double* h, idtx_axis_t* a) {
    const auto* p = as_node(n); if (r) *r = p->radius; if (h) *h = p->height; if (a) *a = p->axis;
}
IDTX_CORE_API double idtx_node_get_sphere_radius(const idtx_node_t* n) { return as_node(n)->radius; }
IDTX_CORE_API idtx_mesh_t* idtx_node_get_mesh(const idtx_node_t* n) { return as_node(n)->mesh; }
IDTX_CORE_API idtx_skeleton_t* idtx_node_get_skeleton(const idtx_node_t* n) { return as_node(n)->skeleton; }
IDTX_CORE_API idtx_mesh_t* idtx_node_get_skinned_mesh(const idtx_node_t* n) { return as_node(n)->skinned_mesh; }

// ---- skeletal animation ----
// idtx_anim_t* is a borrowed FAnimation* (owned by its FlatNode / the scene).
static inline const S::FAnimation* as_anim(const idtx_anim_t* a) {
    return reinterpret_cast<const S::FAnimation*>(a);
}
static inline const S::FAnimTrack* anim_track(const idtx_anim_t* a, int32_t t) {
    const S::FAnimation* fa = as_anim(a);
    if (!fa || t < 0 || t >= static_cast<int32_t>(fa->tracks.size())) return nullptr;
    return &fa->tracks[t];
}

IDTX_CORE_API idtx_anim_t* idtx_node_get_animation(const idtx_node_t* n) {
    const S::FAnimation* a = as_node(n)->animation.get();
    return reinterpret_cast<idtx_anim_t*>(const_cast<S::FAnimation*>(a));
}
IDTX_CORE_API float idtx_anim_get_length(const idtx_anim_t* a) {
    return a ? as_anim(a)->length : 0.0f;
}
IDTX_CORE_API int32_t idtx_anim_get_track_count(const idtx_anim_t* a) {
    return a ? static_cast<int32_t>(as_anim(a)->tracks.size()) : 0;
}
IDTX_CORE_API const char* idtx_anim_track_get_bone_name(const idtx_anim_t* a, int32_t t) {
    const S::FAnimTrack* tr = anim_track(a, t);
    return tr ? tr->bone_name.c_str() : "";
}
IDTX_CORE_API idtx_anim_track_type_t idtx_anim_track_get_type(const idtx_anim_t* a, int32_t t) {
    const S::FAnimTrack* tr = anim_track(a, t);
    if (!tr) return IDTX_ANIM_TRACK_TRANSLATION;
    switch (tr->type) {
        case S::FAnimTrackType::Rotation: return IDTX_ANIM_TRACK_ROTATION;
        case S::FAnimTrackType::Scale:    return IDTX_ANIM_TRACK_SCALE;
        default:                          return IDTX_ANIM_TRACK_TRANSLATION;
    }
}
IDTX_CORE_API int32_t idtx_anim_track_get_key_count(const idtx_anim_t* a, int32_t t) {
    const S::FAnimTrack* tr = anim_track(a, t);
    return tr ? static_cast<int32_t>(tr->times.size()) : 0;
}
IDTX_CORE_API double idtx_anim_track_get_key_time(const idtx_anim_t* a, int32_t t, int32_t k) {
    const S::FAnimTrack* tr = anim_track(a, t);
    if (!tr || k < 0 || k >= static_cast<int32_t>(tr->times.size())) return 0.0;
    return tr->times[k];
}
IDTX_CORE_API void idtx_anim_track_get_key_vec3(const idtx_anim_t* a, int32_t t, int32_t k, float out_xyz[3]) {
    const S::FAnimTrack* tr = anim_track(a, t);
    if (!tr || k < 0 || k >= static_cast<int32_t>(tr->vec3_keys.size())) {
        out_xyz[0] = out_xyz[1] = out_xyz[2] = 0.0f;
        return;
    }
    out_xyz[0] = tr->vec3_keys[k].x; out_xyz[1] = tr->vec3_keys[k].y; out_xyz[2] = tr->vec3_keys[k].z;
}
IDTX_CORE_API void idtx_anim_track_get_key_quat(const idtx_anim_t* a, int32_t t, int32_t k, float out_xyzw[4]) {
    const S::FAnimTrack* tr = anim_track(a, t);
    if (!tr || k < 0 || k >= static_cast<int32_t>(tr->quat_keys.size())) {
        out_xyzw[0] = out_xyzw[1] = out_xyzw[2] = 0.0f; out_xyzw[3] = 1.0f;
        return;
    }
    out_xyzw[0] = tr->quat_keys[k].x; out_xyzw[1] = tr->quat_keys[k].y;
    out_xyzw[2] = tr->quat_keys[k].z; out_xyzw[3] = tr->quat_keys[k].w;
}

IDTX_CORE_API void idtx_node_get_collision(const idtx_node_t* n, idtx_collision_shape_t* shape,
                                           idtx_axis_t* axis, double* height, double* radius) {
    const auto* p = as_node(n);
    if (shape) *shape = p->collision_shape; if (axis) *axis = p->axis;
    if (height) *height = p->col_height; if (radius) *radius = p->col_radius;
}
IDTX_CORE_API int32_t idtx_node_get_collision_type_count(const idtx_node_t* n) {
    return static_cast<int32_t>(as_node(n)->collision_types.size());
}
IDTX_CORE_API const char* idtx_node_get_collision_type(const idtx_node_t* n, int32_t i) {
    const auto& t = as_node(n)->collision_types;
    return (i >= 0 && i < static_cast<int32_t>(t.size())) ? t[i].c_str() : "";
}
IDTX_CORE_API void idtx_node_get_collision_root(const idtx_node_t* n, float out_color3[3],
                                                const char** out_identifier, int32_t* out_enabled, int32_t* out_highlightable) {
    const auto* p = as_node(n);
    if (out_color3) { out_color3[0] = p->highlight_color[0]; out_color3[1] = p->highlight_color[1]; out_color3[2] = p->highlight_color[2]; }
    if (out_identifier) *out_identifier = p->identifier.c_str();
    if (out_enabled) *out_enabled = p->enabled ? 1 : 0;
    if (out_highlightable) *out_highlightable = p->highlightable ? 1 : 0;
}

}  // extern "C"
