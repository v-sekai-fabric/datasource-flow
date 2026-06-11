/**************************************************************************/
/*  FlatTreeTypeConverter.h                                               */
/**************************************************************************/
/* Copyright 2026 V-Sekai contributors.                                   */
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
UsdTypeConverter<FT>::toVector3(const pxr::GfVec3d& v) {
    // p' = B p — rebase the point into the engine's Y-up frame (identity for
    // Y-up stages). Same basis used for normals and blend-shape deltas.
    const float* B = s_up_basis;
    const float x = float(v[0]), y = float(v[1]), z = float(v[2]);
    return { B[0]*x + B[1]*y + B[2]*z,
             B[3]*x + B[4]*y + B[5]*z,
             B[6]*x + B[7]*y + B[8]*z };
}

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
    // USD row-vector basis (rows) + translation.
    float R[9];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            R[r*3+c] = float(m[r][c]);
    float tr[3] = { float(m[3][0]), float(m[3][1]), float(m[3][2]) };

    // Spine-axis bake for cone/cylinder primitives (basis rows = basis * rot).
    if (spineAxis == pxr::UsdGeomTokens->x || spineAxis == pxr::UsdGeomTokens->z) {
        static const float rz[9] = {0,1,0, -1,0,0, 0,0,1};  // +90° about Z
        static const float rx[9] = {1,0,0, 0,0,1, 0,-1,0};  // +90° about X
        const float* rot = (spineAxis == pxr::UsdGeomTokens->x) ? rz : rx;
        float tmp[9];
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                tmp[r*3+c] = R[r*3+0]*rot[0*3+c] + R[r*3+1]*rot[1*3+c] + R[r*3+2]*rot[2*3+c];
        std::copy_n(tmp, 9, R);
    }

    // Up-axis change of basis: R' = B R B^T, t' = B t — rebases the transform into
    // the engine Y-up frame, consistent with the points/normals above. B is a
    // proper rotation so B^-1 = B^T (B[c*3+k] is B^T[k][c]).
    const float* B = s_up_basis;
    float BR[9];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            BR[r*3+c] = B[r*3+0]*R[0*3+c] + B[r*3+1]*R[1*3+c] + B[r*3+2]*R[2*3+c];
    float Rp[9];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            Rp[r*3+c] = BR[r*3+0]*B[c*3+0] + BR[r*3+1]*B[c*3+1] + BR[r*3+2]*B[c*3+2];
    const float Bt[3] = {
        B[0]*tr[0] + B[1]*tr[1] + B[2]*tr[2],
        B[3]*tr[0] + B[4]*tr[1] + B[5]*tr[2],
        B[6]*tr[0] + B[7]*tr[1] + B[8]*tr[2] };

    S::FTransform t;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            t.m[r*4+c] = Rp[r*3+c];
    t.m[12] = Bt[0]; t.m[13] = Bt[1]; t.m[14] = Bt[2];
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

// UV V-flip: USD's texture origin is bottom-left, glTF / Godot / three.js use
// top-left, so V maps as 1 - v. (The old `-v` only looked right under repeat
// wrap, where -v == 1 - v mod 1; it scrambled clamp-sampled hosts like the
// three.js/viser GLB path and left V outside [0,1].)
template <> inline S::FVec2
UsdMeshConverter<FT>::FlipUvV(const S::FVec2& input) { return {input.x, 1.0f - input.y}; }

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
