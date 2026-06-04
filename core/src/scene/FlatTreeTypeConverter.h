/**************************************************************************/
/*  FlatTreeTypeConverter.h                                               */
/**************************************************************************/
/* Copyright 2026 The openusd-fabric authors / V-Sekai contributors.      */
/* SPDX-License-Identifier: Apache-2.0 OR MPL-2.0                         */
/**************************************************************************/

// FlatTree specializations of UsdTypeConverter + TargetMeshBuilder — the core
// mirror of idtxflow_godot/converter/UsdGodotTypeConverter.h. Same math, POD
// outputs. Kept structurally parallel to the Godot version for side-by-side
// review (CHI: disconnect OpenUSD from hosts).

#pragma once

#include <cmath>
#include <optional>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/tf/token.h>
#include <pxr/usd/usdGeom/tokens.h>

#include "idtxflow/converter/TypeConverter.h"
#include "idtxflow/converter/MeshConverter.h"  // for UsdMeshConverter<>::FlipUvV

#include "scene/FlatTreeTarget.h"

namespace idtxflow::converter {

using FT = idtxflow::types::TargetEngineFlatTree;
namespace S = idtx::core::scene;

template <> inline S::FVec2
UsdTypeConverter<FT>::toVector2(const pxr::GfVec2d& v) { return {float(v[0]), float(v[1])}; }

template <> inline S::FVec3
UsdTypeConverter<FT>::toVector3(const pxr::GfVec3d& v) { return {float(v[0]), float(v[1]), float(v[2])}; }

template <> inline S::FVec4
UsdTypeConverter<FT>::toVector4(const pxr::GfVec4d& v) { return {float(v[0]), float(v[1]), float(v[2]), float(v[3])}; }

template <> inline S::FQuat
UsdTypeConverter<FT>::toQuaternion(const pxr::GfQuatd& q) {
    const pxr::GfVec3d i = q.GetImaginary();
    return {float(i[0]), float(i[1]), float(i[2]), float(q.GetReal())};
}

template <> inline S::FColor
UsdTypeConverter<FT>::toColor(const pxr::GfVec4f& c) { return {c[0], c[1], c[2], c[3]}; }

// Row-major 4x4 in USD row-vector convention (point' = point * M); translation
// in m[12..14], same bytes the C ABI hands out. Bakes the spine-axis rotation
// for cone/cylinder exactly as the Godot toTransform does (x-spine -> +90° Z,
// z-spine -> +90° X), via a 3x3 multiply on the basis rows.
template <> inline S::FTransform
UsdTypeConverter<FT>::toTransform(const pxr::GfMatrix4d& m, const pxr::TfToken& spineAxis) {
    S::FTransform t;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            t.m[r * 4 + c] = float(m[r][c]);
    t.m[12] = float(m[3][0]); t.m[13] = float(m[3][1]); t.m[14] = float(m[3][2]);

    if (spineAxis == pxr::UsdGeomTokens->x || spineAxis == pxr::UsdGeomTokens->z) {
        // rot[3][3] = +90° about Z (x-spine) or X (z-spine)
        static const float rz[9] = {0,1,0, -1,0,0, 0,0,1};  // +90° about Z
        static const float rx[9] = {1,0,0, 0,0,1, 0,-1,0};  // +90° about X
        float rot[3][3];
        std::copy_n(spineAxis == pxr::UsdGeomTokens->x ? rz : rx, 9, &rot[0][0]);
        // basis (rows 0..2) = basis * rot
        for (int r = 0; r < 3; ++r) {
            float row[3] = {t.m[r*4+0], t.m[r*4+1], t.m[r*4+2]};
            for (int c = 0; c < 3; ++c)
                t.m[r*4+c] = row[0]*rot[0][c] + row[1]*rot[1][c] + row[2]*rot[2][c];
        }
    }
    return t;
}

// Texture + material: deferred to Phase 1b. For 1a, meshes fall back to display
// colors / vertex colors (handled host-side), so no material is produced yet.
template <> inline std::optional<std::string>
UsdTypeConverter<FT>::toTexture(const std::vector<uint8_t>&, const std::string&, TexturePurpose) {
    return std::nullopt;
}

template <> inline std::optional<idtx_material_t*>
UsdTypeConverter<FT>::toMaterial(const types::MaterialDescription<std::string>&, const pxr::UsdStageRefPtr&) {
    return std::nullopt;
}

// UV V-flip — engine-specific. Mirror the Godot convention (negate V).
template <> inline S::FVec2
UsdMeshConverter<FT>::FlipUvV(const S::FVec2& input) { return {input.x, -input.y}; }

// Mirror of TargetMeshBuilder<Godot>: push position/normal/uv; pad bones to 4
// and normalize weights; indices into Triangles (winding already corrected in
// MeshConverter::BuildMesh).
template <>
class TargetMeshBuilder<FT> {
public:
    using Types = idtxflow::types::TargetEngineTypes<FT>;

    void AddVertex(S::FMeshData& mesh,
                   const S::FVec3& position, const S::FVec3& normal, const S::FVec2& uv,
                   const std::vector<uint32_t>& bones = {},
                   const std::vector<float>& boneWeights = {}) {
        mesh.Vertices.push_back(position);
        mesh.Normals.push_back(normal);
        mesh.UVs.push_back(uv);
        if (!bones.empty()) {
            float weightSum = 0.0f;
            for (size_t i = 0; i < 4; ++i) {
                if (i < bones.size()) {
                    mesh.Bones.push_back(static_cast<int32_t>(bones[i]));
                    mesh.Weights.push_back(boneWeights[i]);
                    weightSum += boneWeights[i];
                } else {
                    mesh.Bones.push_back(0);
                    mesh.Weights.push_back(0.0f);
                }
            }
            const int64_t base = static_cast<int64_t>(mesh.Bones.size()) - 4;
            if (weightSum > 0.0f)
                for (int i = 0; i < 4; ++i) mesh.Weights[base + i] /= weightSum;
        }
    }

    int32_t GetVertexCount(const S::FMeshData& mesh) { return static_cast<int32_t>(mesh.Vertices.size()); }
    void    AddIndex(S::FMeshData& mesh, int32_t index) { mesh.Triangles.push_back(index); }
};

}  // namespace idtxflow::converter
