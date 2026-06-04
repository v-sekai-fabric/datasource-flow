/**************************************************************************/
/*  FlatTreeStageConverter.h                                              */
/**************************************************************************/
/* Copyright 2026 V-Sekai contributors.                                   */
/* SPDX-License-Identifier: Apache-2.0 OR MPL-2.0                         */
/**************************************************************************/

// FlatTree specializations of UsdStageConverter<TargetEngine>::ConvertXxx — the
// core mirror of source/converter/UsdGodotStageConverter.h. Each hook, instead
// of building a godot::Node3D, appends a scene::FlatNode to the OwningEntity
// (the FlatScene). Geometry subset (Phase 1a): animation, real materials,
// pseudo-instancing and deferred payloads are stubbed/simplified — see notes.

#pragma once

#include <algorithm>

#include <pxr/base/tf/token.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xformCache.h>

#include "idtxflow/converter/StageConverter.h"
#include "idtxflow/converter/TypeConverter.h"

#include "scene/FlatTreeTarget.h"
#include "scene/FlatTreeTypeConverter.h"
#include "usd_internal.h"  // idtx::core::detail::read_material

namespace idtxflow::converter {

namespace flat_detail {

using S = idtx::core::scene::FlatScene;
using N = idtx::core::scene::FlatNode;

inline idtx_color_interp_t interp_of(const pxr::TfToken& t) {
    if (t == pxr::UsdGeomTokens->uniform)     return IDTX_COLOR_INTERP_UNIFORM;
    if (t == pxr::UsdGeomTokens->varying)     return IDTX_COLOR_INTERP_VARYING;
    if (t == pxr::UsdGeomTokens->vertex)      return IDTX_COLOR_INTERP_VERTEX;
    if (t == pxr::UsdGeomTokens->faceVarying) return IDTX_COLOR_INTERP_FACE_VARYING;
    return IDTX_COLOR_INTERP_CONSTANT;
}

inline idtx_axis_t axis_of(const pxr::GfVec3f& a) {
    if (a == pxr::GfVec3f::XAxis()) return IDTX_AXIS_X;
    if (a == pxr::GfVec3f::ZAxis()) return IDTX_AXIS_Z;
    return IDTX_AXIS_Y;
}

inline idtx_collision_shape_t shape_of(const pxr::TfToken& s) {
    const std::string& v = s.GetString();
    if (v == "Sphere")   return IDTX_COLLISION_SHAPE_SPHERE;
    if (v == "Capsule")  return IDTX_COLLISION_SHAPE_CAPSULE;
    if (v == "Cylinder") return IDTX_COLLISION_SHAPE_CYLINDER;
    if (v == "Cone")     return IDTX_COLLISION_SHAPE_CONE;
    if (v == "Mesh")     return IDTX_COLLISION_SHAPE_MESH;
    if (v == "Cube")     return IDTX_COLLISION_SHAPE_CUBE;
    return IDTX_COLLISION_SHAPE_UNKNOWN;
}

// Combine USD displayColor (rgb) + displayOpacity (a, already merged into rgba
// by the caller) into the node's flat rgba buffer.
inline void set_display(N* n, const pxr::VtArray<pxr::GfVec4f>& colors, const pxr::TfToken& interp) {
    n->color_interp = interp_of(interp);
    n->display_rgba.reserve(colors.size() * 4);
    for (const auto& c : colors) { n->display_rgba.insert(n->display_rgba.end(), {c[0], c[1], c[2], c[3]}); }
}

// Resolve one MeshDescription's bound material into the scene material table,
// returning its index (-1 if unbound). Deduplicated by USD prim path, so a
// material shared across meshes is read once and shared.
template <typename MD>
inline int32_t material_of(idtx::core::scene::FlatScene* scene, const MD& md) {
    if (!md.usdMaterial) { return -1; }
    const std::string path = md.usdMaterial.GetPrim().GetPath().GetString();
    std::map<std::string, int32_t>::const_iterator it = scene->material_index_by_path.find(path);
    if (it != scene->material_index_by_path.end()) { return it->second; }
    scene->materials.push_back(idtx::core::detail::read_material(md.usdMaterial.GetPrim()));
    const int32_t idx = static_cast<int32_t>(scene->materials.size()) - 1;
    scene->material_index_by_path[path] = idx;
    return idx;
}

// First bound material among `mds` (for single-surface mesh nodes), deduped.
template <typename MD>
inline int32_t first_material(idtx::core::scene::FlatScene* scene, const std::vector<MD>& mds) {
    for (const auto& md : mds) {
        const int32_t mi = material_of(scene, md);
        if (mi >= 0) { return mi; }
    }
    return -1;
}

// Append src mesh into dst, offsetting indices — collapses USD material subsets
// into one idtx_mesh (idtx_mesh is single-surface; 1a has no per-subset material).
inline void merge_mesh(idtx::core::scene::FMeshData& dst, const idtx::core::scene::FMeshData& src) {
    const int32_t base = static_cast<int32_t>(dst.Vertices.size());
    dst.Vertices.insert(dst.Vertices.end(), src.Vertices.begin(), src.Vertices.end());
    dst.Normals.insert(dst.Normals.end(), src.Normals.begin(), src.Normals.end());
    dst.UVs.insert(dst.UVs.end(), src.UVs.begin(), src.UVs.end());
    dst.VertexColors.insert(dst.VertexColors.end(), src.VertexColors.begin(), src.VertexColors.end());
    dst.Bones.insert(dst.Bones.end(), src.Bones.begin(), src.Bones.end());
    dst.Weights.insert(dst.Weights.end(), src.Weights.begin(), src.Weights.end());
    for (int32_t idx : src.Triangles) dst.Triangles.push_back(base + idx);

    // Merge blend shapes by name: each src target's sparse deltas move into dst
    // with vertex indices shifted by `base`, so same-named shapes across the
    // collapsed skin targets accumulate into one set.
    for (const idtx::core::scene::FBlendShape& sbs : src.BlendShapes) {
        idtx::core::scene::FBlendShape* dbs = nullptr;
        for (idtx::core::scene::FBlendShape& d : dst.BlendShapes) {
            if (d.name == sbs.name) { dbs = &d; break; }
        }
        if (dbs == nullptr) {
            idtx::core::scene::FBlendShape created;
            created.name = sbs.name;
            created.weight = sbs.weight;
            dst.BlendShapes.push_back(std::move(created));
            dbs = &dst.BlendShapes.back();
        }
        const bool with_normals = sbs.has_normals
            && sbs.nrm_offsets.size() == sbs.indices.size();
        for (size_t k = 0; k < sbs.indices.size(); ++k) {
            dbs->indices.push_back(base + sbs.indices[k]);
            dbs->pos_offsets.push_back(sbs.pos_offsets[k]);
            if (with_normals) { dbs->nrm_offsets.push_back(sbs.nrm_offsets[k]); }
        }
        if (with_normals) { dbs->has_normals = true; }
    }
}

// Append `src` into `dst` (shared vertex buffer) and record the appended index
// range as a material subset (UsdGeomSubset, familyName "materialBind"). Lets a
// prim's GeomSubsets become one multi-material mesh instead of being merged into
// a single material.
inline void append_subset(idtx::core::scene::FMeshData& dst,
                          const idtx::core::scene::FMeshData& src, int32_t material) {
    const int32_t idx_offset = static_cast<int32_t>(dst.Triangles.size());
    merge_mesh(dst, src);
    const int32_t idx_count = static_cast<int32_t>(dst.Triangles.size()) - idx_offset;
    if (idx_count > 0) {
        dst.Subsets.push_back(idtx::core::scene::FSubset{material, idx_offset, idx_count});
    }
}

}  // namespace flat_detail

using FT = idtxflow::types::TargetEngineFlatTree;
namespace SC = idtx::core::scene;

namespace flat_detail {

// Flatten the converter's AnimationDescription into the FlatNode representation.
// Rotation keys -> quat_keys, translation/scale -> vec3_keys (parallel to times).
// Returns null when there are no usable tracks. The clip length is the max key
// time (the upstream AnimationDescription::Length is left unset for skel clips).
inline std::unique_ptr<SC::FAnimation> to_flat_animation(const AnimationDescription<FT>& src) {
    auto out = std::make_unique<SC::FAnimation>();
    double max_time = 0.0;
    for (const auto& t : src.Tracks) {
        SC::FAnimTrack track;
        track.bone_name = t.Name;
        switch (t.Type) {
            case TRACK_POSITION: {
                track.type = SC::FAnimTrackType::Translation;
            } break;
            case TRACK_ROTATION: {
                track.type = SC::FAnimTrackType::Rotation;
            } break;
            case TRACK_SCALE: {
                track.type = SC::FAnimTrackType::Scale;
            } break;
            default: {
                continue;  // TRACK_TRANSFORM is not used for skeletons
            }
        }
        for (const auto& key : t.Keys) {
            track.times.push_back(key.Time);
            if (key.Time > max_time) {
                max_time = key.Time;
            }
            if (track.type == SC::FAnimTrackType::Rotation) {
                track.quat_keys.push_back(std::get<SC::FQuat>(key.Value));
            } else {
                track.vec3_keys.push_back(std::get<SC::FVec3>(key.Value));
            }
        }
        if (!track.times.empty()) {
            out->tracks.push_back(std::move(track));
        }
    }
    if (out->tracks.empty()) {
        return nullptr;
    }
    out->length = static_cast<float>(max_time);
    return out;
}

}  // namespace flat_detail

template <> inline SC::FlatNode* UsdStageConverter<FT>::ConvertXform(
    const SC::FTransform& transform,
    const std::optional<AnimationDescription<FT>>& /*animation*/) {
    SC::FlatNode* n = OwningEntity->make_node();
    n->kind = IDTX_NODE_XFORM;
    n->local_transform = transform;
    return n;  // animation -> Phase 1b
}

template <> inline SC::FlatNode* UsdStageConverter<FT>::ConvertCube(
    const SC::FTransform& transform, const std::optional<AnimationDescription<FT>>&,
    const std::optional<idtx_material_t*>&, float cube_size,
    const pxr::VtArray<pxr::GfVec4f>& colors, const pxr::TfToken& interp) {
    SC::FlatNode* n = OwningEntity->make_node();
    n->kind = IDTX_NODE_CUBE; n->local_transform = transform; n->size = cube_size;
    flat_detail::set_display(n, colors, interp);
    return n;
}

template <> inline SC::FlatNode* UsdStageConverter<FT>::ConvertCylinder(
    const SC::FTransform& transform, const std::optional<AnimationDescription<FT>>&,
    const std::optional<idtx_material_t*>&, float radius, float height,
    const pxr::VtArray<pxr::GfVec4f>& colors, const pxr::TfToken& interp) {
    SC::FlatNode* n = OwningEntity->make_node();
    n->kind = IDTX_NODE_CYLINDER; n->local_transform = transform; n->radius = radius; n->height = height;
    flat_detail::set_display(n, colors, interp);
    return n;
}

template <> inline SC::FlatNode* UsdStageConverter<FT>::ConvertCone(
    const SC::FTransform& transform, const std::optional<AnimationDescription<FT>>&,
    const std::optional<idtx_material_t*>&, float radius, float height,
    const pxr::VtArray<pxr::GfVec4f>& colors, const pxr::TfToken& interp) {
    SC::FlatNode* n = OwningEntity->make_node();
    n->kind = IDTX_NODE_CONE; n->local_transform = transform; n->radius = radius; n->height = height;
    flat_detail::set_display(n, colors, interp);
    return n;
}

template <> inline SC::FlatNode* UsdStageConverter<FT>::ConvertSphere(
    const SC::FTransform& transform, const std::optional<AnimationDescription<FT>>&,
    const std::optional<idtx_material_t*>&, float radius,
    const pxr::VtArray<pxr::GfVec4f>& colors, const pxr::TfToken& interp) {
    SC::FlatNode* n = OwningEntity->make_node();
    n->kind = IDTX_NODE_SPHERE; n->local_transform = transform; n->radius = radius;
    flat_detail::set_display(n, colors, interp);
    return n;
}

template <> inline SC::FlatNode* UsdStageConverter<FT>::ConvertCollisionRoot(
    const SC::FTransform& transform, const pxr::GfVec3f highlight_color,
    const std::string identifier, const bool enabled, const bool highlightable) {
    SC::FlatNode* n = OwningEntity->make_node();
    n->kind = IDTX_NODE_COLLISION_ROOT; n->local_transform = transform;
    n->highlight_color[0] = highlight_color[0]; n->highlight_color[1] = highlight_color[1]; n->highlight_color[2] = highlight_color[2];
    n->identifier = identifier; n->enabled = enabled; n->highlightable = highlightable;
    return n;
}

template <> inline SC::FlatNode* UsdStageConverter<FT>::ConvertCollision(
    const SC::FTransform& transform, const pxr::TfToken shape, const pxr::VtArray<pxr::TfToken> types,
    const pxr::GfVec3f axis, const double height, const double radius) {
    SC::FlatNode* n = OwningEntity->make_node();
    n->kind = IDTX_NODE_COLLISION; n->local_transform = transform;
    n->collision_shape = flat_detail::shape_of(shape); n->axis = flat_detail::axis_of(axis);
    n->col_height = height; n->col_radius = radius;
    for (const auto& t : types) n->collision_types.push_back(t.GetString());
    return n;
}

template <> inline SC::FlatNode* UsdStageConverter<FT>::ConvertMesh(
    const SC::FTransform& transform, const std::optional<AnimationDescription<FT>>&,
    const std::vector<MeshDescription<UsdMeshConverter<FT>::MeshDataType>>& mesh_descriptions,
    const pxr::VtArray<pxr::GfVec4f>& colors, const pxr::TfToken& interp) {
    SC::FlatNode* n = OwningEntity->make_node();
    n->kind = IDTX_NODE_MESH; n->local_transform = transform;
    flat_detail::set_display(n, colors, interp);
    // GeomSubsets -> material subsets of one mesh (multi-material static mesh).
    for (const auto& md : mesh_descriptions)
        flat_detail::append_subset(n->mesh_data, md.meshData, flat_detail::material_of(OwningEntity, md));
    n->material_index = n->mesh_data.Subsets.empty() ? -1 : n->mesh_data.Subsets.front().material;
    return n;  // mesh_data -> idtx_mesh at finalize (idtx_scene.cpp)
}

template <> inline SC::FlatNode* UsdStageConverter<FT>::ConvertSkeleton(
    const SC::FTransform& transform, const std::optional<AnimationDescription<FT>>& anim,
    const SkeletonDescription<FT>& skel) {
    SC::FlatNode* n = OwningEntity->make_node();
    n->kind = IDTX_NODE_SKELETON; n->local_transform = transform;
    n->skeleton = idtx_skeleton_create();
    if (anim && !anim->Tracks.empty()) {
        n->animation = flat_detail::to_flat_animation(*anim);
    }
    for (const auto& bone : skel.Bones)
        idtx_skeleton_add_bone(n->skeleton, bone.Name.c_str(), bone.parentIndex,
                               bone.restTransform.m, bone.bindPose.m);
    // One skinned mesh PER source skin target (USD mesh prim); its GeomSubsets
    // become material subsets of that mesh, so the authored per-material bindings
    // survive instead of collapsing to one. Separate prims stay separate meshes.
    for (const auto& target : skel.SkinTargets) {
        SC::FMeshData surf;
        for (const auto& md : target.MeshDescriptions) {
            flat_detail::append_subset(surf, md.meshData, flat_detail::material_of(OwningEntity, md));
        }
        if (!surf.Vertices.empty()) {
            n->skin_materials.push_back(surf.Subsets.empty() ? -1 : surf.Subsets.front().material);
            n->skin_names.push_back(target.Name);   // preserve the skin-target prim name
            n->skin_surfaces.push_back(std::move(surf));
        }
    }
    return n;  // skin_surfaces -> skinned_meshes at finalize
}

// Pseudo-instancing -> Phase 1b. For 1a, convert each occurrence as a plain
// Gprim (loses the multimesh dedup, but renders correctly).
template <> inline SC::FlatNode* UsdStageConverter<FT>::ConvertGprimPseudoInstance(
    const SC::FTransform&, const pxr::UsdGeomGprim& gprim, const pxr::SdfPath&) {
    return ConvertGprim(gprim);
}

// Deferred payloads -> Phase 1b. The core opens stages with LoadAll, so a prim
// with an unloaded payload should not occur; return nothing if it does.
template <> inline SC::FlatNode* UsdStageConverter<FT>::ConvertPrimWithPayload(
    const pxr::UsdPrim&, const std::string&, const SC::FTransform&, const pxr::SdfLayerRefPtr&) {
    return nullptr;
}

template <> inline SC::FlatNode* UsdStageConverter<FT>::ConvertPrimPostProcess(
    const pxr::UsdPrim& usd_prim, SC::FlatNode* converted, SC::FlatNode* parent) {
    if (!converted) return nullptr;
    converted->name = usd_prim.GetName().GetString();
    converted->path = usd_prim.GetPath().GetString();
    if (parent) converted->parent = OwningEntity->index_of(parent);
    return converted;
}

// Record the stage's up-axis + MPU onto the scene; the HOST applies the root
// swing/scale (see idtx_scene.h conventions). Geometry stays in stage space.
template <> inline std::vector<SC::FlatNode*> UsdStageConverter<FT>::ConvertStagePostProcess(
    const std::vector<SC::FlatNode*>& entities) {
    OwningEntity->meters_per_unit = StageMetersPerUnit;
    // The up-axis change of basis was already baked into every transform and
    // point during conversion (TypeConverter::set_up_axis_basis), so the scene is
    // now Y-up regardless of the stage's authored axis. Report Y so hosts don't
    // re-apply a root rotation on top of the already-rebased data.
    OwningEntity->up_axis = IDTX_AXIS_Y;
    return entities;
}

}  // namespace idtxflow::converter
