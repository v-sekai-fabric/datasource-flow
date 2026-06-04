// Copyright 2026 V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// idtx_core_export_avatar_to_usd — write an idtx_avatar_t* tree into a
// USD file at `path`. Lifts the body of UsdGodotStageExporter's
// ExportSceneToFile into the engine-agnostic core.

#include "idtx_core/idtx_core.h"
#include "idtx_core/internal/string_utils.h"
#include "idtx_core/internal/usd_helpers.h"

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/reference.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/sdf/valueTypeName.h>
#include <pxr/usd/usd/editTarget.h>
#include <pxr/usd/usd/references.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformOp.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdShade/tokens.h>
#include <pxr/usd/usdGeom/capsule.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/cylinder.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdSkel/bindingAPI.h>
#include <pxr/usd/usdSkel/root.h>
#include <pxr/usd/usdSkel/skeleton.h>
#include <pxr/usd/usdSkel/tokens.h>

#include <set>
#include <string>
#include <vector>

namespace idtx::core::detail {

// Emit the avatar's root Xform under `parent_path` with the avatar's
// root_transform. Returns the resulting prim path.
static pxr::SdfPath emit_root_xform(
    pxr::UsdStageRefPtr const& stage,
    pxr::SdfPath const& parent_path,
    idtx_avatar_t const* avatar)
{
    std::string root_name = sanitise_prim_name(idtx_avatar_get_name(avatar));
    pxr::SdfPath root_path = parent_path.AppendChild(pxr::TfToken(root_name));
    pxr::UsdGeomXform xf = pxr::UsdGeomXform::Define(stage, root_path);

    float m[16];
    idtx_avatar_get_root_transform(avatar, m);
    // MakeMatrixXform (not AddTransformOp): it resets xformOpOrder before
    // adding the single matrix op, so it is idempotent. In the layer-aware
    // modes (OVERLAY / FLATTEN) the root prim already carries an
    // xformOp:transform from the composed source; AddTransformOp would
    // raise "xformOp:transform already exists" and leave a duplicate /
    // inverse op the value can't be set on. MakeMatrixXform overrides
    // cleanly and is identical to AddTransformOp on a fresh flat stage.
    auto op = xf.MakeMatrixXform();
    op.Set(float16_to_gf_matrix(m));

    return root_path;
}

// Emit UsdSkelRoot + UsdSkelSkeleton under `parent_path`. Returns the
// SkelRoot path (empty SdfPath if the skeleton handle is NULL / empty).
// Mirrors UsdGodotStageExporter::ExportSkeleton but reads from the
// engine-agnostic idtx_skeleton_t handle.
static pxr::SdfPath emit_skeleton(
    pxr::UsdStageRefPtr const& stage,
    pxr::SdfPath const& parent_path,
    idtx_skeleton_t const* skel)
{
    if (skel == nullptr) return pxr::SdfPath();
    int32_t n = idtx_skeleton_get_bone_count(skel);
    if (n <= 0) return pxr::SdfPath();

    std::string skel_root_name = sanitise_prim_name(idtx_skeleton_get_name(skel)) + "_SkelRoot";
    pxr::SdfPath root_path = parent_path.AppendChild(pxr::TfToken(skel_root_name));
    pxr::UsdSkelRoot::Define(stage, root_path);

    pxr::SdfPath skel_path = root_path.AppendChild(pxr::TfToken("Skeleton"));
    pxr::UsdSkelSkeleton usd_skel = pxr::UsdSkelSkeleton::Define(stage, skel_path);

    // Build skeleton-relative joint paths: each bone's joint identifier
    // is its parent's path + "/" + sanitised name; root bones live at
    // the top level. Matches the UsdSkel convention.
    std::vector<std::string> joint_paths(static_cast<size_t>(n));
    pxr::VtArray<pxr::TfToken>     joints;
    pxr::VtArray<pxr::GfMatrix4d>  rest_transforms;
    pxr::VtArray<pxr::GfMatrix4d>  bind_transforms;
    joints.reserve(n);
    rest_transforms.reserve(n);
    bind_transforms.reserve(n);

    for (int32_t i = 0; i < n; ++i) {
        std::string san = sanitise_prim_name(idtx_skeleton_get_bone_name(skel, i));
        int32_t parent = idtx_skeleton_get_bone_parent(skel, i);
        joint_paths[i] = (parent < 0)
            ? san
            : (joint_paths[static_cast<size_t>(parent)] + "/" + san);
        joints.push_back(pxr::TfToken(joint_paths[i]));

        float rest[16]; idtx_skeleton_get_bone_rest(skel, i, rest);
        float bind[16]; idtx_skeleton_get_bone_bind(skel, i, bind);
        rest_transforms.push_back(float16_to_gf_matrix(rest));
        bind_transforms.push_back(float16_to_gf_matrix(bind));
    }

    usd_skel.CreateJointsAttr().Set(joints);
    usd_skel.CreateRestTransformsAttr().Set(rest_transforms);
    usd_skel.CreateBindTransformsAttr().Set(bind_transforms);

    return root_path;
}

// Emit one UsdGeomMesh prim for `mesh` under `parent_path`. Returns
// the mesh prim path. `siblings` tracks already-used names so multi-
// mesh avatars don't collide on identically-named meshes.
static pxr::SdfPath emit_mesh(
    pxr::UsdStageRefPtr const& stage,
    pxr::SdfPath const& parent_path,
    idtx_mesh_t const* mesh,
    std::set<std::string>& siblings)
{
    if (mesh == nullptr) return pxr::SdfPath();
    int32_t vc = idtx_mesh_get_vertex_count(mesh);
    int32_t ic = idtx_mesh_get_index_count(mesh);
    if (vc <= 0 || ic <= 0) return pxr::SdfPath();

    std::string desired = sanitise_prim_name(idtx_mesh_get_name(mesh));
    if (desired.empty()) desired = "Mesh";
    pxr::SdfPath mesh_path = unique_child_path(parent_path, desired, siblings);
    pxr::UsdGeomMesh usd_mesh = pxr::UsdGeomMesh::Define(stage, mesh_path);

    // UsdGeomMesh's subdivisionScheme defaults to "catmullClark" — which
    // is wrong for game-content geometry. Always emit "none" so consumers
    // treat the mesh as the polygon set it actually is, and so the
    // round-trip diff stays clean for fixtures that author it explicitly.
    usd_mesh.CreateSubdivisionSchemeAttr().Set(pxr::UsdGeomTokens->none);

    // Positions
    std::vector<float> positions(static_cast<size_t>(vc) * 3);
    idtx_mesh_get_positions(mesh, positions.data());
    pxr::VtArray<pxr::GfVec3f> points;
    points.reserve(vc);
    for (int32_t i = 0; i < vc; ++i) {
        points.push_back(pxr::GfVec3f(
            positions[i * 3 + 0],
            positions[i * 3 + 1],
            positions[i * 3 + 2]));
    }
    usd_mesh.CreatePointsAttr().Set(points);

    // Indices. faceVertexCounts comes from the mesh's override array
    // when present (n-gon round-trip from CHI-252 sidecar), otherwise
    // we fall back to a triangle-list assumption.
    pxr::VtArray<int> face_vertex_counts;
    int32_t fvc_count = idtx_mesh_get_face_vertex_count_count(mesh);
    if (fvc_count > 0) {
        std::vector<int32_t> fvc(static_cast<size_t>(fvc_count));
        idtx_mesh_get_face_vertex_counts(mesh, fvc.data());
        face_vertex_counts.reserve(fvc_count);
        for (int32_t i = 0; i < fvc_count; ++i) {
            face_vertex_counts.push_back(static_cast<int>(fvc[i]));
        }
    } else {
        int32_t face_count = ic / 3;
        face_vertex_counts.assign(face_count, 3);
    }
    pxr::VtArray<int> face_vertex_indices;
    face_vertex_indices.reserve(ic);
    {
        std::vector<int32_t> indices(static_cast<size_t>(ic));
        idtx_mesh_get_indices(mesh, indices.data());
        for (int32_t i = 0; i < ic; ++i) {
            face_vertex_indices.push_back(static_cast<int>(indices[i]));
        }
    }
    usd_mesh.CreateFaceVertexCountsAttr().Set(face_vertex_counts);
    usd_mesh.CreateFaceVertexIndicesAttr().Set(face_vertex_indices);

    // Normals (vertex-interpolated)
    if (idtx_mesh_has_normals(mesh)) {
        std::vector<float> n_buf(static_cast<size_t>(vc) * 3);
        idtx_mesh_get_normals(mesh, n_buf.data());
        pxr::VtArray<pxr::GfVec3f> normals;
        normals.reserve(vc);
        for (int32_t i = 0; i < vc; ++i) {
            normals.push_back(pxr::GfVec3f(
                n_buf[i * 3 + 0],
                n_buf[i * 3 + 1],
                n_buf[i * 3 + 2]));
        }
        auto attr = usd_mesh.CreateNormalsAttr();
        attr.Set(normals);
        usd_mesh.SetNormalsInterpolation(pxr::UsdGeomTokens->vertex);
    }

    // UVs as primvar:st (vertex-interpolated)
    if (idtx_mesh_has_uvs(mesh)) {
        std::vector<float> uv_buf(static_cast<size_t>(vc) * 2);
        idtx_mesh_get_uvs(mesh, uv_buf.data());
        pxr::VtArray<pxr::GfVec2f> uvs;
        uvs.reserve(vc);
        for (int32_t i = 0; i < vc; ++i) {
            uvs.push_back(pxr::GfVec2f(uv_buf[i * 2 + 0], uv_buf[i * 2 + 1]));
        }
        pxr::UsdGeomPrimvarsAPI pvapi(usd_mesh.GetPrim());
        auto st = pvapi.CreatePrimvar(
            pxr::TfToken("st"),
            pxr::SdfValueTypeNames->TexCoord2fArray,
            pxr::UsdGeomTokens->vertex);
        st.Set(uvs);
    }

    // Display color (per-vertex)
    if (idtx_mesh_has_colors(mesh)) {
        std::vector<float> c_buf(static_cast<size_t>(vc) * 4);
        idtx_mesh_get_colors(mesh, c_buf.data());
        pxr::VtArray<pxr::GfVec3f> rgb;
        rgb.reserve(vc);
        for (int32_t i = 0; i < vc; ++i) {
            // Drop alpha — USD displayColor is RGB; alpha would go in
            // displayOpacity if needed (omitted for the MVP).
            rgb.push_back(pxr::GfVec3f(
                c_buf[i * 4 + 0],
                c_buf[i * 4 + 1],
                c_buf[i * 4 + 2]));
        }
        auto attr = usd_mesh.CreateDisplayColorAttr();
        attr.Set(rgb);
        usd_mesh.SetNormalsInterpolation(pxr::UsdGeomTokens->vertex);
    }

    return mesh_path;
}

// Emit a UsdShadeMaterial under `parent_path` from idtx_material_t.
// PBR materials get a UsdPreviewSurface shader subtree; MToon-flagged
// materials get the same surface plus the VSekaiMToonAPI applied
// schema so downstream code (or a future VRM serializer) can find the
// MToon parameters.
static pxr::SdfPath emit_material(
    pxr::UsdStageRefPtr const& stage,
    pxr::SdfPath const& parent_path,
    idtx_material_t const* mat,
    std::set<std::string>& siblings)
{
    if (mat == nullptr) return pxr::SdfPath();
    std::string desired = sanitise_prim_name(idtx_material_get_name(mat));
    if (desired.empty()) desired = "Material";
    pxr::SdfPath mat_path = unique_child_path(parent_path, desired, siblings);
    pxr::UsdShadeMaterial usd_mat = pxr::UsdShadeMaterial::Define(stage, mat_path);

    // UsdPreviewSurface shader subprim
    pxr::SdfPath shader_path = mat_path.AppendChild(pxr::TfToken("PreviewSurface"));
    pxr::UsdShadeShader shader = pxr::UsdShadeShader::Define(stage, shader_path);
    shader.CreateIdAttr().Set(pxr::TfToken("UsdPreviewSurface"));

    float rgba[4]; idtx_material_get_base_color(mat, rgba);
    shader.CreateInput(pxr::TfToken("diffuseColor"), pxr::SdfValueTypeNames->Color3f)
          .Set(pxr::GfVec3f(rgba[0], rgba[1], rgba[2]));
    shader.CreateInput(pxr::TfToken("opacity"), pxr::SdfValueTypeNames->Float)
          .Set(rgba[3]);
    shader.CreateInput(pxr::TfToken("metallic"), pxr::SdfValueTypeNames->Float)
          .Set(idtx_material_get_metallic(mat));
    shader.CreateInput(pxr::TfToken("roughness"), pxr::SdfValueTypeNames->Float)
          .Set(idtx_material_get_roughness(mat));

    // Base-color texture: author the standard UsdPreviewSurface texture subtree
    // (st reader -> UsdUVTexture -> diffuseColor) so the diffuse map round-trips.
    // The Godot importer follows exactly this diffuseColor connection.
    const char* base_tex = idtx_material_get_base_color_texture(mat);
    if (base_tex != nullptr && base_tex[0] != '\0') {
        pxr::UsdShadeShader st_reader = pxr::UsdShadeShader::Define(
            stage, mat_path.AppendChild(pxr::TfToken("stReader")));
        st_reader.CreateIdAttr().Set(pxr::TfToken("UsdPrimvarReader_float2"));
        st_reader.CreateInput(pxr::TfToken("varname"), pxr::SdfValueTypeNames->Token)
                 .Set(pxr::TfToken("st"));
        pxr::UsdShadeOutput st_out = st_reader.CreateOutput(
            pxr::TfToken("result"), pxr::SdfValueTypeNames->Float2);

        pxr::UsdShadeShader tex = pxr::UsdShadeShader::Define(
            stage, mat_path.AppendChild(pxr::TfToken("diffuseTexture")));
        tex.CreateIdAttr().Set(pxr::TfToken("UsdUVTexture"));
        tex.CreateInput(pxr::TfToken("file"), pxr::SdfValueTypeNames->Asset)
           .Set(pxr::SdfAssetPath(base_tex));
        tex.CreateInput(pxr::TfToken("st"), pxr::SdfValueTypeNames->Float2)
           .ConnectToSource(st_out);
        tex.CreateInput(pxr::TfToken("wrapS"), pxr::SdfValueTypeNames->Token).Set(pxr::TfToken("repeat"));
        tex.CreateInput(pxr::TfToken("wrapT"), pxr::SdfValueTypeNames->Token).Set(pxr::TfToken("repeat"));
        pxr::UsdShadeOutput tex_rgb = tex.CreateOutput(
            pxr::TfToken("rgb"), pxr::SdfValueTypeNames->Float3);
        shader.GetInput(pxr::TfToken("diffuseColor")).ConnectToSource(tex_rgb);
    }

    // Connect shader surface output to material
    pxr::UsdShadeOutput surf_out = shader.CreateOutput(
        pxr::TfToken("surface"), pxr::SdfValueTypeNames->Token);
    usd_mat.CreateSurfaceOutput().ConnectToSource(surf_out);

    // MToon overlay — apply VSekaiMToonAPI applied schema + write the
    // v_sekai:mtoon:* attributes for round-tripping.
    if (idtx_material_is_mtoon(mat)) {
        pxr::UsdPrim prim = usd_mat.GetPrim();
        prim.ApplyAPI(pxr::TfToken("VSekaiMToonAPI"));

        float shade[3]; idtx_material_get_mtoon_shade_color(mat, shade);
        float rim[3];   idtx_material_get_mtoon_rim_color(mat, rim);
        prim.CreateAttribute(
            pxr::TfToken("v_sekai:mtoon:shadeColor"),
            pxr::SdfValueTypeNames->Color3f).Set(pxr::GfVec3f(shade[0], shade[1], shade[2]));
        prim.CreateAttribute(
            pxr::TfToken("v_sekai:mtoon:rimColor"),
            pxr::SdfValueTypeNames->Color3f).Set(pxr::GfVec3f(rim[0], rim[1], rim[2]));
        prim.CreateAttribute(
            pxr::TfToken("v_sekai:mtoon:outlineWidth"),
            pxr::SdfValueTypeNames->Float).Set(idtx_material_get_mtoon_outline_width(mat));
    }

    return mat_path;
}

// Bind a UsdGeomMesh to the avatar's UsdSkelSkeleton at `skel_path`,
// applying UsdSkelBindingAPI and writing jointIndices / jointWeights
// primvars from the idtx_mesh skinning data. No-op if the mesh has no
// skinning information or the skeleton path is empty.
static void bind_mesh_to_skeleton(
    pxr::UsdStageRefPtr const& stage,
    pxr::SdfPath const& mesh_path,
    pxr::SdfPath const& skel_path,
    idtx_mesh_t const* mesh)
{
    if (mesh == nullptr || mesh_path.IsEmpty() || skel_path.IsEmpty()) return;
    int32_t bpv = idtx_mesh_get_bones_per_vertex(mesh);
    int32_t vc  = idtx_mesh_get_vertex_count(mesh);
    if (bpv <= 0 || vc <= 0) return;

    pxr::UsdPrim mesh_prim = stage->GetPrimAtPath(mesh_path);
    if (!mesh_prim.IsValid()) return;

    pxr::UsdSkelBindingAPI binding = pxr::UsdSkelBindingAPI::Apply(mesh_prim);
    binding.CreateSkeletonRel().AddTarget(skel_path);

    std::vector<int32_t> bi(static_cast<size_t>(vc) * static_cast<size_t>(bpv));
    std::vector<float>   bw(static_cast<size_t>(vc) * static_cast<size_t>(bpv));
    idtx_mesh_get_bone_indices(mesh, bi.data());
    idtx_mesh_get_weights(mesh, bw.data());

    pxr::VtArray<int>   indices_vt;
    pxr::VtArray<float> weights_vt;
    indices_vt.reserve(bi.size());
    weights_vt.reserve(bw.size());
    for (auto v : bi) indices_vt.push_back(static_cast<int>(v));
    for (auto v : bw) weights_vt.push_back(v);

    auto ji = binding.CreateJointIndicesPrimvar(/*constant=*/false, bpv);
    ji.Set(indices_vt);
    auto jw = binding.CreateJointWeightsPrimvar(/*constant=*/false, bpv);
    jw.Set(weights_vt);

    // geomBindTransform — UsdSkel's default is identity, so skip
    // emitting it. (Earlier we always set the identity matrix
    // explicitly, but that bloated round-trip diffs against
    // identity-bind fixtures.) Future API extension can take a
    // non-identity bind matrix on the idtx_mesh handle if needed.
}

// Emit a single VSekaiSpringBoneAPI prim from idtx_spring_chain.
static void emit_spring_chain(
    pxr::UsdStageRefPtr const& stage,
    pxr::SdfPath const& parent_path,
    idtx_spring_chain_t const* chain,
    std::set<std::string>& siblings)
{
    if (chain == nullptr) return;
    std::string desired = sanitise_prim_name(idtx_spring_chain_get_name(chain));
    if (desired.empty()) desired = "Chain";
    pxr::SdfPath p = unique_child_path(parent_path, desired, siblings);
    auto xf = pxr::UsdGeomXform::Define(stage, p);
    pxr::UsdPrim prim = xf.GetPrim();
    prim.ApplyAPI(pxr::TfToken("VSekaiSpringBoneAPI"));

    prim.CreateAttribute(
        pxr::TfToken("v_sekai:springBone:stiffness"),
        pxr::SdfValueTypeNames->Float).Set(idtx_spring_chain_get_stiffness(chain));
    prim.CreateAttribute(
        pxr::TfToken("v_sekai:springBone:drag"),
        pxr::SdfValueTypeNames->Float).Set(idtx_spring_chain_get_drag(chain));
    prim.CreateAttribute(
        pxr::TfToken("v_sekai:springBone:gravityPower"),
        pxr::SdfValueTypeNames->Float).Set(idtx_spring_chain_get_gravity_power(chain));
    prim.CreateAttribute(
        pxr::TfToken("v_sekai:springBone:hitRadius"),
        pxr::SdfValueTypeNames->Float).Set(idtx_spring_chain_get_hit_radius(chain));

    float gdir[3]; idtx_spring_chain_get_gravity_dir(chain, gdir);
    prim.CreateAttribute(
        pxr::TfToken("v_sekai:springBone:gravityDir"),
        pxr::SdfValueTypeNames->Float3).Set(pxr::GfVec3f(gdir[0], gdir[1], gdir[2]));

    // Joint references stored as a flat int[] of bone indices.
    int32_t jc = idtx_spring_chain_get_joint_count(chain);
    pxr::VtArray<int> joints; joints.reserve(jc);
    for (int32_t j = 0; j < jc; ++j) joints.push_back(idtx_spring_chain_get_joint(chain, j));
    prim.CreateAttribute(
        pxr::TfToken("v_sekai:springBone:joints"),
        pxr::SdfValueTypeNames->IntArray).Set(joints);

    // Collider references — flat int[] of collider indices.
    int32_t cc = idtx_spring_chain_get_collider_count(chain);
    if (cc > 0) {
        pxr::VtArray<int> cols; cols.reserve(cc);
        for (int32_t c = 0; c < cc; ++c) cols.push_back(idtx_spring_chain_get_collider(chain, c));
        // Schema reserves `v_sekai:springBone:colliders` for the
        // relationship form (rel → collider prim paths). For the
        // wire-format int[] indices, author a sibling custom attr
        // `v_sekai:springBone:colliderIndices` so USD doesn't reject
        // the conflicting type at the same name.
        pxr::UsdAttribute attr = prim.CreateAttribute(
            pxr::TfToken("v_sekai:springBone:colliderIndices"),
            pxr::SdfValueTypeNames->IntArray,
            /*custom=*/true);
        attr.Set(cols);
    }
}

// Emit a single VSekaiSpringBoneColliderAPI prim from idtx_spring_collider.
static void emit_spring_collider(
    pxr::UsdStageRefPtr const& stage,
    pxr::SdfPath const& parent_path,
    idtx_spring_collider_t const* col,
    std::set<std::string>& siblings)
{
    if (col == nullptr) return;
    std::string desired = sanitise_prim_name(idtx_spring_collider_get_name(col));
    if (desired.empty()) desired = "Collider";
    pxr::SdfPath p = unique_child_path(parent_path, desired, siblings);
    auto xf = pxr::UsdGeomXform::Define(stage, p);
    pxr::UsdPrim prim = xf.GetPrim();
    prim.ApplyAPI(pxr::TfToken("VSekaiSpringBoneColliderAPI"));

    idtx_collider_shape_t shape = idtx_spring_collider_get_shape(col);
    prim.CreateAttribute(
        pxr::TfToken("v_sekai:springBone:collider:shape"),
        pxr::SdfValueTypeNames->Token).Set(
            shape == IDTX_COLLIDER_CAPSULE ? pxr::TfToken("capsule") : pxr::TfToken("sphere"));

    prim.CreateAttribute(
        pxr::TfToken("v_sekai:springBone:collider:attachedBone"),
        pxr::SdfValueTypeNames->Int).Set(idtx_spring_collider_get_attached_bone(col));

    float off[3]; idtx_spring_collider_get_offset(col, off);
    prim.CreateAttribute(
        pxr::TfToken("v_sekai:springBone:collider:offset"),
        pxr::SdfValueTypeNames->Float3).Set(pxr::GfVec3f(off[0], off[1], off[2]));

    prim.CreateAttribute(
        pxr::TfToken("v_sekai:springBone:collider:radius"),
        pxr::SdfValueTypeNames->Float).Set(idtx_spring_collider_get_radius(col));

    if (shape == IDTX_COLLIDER_CAPSULE) {
        float tail[3]; idtx_spring_collider_get_tail(col, tail);
        prim.CreateAttribute(
            pxr::TfToken("v_sekai:springBone:collider:tail"),
            pxr::SdfValueTypeNames->Float3).Set(pxr::GfVec3f(tail[0], tail[1], tail[2]));
    }
}

// Emit one physics collider prim. Picks the right UsdGeom primitive
// for the shape (Cube / Sphere / Capsule / Cylinder), applies
// UsdPhysicsCollisionAPI, and writes V-Sekai extension attributes
// for tapered variants (no standard USD physics tapered shape).
static void emit_physics_collider(
    pxr::UsdStageRefPtr const& stage,
    pxr::SdfPath const& parent_path,
    idtx_physics_collider_t const* col,
    std::set<std::string>& siblings)
{
    if (col == nullptr) return;
    std::string desired = sanitise_prim_name(idtx_physics_collider_get_name(col));
    if (desired.empty()) desired = "Collider";
    pxr::SdfPath p = unique_child_path(parent_path, desired, siblings);

    float dims[3]; idtx_physics_collider_get_dimensions(col, dims);
    idtx_physics_shape_t shape = idtx_physics_collider_get_shape(col);

    pxr::UsdPrim prim;
    switch (shape) {
        case IDTX_PHYSICS_BOX: {
            auto cube = pxr::UsdGeomCube::Define(stage, p);
            cube.CreateSizeAttr().Set(2.0);  // UsdGeomCube is unit cube; scale via xform
            // Half-extents go on the xform scale.
            float m[16] = {
                dims[0], 0, 0, 0,
                0, dims[1], 0, 0,
                0, 0, dims[2], 0,
                0, 0, 0,       1,
            };
            float prev[16]; idtx_physics_collider_get_transform(col, prev);
            // Multiply prev * scale (row-major).
            float final_m[16];
            for (int r = 0; r < 4; ++r) {
                for (int c = 0; c < 4; ++c) {
                    float s = 0.0f;
                    for (int k = 0; k < 4; ++k) s += prev[r * 4 + k] * m[k * 4 + c];
                    final_m[r * 4 + c] = s;
                }
            }
            auto op = cube.AddTransformOp(pxr::UsdGeomXformOp::PrecisionDouble);
            op.Set(float16_to_gf_matrix(final_m));
            prim = cube.GetPrim();
            break;
        }
        case IDTX_PHYSICS_SPHERE: {
            auto sph = pxr::UsdGeomSphere::Define(stage, p);
            sph.CreateRadiusAttr().Set(static_cast<double>(dims[0]));
            auto op = sph.AddTransformOp(pxr::UsdGeomXformOp::PrecisionDouble);
            float xf[16]; idtx_physics_collider_get_transform(col, xf);
            op.Set(float16_to_gf_matrix(xf));
            prim = sph.GetPrim();
            break;
        }
        case IDTX_PHYSICS_CAPSULE: {
            auto cap = pxr::UsdGeomCapsule::Define(stage, p);
            cap.CreateRadiusAttr().Set(static_cast<double>(dims[0]));
            cap.CreateHeightAttr().Set(static_cast<double>(dims[1]));
            cap.CreateAxisAttr().Set(pxr::TfToken("Y"));
            auto op = cap.AddTransformOp(pxr::UsdGeomXformOp::PrecisionDouble);
            float xf[16]; idtx_physics_collider_get_transform(col, xf);
            op.Set(float16_to_gf_matrix(xf));
            prim = cap.GetPrim();
            break;
        }
        case IDTX_PHYSICS_CYLINDER: {
            auto cyl = pxr::UsdGeomCylinder::Define(stage, p);
            cyl.CreateRadiusAttr().Set(static_cast<double>(dims[0]));
            cyl.CreateHeightAttr().Set(static_cast<double>(dims[1]));
            cyl.CreateAxisAttr().Set(pxr::TfToken("Y"));
            auto op = cyl.AddTransformOp(pxr::UsdGeomXformOp::PrecisionDouble);
            float xf[16]; idtx_physics_collider_get_transform(col, xf);
            op.Set(float16_to_gf_matrix(xf));
            prim = cyl.GetPrim();
            break;
        }
        case IDTX_PHYSICS_TAPERED_CAPSULE:
        case IDTX_PHYSICS_TAPERED_CYLINDER: {
            // No standard USD physics primitive for tapered shapes.
            // Emit a Capsule as the "downgraded" representation (using
            // max of top/bottom radius as the collision-conservative
            // approximation) and attach V-Sekai extension attributes
            // that carry the full shape for round-trip into Godot/Jolt.
            auto cap = pxr::UsdGeomCapsule::Define(stage, p);
            float r = (dims[0] > dims[1]) ? dims[0] : dims[1];
            cap.CreateRadiusAttr().Set(static_cast<double>(r));
            cap.CreateHeightAttr().Set(static_cast<double>(dims[2]));
            cap.CreateAxisAttr().Set(pxr::TfToken("Y"));
            auto op = cap.AddTransformOp(pxr::UsdGeomXformOp::PrecisionDouble);
            float xf[16]; idtx_physics_collider_get_transform(col, xf);
            op.Set(float16_to_gf_matrix(xf));
            prim = cap.GetPrim();
            prim.CreateAttribute(
                pxr::TfToken("v_sekai:physics:tapered"),
                pxr::SdfValueTypeNames->Bool).Set(true);
            prim.CreateAttribute(
                pxr::TfToken("v_sekai:physics:topRadius"),
                pxr::SdfValueTypeNames->Float).Set(dims[0]);
            prim.CreateAttribute(
                pxr::TfToken("v_sekai:physics:bottomRadius"),
                pxr::SdfValueTypeNames->Float).Set(dims[1]);
            prim.CreateAttribute(
                pxr::TfToken("v_sekai:physics:midHeight"),
                pxr::SdfValueTypeNames->Float).Set(dims[2]);
            if (shape == IDTX_PHYSICS_TAPERED_CYLINDER) {
                prim.CreateAttribute(
                    pxr::TfToken("v_sekai:physics:taperedShape"),
                    pxr::SdfValueTypeNames->Token).Set(pxr::TfToken("cylinder"));
            }
            break;
        }
    }

    if (prim.IsValid()) {
        prim.ApplyAPI(pxr::TfToken("PhysicsCollisionAPI"));
        int32_t bone = idtx_physics_collider_get_attached_bone(col);
        if (bone >= 0) {
            prim.CreateAttribute(
                pxr::TfToken("v_sekai:physics:attachedBone"),
                pxr::SdfValueTypeNames->Int).Set(bone);
        }
    }
}

}  // namespace idtx::core::detail

namespace idtx::core::detail {

// Author the avatar's full prim tree into `stage` under the stage's
// current edit target: stage metadata (upAxis / metersPerUnit), root
// Xform (becomes the default prim), VRM-upgrade provenance stamp,
// skeleton, materials, meshes (with material + skeleton binding),
// physics colliders, and spring bones.
//
// Shared by every export mode. The caller owns stage creation and edit-
// target selection, so the same authoring runs whether we're writing a
// fresh flat stage, an overlay sublayer, or the session layer ahead of a
// flatten. Emitting `def`s here is intentional: when this layer is the
// strongest in a composition the avatar's opinions win, and the source's
// references / payloads / untouched prims compose in underneath.
static void author_avatar_tree(
    pxr::UsdStageRefPtr const& stage,
    idtx_avatar_t const* avatar)
{
    stage->SetMetadata(pxr::TfToken("upAxis"),        pxr::VtValue(pxr::TfToken("Y")));
    stage->SetMetadata(pxr::TfToken("metersPerUnit"), pxr::VtValue(1.0));

    pxr::SdfPath root_path = emit_root_xform(
        stage, pxr::SdfPath::AbsoluteRootPath(), avatar);
    stage->SetDefaultPrim(stage->GetPrimAtPath(root_path));

    // Provenance stamp: when an avatar was upgraded from VRM 0.x on
    // the way in, record it in customData so a later round-trip can
    // preserve the upgrade history. Per CHI-252 acceptance criterion.
    const char* src_ver = idtx_avatar_get_source_vrm_version(avatar);
    if (src_ver != nullptr && src_ver[0] != '\0') {
        pxr::UsdPrim root_prim = stage->GetPrimAtPath(root_path);
        pxr::VtDictionary cd = root_prim.GetCustomData();
        cd[pxr::TfToken("vSekai:upgrade:fromVrm")] = pxr::VtValue(std::string(src_ver));
        root_prim.SetCustomData(cd);
    }

    pxr::SdfPath skel_root_path = emit_skeleton(
        stage, root_path, idtx_avatar_get_skeleton(avatar));
    pxr::SdfPath skel_path = skel_root_path.IsEmpty()
        ? pxr::SdfPath()
        : skel_root_path.AppendChild(pxr::TfToken("Skeleton"));

    // Materials live in a dedicated Materials scope so they can be
    // referenced by mesh binding by path AND can never collide with a
    // mesh/skeleton prim of the same name under the root. (An unnamed
    // mesh and unnamed material both sanitise to "Unnamed"; emitting both
    // as direct root children redefined the Material prim as a Mesh, so
    // material:binding then targeted a non-material — usdchecker flags
    // this as MaterialBindingCollectionValidator.InvalidResourcePath.)
    std::vector<pxr::SdfPath> material_paths;
    {
        int32_t mat_count = idtx_avatar_get_material_count(avatar);
        material_paths.reserve(static_cast<size_t>(mat_count));
        if (mat_count > 0) {
            pxr::SdfPath materials_root = root_path.AppendChild(pxr::TfToken("Materials"));
            pxr::UsdGeomScope::Define(stage, materials_root);
            std::set<std::string> mat_siblings;
            for (int32_t i = 0; i < mat_count; ++i) {
                material_paths.push_back(
                    emit_material(
                        stage, materials_root, idtx_avatar_get_material(avatar, i), mat_siblings));
            }
        }
    }

    {
        std::set<std::string> mesh_siblings;
        int32_t mesh_count = idtx_avatar_get_mesh_count(avatar);
        for (int32_t i = 0; i < mesh_count; ++i) {
            pxr::SdfPath mp = emit_mesh(
                stage, root_path, idtx_avatar_get_mesh(avatar, i), mesh_siblings);
            if (mp.IsEmpty()) continue;
            int32_t mat_index = idtx_avatar_get_mesh_material(avatar, i);
            if (mat_index >= 0 && mat_index < static_cast<int32_t>(material_paths.size())
                && !material_paths[mat_index].IsEmpty()) {
                pxr::UsdPrim mesh_prim = stage->GetPrimAtPath(mp);
                pxr::UsdShadeMaterial mat = pxr::UsdShadeMaterial::Get(
                    stage, material_paths[mat_index]);
                // Apply the API schema first — UsdShadeMaterialBindingAPI::Bind()
                // writes the material:binding rel but does NOT add the API to
                // apiSchemas. Without it usdchecker --arkit=false flags the
                // material:binding rel as "MissingMaterialBindingAPI".
                auto binding = pxr::UsdShadeMaterialBindingAPI::Apply(mesh_prim);
                binding.Bind(mat);
            }

            bind_mesh_to_skeleton(
                stage, mp, skel_path, idtx_avatar_get_mesh(avatar, i));
        }
    }

    // Physics colliders — grouped under a Scope below the avatar root
    // so they don't clutter the geometry hierarchy. Each one carries
    // UsdPhysicsCollisionAPI + V-Sekai extension attributes for
    // tapered variants.
    int32_t phys_count = idtx_avatar_get_physics_collider_count(avatar);
    if (phys_count > 0) {
        pxr::SdfPath phys_root = root_path.AppendChild(pxr::TfToken("PhysicsColliders"));
        pxr::UsdGeomScope::Define(stage, phys_root);
        std::set<std::string> phys_siblings;
        for (int32_t i = 0; i < phys_count; ++i) {
            emit_physics_collider(
                stage, phys_root, idtx_avatar_get_physics_collider(avatar, i), phys_siblings);
        }
    }

    // Spring bones — chains + colliders inside a single Scope so the
    // VRMC_springBone hierarchy stays grouped under the avatar root.
    int32_t chain_count    = idtx_avatar_get_spring_chain_count(avatar);
    int32_t collider_count = idtx_avatar_get_spring_collider_count(avatar);
    if (chain_count > 0 || collider_count > 0) {
        pxr::SdfPath spring_root = root_path.AppendChild(pxr::TfToken("SpringBones"));
        pxr::UsdGeomScope::Define(stage, spring_root);

        std::set<std::string> chain_siblings;
        for (int32_t i = 0; i < chain_count; ++i) {
            emit_spring_chain(
                stage, spring_root, idtx_avatar_get_spring_chain(avatar, i), chain_siblings);
        }
        std::set<std::string> col_siblings;
        for (int32_t i = 0; i < collider_count; ++i) {
            emit_spring_collider(
                stage, spring_root, idtx_avatar_get_spring_collider(avatar, i), col_siblings);
        }
    }
}

// True if the prim spec carries a composition arc we must preserve
// (a reference / payload) — LAYER_ONLY pulls the source in this way, so
// such a prim is never flipped to `over` or pruned.
static bool has_composition_arc(pxr::SdfPrimSpecHandle const& spec)
{
    return !spec->GetReferenceList().GetAddedOrExplicitItems().empty()
        || !spec->GetPayloadList().GetAddedOrExplicitItems().empty();
}

// Delta-minimise `delta` against `base` (the composed source): for every
// property, erase each value-bearing field (attribute default, attribute
// connection targets, relationship targets) whose value already matches
// base, and remove the property spec once it carries none of them. Then
// flip surviving prim specifiers from `def` to `over` where the prim also
// exists in base, and prune overs that ended up empty.
//
// Net effect: an avatar imported from `base` and re-exported unchanged
// authors NO redundant opinion — not the geometry, not the material
// binding — so the delta carries only what the caller actually changed.
static void minimize_overlay(
    pxr::SdfLayerRefPtr const& delta,
    pxr::SdfLayerRefPtr const& base)
{
    if (!delta || !base) return;

    // The fields that carry a property's "value". Attributes use Default
    // (+ ConnectionPaths for shader wiring); relationships use TargetPaths
    // (e.g. material:binding). Our exporter authors no time samples.
    static const pxr::TfToken value_fields[] = {
        pxr::SdfFieldKeys->Default,
        pxr::SdfFieldKeys->ConnectionPaths,
        pxr::SdfFieldKeys->TargetPaths,
    };

    std::vector<pxr::SdfPath> props;
    std::vector<pxr::SdfPath> prims;
    delta->Traverse(pxr::SdfPath::AbsoluteRootPath(),
        [&](pxr::SdfPath const& p) {
            if (p.IsPrimPropertyPath()) {
                props.push_back(p);
            } else if (p.IsPrimPath() && p != pxr::SdfPath::AbsoluteRootPath()) {
                prims.push_back(p);
            }
        });

    // 1. Subtract value-bearing fields equal to the composed source, then
    //    remove the property spec once it carries no value at all.
    for (pxr::SdfPath const& p : props) {
        for (pxr::TfToken const& f : value_fields) {
            pxr::VtValue dv, bv;
            if (delta->HasField(p, f, &dv) &&
                base->HasField(p, f, &bv) &&
                dv == bv) {
                delta->EraseField(p, f);
            }
        }
        bool valueless = true;
        for (pxr::TfToken const& f : value_fields) {
            if (delta->HasField(p, f)) { valueless = false; break; }
        }
        if (valueless) {
            if (pxr::SdfPropertySpecHandle prop = delta->GetPropertyAtPath(p)) {
                if (pxr::SdfPrimSpecHandle owner = delta->GetPrimAtPath(p.GetPrimPath())) {
                    owner->RemoveProperty(prop);
                }
            }
        }
    }

    // 2. Deepest-first so children are pruned before parents: flip
    //    def -> over for prims the source already defines (leaving prims
    //    that carry a reference/payload arc as authored), then drop overs
    //    that ended up carrying nothing.
    std::sort(prims.begin(), prims.end(),
        [](pxr::SdfPath const& a, pxr::SdfPath const& b) {
            return a.GetPathElementCount() > b.GetPathElementCount();
        });
    for (pxr::SdfPath const& p : prims) {
        pxr::SdfPrimSpecHandle spec = delta->GetPrimAtPath(p);
        if (!spec) continue;
        bool arc = has_composition_arc(spec);
        if (base->GetPrimAtPath(p) && !arc) {
            spec->SetSpecifier(pxr::SdfSpecifierOver);
        }
        bool inert = !arc
                  && spec->GetProperties().empty()
                  && spec->GetNameChildren().empty()
                  && spec->GetSpecifier() == pxr::SdfSpecifierOver;
        if (inert) {
            pxr::SdfPath parent = p.GetParentPath();
            if (parent == pxr::SdfPath::AbsoluteRootPath()) {
                delta->RemoveRootPrim(spec);
            } else if (pxr::SdfPrimSpecHandle pp = delta->GetPrimAtPath(parent)) {
                pp->RemoveNameChild(spec);
            }
        }
    }
}

}  // namespace idtx::core::detail

extern "C" IDTX_CORE_API void idtx_usd_export_opts_init(idtx_usd_export_opts_t* opts)
{
    if (opts == nullptr) return;
    opts->mode             = IDTX_USD_NEW_FLAT;
    opts->source_path      = nullptr;
    opts->edit_target_id   = nullptr;
    opts->reflect_per_prim = 0;
}

extern "C" IDTX_CORE_API int32_t idtx_core_export_avatar_to_usd_ex(
    const idtx_avatar_t* avatar,
    const char* path,
    const idtx_usd_export_opts_t* opts)
{
    if (avatar == nullptr || path == nullptr) return 1;

    idtx_usd_export_opts_t o;
    idtx_usd_export_opts_init(&o);
    if (opts != nullptr) o = *opts;
    if (o.mode < 0 || o.mode >= IDTX_USD_MODE_MAX) return 1;

    // Resolve the source stage: an explicit opts.source_path wins, else
    // the import-time provenance stamped on the avatar handle.
    std::string source;
    if (o.source_path != nullptr && o.source_path[0] != '\0') {
        source = o.source_path;
    } else {
        const char* prov = idtx_avatar_get_source_usd_path(avatar);
        if (prov != nullptr && prov[0] != '\0') source = prov;
    }

    // Without a source there is nothing to layer against — every mode
    // collapses to the flat single-layer write.
    idtx_usd_export_mode_t mode = source.empty() ? IDTX_USD_NEW_FLAT : o.mode;

    // ---- NEW_FLAT: fresh single-layer stage (the legacy behaviour). ----
    if (mode == IDTX_USD_NEW_FLAT) {
        pxr::UsdStageRefPtr stage = pxr::UsdStage::CreateNew(std::string(path));
        if (!stage) return 2;
        idtx::core::detail::author_avatar_tree(stage, avatar);
        if (!stage->GetRootLayer()->Save()) return 3;
        return 0;
    }

    // ---- FLATTEN: compose source + avatar deltas, write standalone. -----
    if (mode == IDTX_USD_FLATTEN) {
        pxr::UsdStageRefPtr src = pxr::UsdStage::Open(source);
        if (!src) return 4;
        // Author the avatar onto the session layer so the source's own
        // layers are never mutated; Flatten() then composes root +
        // session + sublayers (resolving references/payloads inline)
        // into a single standalone layer.
        src->SetEditTarget(pxr::UsdEditTarget(src->GetSessionLayer()));
        idtx::core::detail::author_avatar_tree(src, avatar);
        pxr::SdfLayerRefPtr flat = src->Flatten();
        if (!flat) return 3;
        if (!flat->Export(std::string(path))) return 3;
        return 0;
    }

    // ---- OVERLAY / LAYER_ONLY: a delta layer over the source. ----------
    // `path` is a new layer holding only the avatar's deltas; the source
    // is pulled in so its references, payloads, and untouched prims all
    // survive. The two modes differ ONLY in the composition arc used:
    //
    //   OVERLAY    — source attached as a weaker SUBLAYER of the delta.
    //                A patch on top of the source's layer stack.
    //   LAYER_ONLY — source pulled by a composition REFERENCE arc on the
    //                avatar root. The delta is a standalone stage that
    //                incorporates the source asset, not a layer patch.
    //
    // In both cases the full avatar is authored, then minimize_overlay()
    // subtracts every opinion the composed source already provides (values,
    // material bindings, shader connections) and flips def->over, so an
    // unchanged import->export round-trip yields an empty delta.
    pxr::SdfLayerRefPtr over = pxr::SdfLayer::CreateNew(std::string(path));
    if (!over) return 2;

    if (mode == IDTX_USD_OVERLAY) {
        over->GetSubLayerPaths().push_back(source);
    }

    pxr::UsdStageRefPtr stage = pxr::UsdStage::Open(over);
    if (!stage) return 2;

    // edit_target_id (OVERLAY only) routes opinions to a named layer in the
    // composed stack instead of the delta layer. Routing into an existing
    // source layer means we do NOT minimise (the source would be its own
    // base) and we save that layer directly.
    pxr::SdfLayerRefPtr authored = over;
    bool minimise = true;
    if (mode == IDTX_USD_OVERLAY &&
        o.edit_target_id != nullptr && o.edit_target_id[0] != '\0') {
        pxr::SdfLayerRefPtr target = pxr::SdfLayer::Find(o.edit_target_id);
        if (!target) return 5;
        stage->SetEditTarget(pxr::UsdEditTarget(target));
        authored = target;
        minimise = (target == over);
    } else {
        stage->SetEditTarget(pxr::UsdEditTarget(over));
    }

    idtx::core::detail::author_avatar_tree(stage, avatar);

    // LAYER_ONLY: pull the source in by a reference arc on the avatar root
    // (its default prim). The source's default prim composes underneath, so
    // the avatar's own def's become redundant and minimise away.
    if (mode == IDTX_USD_LAYER_ONLY) {
        if (pxr::UsdPrim root = stage->GetDefaultPrim()) {
            root.GetReferences().AddReference(source);
        }
    }

    if (minimise) {
        // Base = the source composed on its own (without the avatar's
        // edits), so equal opinions in `over` can be subtracted.
        if (pxr::UsdStageRefPtr base_stage = pxr::UsdStage::Open(source)) {
            idtx::core::detail::minimize_overlay(over, base_stage->Flatten());
        }
    }

    if (!authored->Save()) return 3;
    return 0;
}

// Flat export — the original destructive single-layer write, preserved
// as a thin forwarder so existing callers and the C ABI symbol are
// unchanged. NEW_FLAT mode ignores any source provenance on the avatar,
// so this is byte-for-byte the legacy behaviour.
extern "C" IDTX_CORE_API int32_t idtx_core_export_avatar_to_usd(
    const idtx_avatar_t* avatar,
    const char* path)
{
    idtx_usd_export_opts_t o;
    idtx_usd_export_opts_init(&o);   // mode = IDTX_USD_NEW_FLAT
    return idtx_core_export_avatar_to_usd_ex(avatar, path, &o);
}
