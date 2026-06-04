/**************************************************************************/
/*  idtx_scene.cpp                                                        */
/**************************************************************************/
/* Copyright 2026 The openusd-fabric authors / V-Sekai contributors.      */
/* SPDX-License-Identifier: Apache-2.0 OR MPL-2.0                         */
/**************************************************************************/

// Implementation of idtx_scene.h: open a USD stage, run the upstream converter
// with the in-core FlatTree target, finalize staged mesh data into idtx_mesh
// handles, and expose the result through the flat C ABI. The converter logic
// (StageConverter + Mesh/Skeleton/Material converters) is the upstream code,
// unchanged — only the TargetEngine (FlatTree) is ours.

#include "idtx_core/idtx_scene.h"

#include <string>
#include <vector>

#include <pxr/usd/usd/stage.h>

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

    return mesh;
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
