/**************************************************************************/
/*  FlatTreeTarget.h                                                      */
/**************************************************************************/
/* Copyright 2026 The openusd-fabric authors / V-Sekai contributors.      */
/* SPDX-License-Identifier: Apache-2.0 OR MPL-2.0                         */
/**************************************************************************/

// FlatTree — the in-core TargetEngine for the upstream UsdStageConverter.
//
// The converter framework (shared/include/idtxflow/converter/*) is templated on
// a TargetEngine and is engine-agnostic; the per-engine half is just the
// TargetEngineTypes specialization + the ConvertXxx hooks. This file is the
// core's mirror of idtxflow_godot/types/GodotTypes.h: instead of building
// godot::Node3D, the converter builds a flat tree of FlatNode that the C ABI
// (idtx_scene.h) hands to every host. So the EXACT upstream conversion logic
// (MeshConverter's leftHanded default, MaterialConverter, SkeletonConverter,
// the ConvertStagePostProcess up-axis/MPU rules) runs unchanged here — no
// reimplementation, no convention drift.
//
// Coordinate/unit conventions are NOT applied here: the FlatTree
// ConvertStagePostProcess records up_axis + metersPerUnit onto the scene and
// leaves geometry in stage space, exactly as documented in idtx_scene.h (the
// host applies the root swing/scale per engine).

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "idtx_core/idtx_core.h"     // idtx_mesh_t / idtx_skeleton_t / idtx_material_t
#include "idtx_core/idtx_scene.h"    // idtx_node_kind_t / idtx_axis_t / ...

#include "idtxflow/types/TargetTypes.h"

namespace idtx::core::scene {

// ---- POD math types satisfying the TargetTypes concepts (Vector*Like etc.) ----
struct FVec2 { float x = 0, y = 0; };
struct FVec3 { float x = 0, y = 0, z = 0; };
struct FVec4 { float x = 0, y = 0, z = 0, w = 0; };
struct FQuat { float x = 0, y = 0, z = 0, w = 1; };
struct FColor { float r = 1, g = 1, b = 1, a = 1; };

// TransformLike requires a class type of at least 16 floats. Row-major 4x4,
// the same layout the C ABI hands out via idtx_node_get_local_transform().
struct FTransform {
    float m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
};

// Mirror of GodotTypes' MeshData (same field names/shape so TargetMeshBuilder
// mirrors the Godot one), but with POD element types. idtx_scene.cpp flattens
// these into the idtx_mesh_set_* C ABI at finalize. Bones are padded to 4/vertex
// and weights normalized by the builder, exactly as the Godot builder does.
struct FMeshData {
    std::vector<FVec3>   Vertices;
    std::vector<int32_t> Triangles;
    std::vector<FVec3>   Normals;
    std::vector<FVec2>   UVs;
    std::vector<FColor>  VertexColors;
    std::vector<int32_t> Bones;
    std::vector<float>   Weights;
};

// Skeletal animation, flattened from the converter's AnimationDescription. One
// track per (joint, channel); each channel is keyed by time. Rotation keys live
// in quat_keys, translation/scale keys in vec3_keys (parallel to `times`). The
// host (idtx_scene.cpp -> idtx_anim getters) reads these to build its own clip.
enum class FAnimTrackType { Translation, Rotation, Scale };

struct FAnimTrack {
    std::string         bone_name;   // USD joint name (matches a skeleton bone)
    FAnimTrackType      type = FAnimTrackType::Translation;
    std::vector<double> times;
    std::vector<FVec3>  vec3_keys;   // Translation / Scale
    std::vector<FQuat>  quat_keys;   // Rotation
};

struct FAnimation {
    float                   length = 0.0f;   // seconds (max key time)
    std::vector<FAnimTrack> tracks;
};

// One converted prim. ConvertedEntity in the TargetEngine contract; the scene
// owns these (heap-allocated so pointers stay stable as the tree grows, unlike
// indices into a reallocating vector). Per-kind payload is a flat superset —
// only the fields relevant to `kind` are meaningful.
struct FlatNode {
    idtx_node_kind_t kind = IDTX_NODE_XFORM;
    std::string      name;
    std::string      path;            // full USD prim path
    FTransform       local_transform;
    int32_t          parent = -1;     // filled in ConvertPrimPostProcess
    int32_t          material_index = -1;

    // shared visual payload
    std::vector<float>  display_rgba; // count*4
    idtx_color_interp_t color_interp = IDTX_COLOR_INTERP_CONSTANT;

    // primitive params (cube/cone/cylinder/sphere)
    double      size = 0, radius = 0, height = 0;
    idtx_axis_t axis = IDTX_AXIS_Y;

    // mesh / skeleton — owned idtx_* handles built from FMeshData (idtx_scene.cpp)
    idtx_mesh_t*     mesh = nullptr;
    idtx_skeleton_t* skeleton = nullptr;
    idtx_mesh_t*     skinned_mesh = nullptr;
    FMeshData        mesh_data;        // staging; converted to `mesh` at finalize

    // skeletal animation (SKELETON kind); null when the skeleton has no clip.
    std::unique_ptr<FAnimation> animation;

    // collision / collision-root
    idtx_collision_shape_t collision_shape = IDTX_COLLISION_SHAPE_UNKNOWN;
    double                 col_height = 0, col_radius = 0;
    std::vector<std::string> collision_types;
    float       highlight_color[3] = {1, 0, 0};
    std::string identifier;
    bool        enabled = false, highlightable = false;
};

// OwningEntity / scene builder. Owns every FlatNode + the scene-wide material
// table, and carries the stage metadata recorded by ConvertStagePostProcess.
struct FlatScene {
    std::vector<std::unique_ptr<FlatNode>> nodes;
    std::vector<idtx_material_t*>          materials;   // owned
    idtx_axis_t up_axis = IDTX_AXIS_Y;
    double      meters_per_unit = 0.01;

    FlatNode* make_node() {
        nodes.push_back(std::make_unique<FlatNode>());
        return nodes.back().get();
    }
    int32_t index_of(const FlatNode* n) const {
        for (size_t i = 0; i < nodes.size(); ++i)
            if (nodes[i].get() == n) return static_cast<int32_t>(i);
        return -1;
    }
};

}  // namespace idtx::core::scene

namespace idtxflow::types {

// The TargetEngine tag, mirroring TargetEngineGodot.
struct TargetEngineFlatTree {
    static constexpr const char* name = "FlatTree";
};

template <>
struct TargetEngineTypes<TargetEngineFlatTree> {
    using Vector4 = idtx::core::scene::FVec4;
    using Vector3 = idtx::core::scene::FVec3;
    using Vector2 = idtx::core::scene::FVec2;
    using Quaternion = idtx::core::scene::FQuat;
    using Color = idtx::core::scene::FColor;
    using Transform = idtx::core::scene::FTransform;
    using MeshData = idtx::core::scene::FMeshData;
    using Index = int32_t;

    using Material = idtx_material_t*;
    using Texture = std::string;   // texture asset path; resolved host-side / Phase 1b

    using ConvertedEntity = idtx::core::scene::FlatNode;
    using OwningEntity = idtx::core::scene::FlatScene;
};

static_assert(TargetEngineTypesLike<TargetEngineTypes<TargetEngineFlatTree>>,
              "FlatTree engine types don't satisfy the TargetEngine concept");

}  // namespace idtxflow::types
