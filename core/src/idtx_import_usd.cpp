// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// idtx_core_import_avatar_from_usd — read a USD file and rebuild an
// idtx_avatar_t* from it. Reverse direction of idtx_export_usd.cpp;
// powers the future Unity import path.

#include "idtx_core/idtx_core.h"
#include "idtx_core/internal/usd_helpers.h"
#include "usd_internal.h"

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/capsule.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/cylinder.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdShade/connectableAPI.h>
#include <pxr/usd/usdSkel/bindingAPI.h>
#include <pxr/usd/usdSkel/skeleton.h>

#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace idtx::core::detail {

// Pick the avatar root prim: the stage's defaultPrim if set, otherwise
// the first top-level prim. Returns an invalid UsdPrim if neither exists.
static pxr::UsdPrim pick_avatar_root(pxr::UsdStageRefPtr const& stage)
{
    pxr::UsdPrim def = stage->GetDefaultPrim();
    if (def.IsValid()) return def;
    for (auto const& p : stage->GetPseudoRoot().GetChildren()) {
        return p;
    }
    return pxr::UsdPrim();
}

// Find the UsdSkelSkeleton bound by the meshes inside `root`.
// Per UsdSkel conventions, every skinned UsdGeomMesh carries a
// `skel:skeleton` relationship to the authoritative Skeleton. The
// VRM skeleton is whichever Skeleton the majority of meshes
// reference — using the binding (not just the first Skeleton spec
// found in the tree) is well-specified by the UsdSkel docs:
// https://openusd.org/dev/api/_usd_skel__schemas.html#UsdSkel_Skeleton
// Falls back to "first Skeleton under `root`" only when no mesh
// declares a binding (degenerate test fixtures).
static pxr::UsdPrim find_first_skeleton_prim(pxr::UsdPrim const& root)
{
    if (!root.IsValid()) return pxr::UsdPrim();
    std::unordered_map<std::string, int> binding_counts;
    for (auto const& p : pxr::UsdPrimRange(root)) {
        if (!p.IsA<pxr::UsdGeomMesh>()) continue;
        pxr::UsdRelationship rel = p.GetRelationship(pxr::TfToken("skel:skeleton"));
        if (!rel) continue;
        pxr::SdfPathVector targets;
        rel.GetTargets(&targets);
        for (auto const& t : targets) binding_counts[t.GetString()] += 1;
    }
    if (!binding_counts.empty()) {
        std::string winner;
        int winner_n = -1;
        for (auto const& kv : binding_counts) {
            if (kv.second > winner_n) { winner = kv.first; winner_n = kv.second; }
        }
        pxr::UsdPrim sk = root.GetStage()->GetPrimAtPath(pxr::SdfPath(winner));
        if (sk.IsValid() && sk.IsA<pxr::UsdSkelSkeleton>()) return sk;
    }
    // Fallback: first Skeleton in tree order (fixtures with no binding).
    for (auto const& p : pxr::UsdPrimRange(root)) {
        if (p.IsA<pxr::UsdSkelSkeleton>()) return p;
    }
    return pxr::UsdPrim();
}

// Build idtx_skeleton_t from a UsdSkelSkeleton prim. Returns NULL if
// the skeleton has no joints or required attrs are missing.
static idtx_skeleton_t* read_skeleton(pxr::UsdPrim const& skel_prim)
{
    if (!skel_prim.IsValid()) return nullptr;
    pxr::UsdSkelSkeleton usd_skel(skel_prim);

    pxr::VtArray<pxr::TfToken>    joints;
    pxr::VtArray<pxr::GfMatrix4d> rest_transforms;
    pxr::VtArray<pxr::GfMatrix4d> bind_transforms;

    auto joints_attr = usd_skel.GetJointsAttr();
    if (!joints_attr || !joints_attr.Get(&joints) || joints.empty()) return nullptr;
    usd_skel.GetRestTransformsAttr().Get(&rest_transforms);
    usd_skel.GetBindTransformsAttr().Get(&bind_transforms);

    idtx_skeleton_t* skel = idtx_skeleton_create();
    // Recover the idtx_skeleton's name from the parent SkelRoot
    // (idtx_export_usd emits "<skel_name>_SkelRoot" for the parent).
    // Falling back to the Skeleton prim's own name when the suffix
    // pattern doesn't match keeps imports of non-idtx USD files
    // (Blender / Houdini / Maya exports) working.
    std::string skel_name = skel_prim.GetName().GetString();
    pxr::UsdPrim skel_parent = skel_prim.GetParent();
    if (skel_parent && skel_parent.IsValid()) {
        std::string parent_name = skel_parent.GetName().GetString();
        const std::string suffix = "_SkelRoot";
        if (parent_name.size() > suffix.size()
            && parent_name.compare(parent_name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            skel_name = parent_name.substr(0, parent_name.size() - suffix.size());
        }
    }
    idtx_skeleton_set_name(skel, skel_name.c_str());

    // joint paths are "a/b/c"; parent of joint i is the joint whose
    // path is i's path minus the trailing /name. Build a map of
    // path -> index as we go.
    std::vector<std::string> joint_paths;
    joint_paths.reserve(joints.size());
    for (auto const& jt : joints) joint_paths.push_back(jt.GetString());

    auto find_parent = [&](std::string const& jp) -> int32_t {
        size_t slash = jp.find_last_of('/');
        if (slash == std::string::npos) return -1;
        std::string parent_path = jp.substr(0, slash);
        for (size_t k = 0; k < joint_paths.size(); ++k) {
            if (joint_paths[k] == parent_path) return static_cast<int32_t>(k);
        }
        return -1;
    };

    for (size_t i = 0; i < joints.size(); ++i) {
        std::string const& jp = joint_paths[i];
        size_t slash = jp.find_last_of('/');
        std::string bone_name = (slash == std::string::npos) ? jp : jp.substr(slash + 1);

        float rest[16];
        float bind[16];
        if (i < rest_transforms.size()) {
            idtx::core::gf_matrix_to_float16(rest_transforms[i], rest);
        } else {
            for (int k = 0; k < 16; ++k) rest[k] = (k % 5 == 0) ? 1.0f : 0.0f;
        }
        if (i < bind_transforms.size()) {
            idtx::core::gf_matrix_to_float16(bind_transforms[i], bind);
        } else {
            for (int k = 0; k < 16; ++k) bind[k] = (k % 5 == 0) ? 1.0f : 0.0f;
        }

        idtx_skeleton_add_bone(skel, bone_name.c_str(), find_parent(jp), rest, bind);
    }

    return skel;
}

// Read a UsdGeomMesh into an idtx_mesh_t. Returns NULL if positions
// or indices are missing/empty.
static idtx_mesh_t* read_mesh(pxr::UsdPrim const& prim)
{
    pxr::UsdGeomMesh m(prim);
    if (!m) return nullptr;

    pxr::VtArray<pxr::GfVec3f> points;
    pxr::VtArray<int> face_counts;
    pxr::VtArray<int> face_indices;
    m.GetPointsAttr().Get(&points);
    m.GetFaceVertexCountsAttr().Get(&face_counts);
    m.GetFaceVertexIndicesAttr().Get(&face_indices);
    if (points.empty() || face_indices.empty()) return nullptr;

    int32_t vc = static_cast<int32_t>(points.size());
    std::vector<float> positions(static_cast<size_t>(vc) * 3);
    for (int32_t i = 0; i < vc; ++i) {
        positions[i * 3 + 0] = points[i][0];
        positions[i * 3 + 1] = points[i][1];
        positions[i * 3 + 2] = points[i][2];
    }

    std::vector<float> normals;
    pxr::VtArray<pxr::GfVec3f> norm_arr;
    if (m.GetNormalsAttr() && m.GetNormalsAttr().Get(&norm_arr) && static_cast<int32_t>(norm_arr.size()) == vc) {
        normals.resize(static_cast<size_t>(vc) * 3);
        for (int32_t i = 0; i < vc; ++i) {
            normals[i * 3 + 0] = norm_arr[i][0];
            normals[i * 3 + 1] = norm_arr[i][1];
            normals[i * 3 + 2] = norm_arr[i][2];
        }
    }

    std::vector<float> uvs;
    pxr::UsdGeomPrimvarsAPI pvapi(prim);
    auto st = pvapi.GetPrimvar(pxr::TfToken("st"));
    pxr::VtArray<pxr::GfVec2f> uv_arr;
    if (st && st.Get(&uv_arr) && static_cast<int32_t>(uv_arr.size()) == vc) {
        uvs.resize(static_cast<size_t>(vc) * 2);
        for (int32_t i = 0; i < vc; ++i) {
            uvs[i * 2 + 0] = uv_arr[i][0];
            uvs[i * 2 + 1] = uv_arr[i][1];
        }
    }

    std::vector<float> colors;
    pxr::VtArray<pxr::GfVec3f> color_arr;
    if (m.GetDisplayColorAttr() && m.GetDisplayColorAttr().Get(&color_arr)
        && static_cast<int32_t>(color_arr.size()) == vc) {
        colors.resize(static_cast<size_t>(vc) * 4);
        for (int32_t i = 0; i < vc; ++i) {
            colors[i * 4 + 0] = color_arr[i][0];
            colors[i * 4 + 1] = color_arr[i][1];
            colors[i * 4 + 2] = color_arr[i][2];
            colors[i * 4 + 3] = 1.0f;  // displayColor has no alpha
        }
    }

    idtx_mesh_t* mesh = idtx_mesh_create();
    idtx_mesh_set_name(mesh, prim.GetName().GetString().c_str());

    idtx_mesh_set_vertices(
        mesh, vc,
        positions.data(),
        normals.empty() ? nullptr : normals.data(),
        uvs.empty()     ? nullptr : uvs.data(),
        colors.empty()  ? nullptr : colors.data());

    std::vector<int32_t> idx_buf(face_indices.size());
    for (size_t i = 0; i < face_indices.size(); ++i) idx_buf[i] = face_indices[i];
    idtx_mesh_set_indices(mesh, static_cast<int32_t>(face_indices.size()), idx_buf.data());

    // n-gon preservation: if the source USD carried non-triangulated
    // faceVertexCounts (i.e. not all 3's), keep them on the handle so
    // a Godot -> USD round-trip re-emits the original topology.
    bool any_non_triangle = false;
    for (auto c : face_counts) {
        if (c != 3) { any_non_triangle = true; break; }
    }
    if (any_non_triangle && !face_counts.empty()) {
        std::vector<int32_t> fvc(face_counts.size());
        for (size_t i = 0; i < face_counts.size(); ++i) fvc[i] = face_counts[i];
        idtx_mesh_set_face_vertex_counts(
            mesh, static_cast<int32_t>(fvc.size()), fvc.data());
    }

    // Skinning. UsdSkel allows a mesh to override the inherited
    // Skeleton's `joints` array with its own per-mesh subset
    // (`skel:joints` token[] on the mesh). When that override is
    // present, per-vertex `primvars:skel:jointIndices` values are
    // LOCAL to that subset — they index into `skel:joints` on the
    // mesh, not into the Skeleton's global joints array.
    //
    // glTF's `skin.joints[]` is the SINGLE global list. We pick that
    // list to be the Skeleton's full joints (idtx_skeleton bones in
    // order). To make the per-vertex JOINTS_N values point at the
    // right bones, we remap local subset indices to global indices
    // by name lookup: find each local joint path in the global
    // joints list and replace.
    //
    // Without this remap, meshes that USE the local-subset feature
    // get every vertex skinned to whatever bone happens to sit at
    // the same numerical index in the global array — the classic
    // "spikes radiating from origin" Blender→Godot import artefact.
    pxr::UsdSkelBindingAPI binding(prim);
    if (binding) {
        pxr::VtArray<int>   ji_arr;
        pxr::VtArray<float> jw_arr;
        auto ji_pv = binding.GetJointIndicesPrimvar();
        auto jw_pv = binding.GetJointWeightsPrimvar();
        if (ji_pv && ji_pv.Get(&ji_arr) && jw_pv && jw_pv.Get(&jw_arr)
            && !ji_arr.empty() && ji_arr.size() == jw_arr.size()
            && (ji_arr.size() % static_cast<size_t>(vc)) == 0) {

            // Build LOCAL -> GLOBAL remap if the mesh overrides
            // skel:joints. Empty override means "inherit the
            // Skeleton's joints"; in that case LOCAL == GLOBAL.
            std::vector<int32_t> local_to_global;
            pxr::UsdAttribute mesh_joints_attr = prim.GetAttribute(pxr::TfToken("skel:joints"));
            if (mesh_joints_attr) {
                pxr::VtArray<pxr::TfToken> mesh_joints;
                mesh_joints_attr.Get(&mesh_joints);
                if (!mesh_joints.empty()) {
                    // Find the bound Skeleton's global joints list.
                    pxr::UsdSkelSkeleton bound = binding.GetInheritedSkeleton();
                    pxr::VtArray<pxr::TfToken> global_joints;
                    if (bound) bound.GetJointsAttr().Get(&global_joints);
                    // Index global by name for O(1) lookup.
                    std::unordered_map<std::string, int32_t> global_idx;
                    global_idx.reserve(global_joints.size());
                    for (size_t i = 0; i < global_joints.size(); ++i) {
                        global_idx[global_joints[i].GetString()] = static_cast<int32_t>(i);
                    }
                    local_to_global.resize(mesh_joints.size(), -1);
                    for (size_t i = 0; i < mesh_joints.size(); ++i) {
                        auto it = global_idx.find(mesh_joints[i].GetString());
                        if (it != global_idx.end()) {
                            local_to_global[i] = it->second;
                        }
                    }
                }
            }

            int32_t bpv = static_cast<int32_t>(ji_arr.size() / static_cast<size_t>(vc));
            std::vector<int32_t> ibuf(ji_arr.size());
            std::vector<float>   wbuf(jw_arr.size());
            for (size_t i = 0; i < ji_arr.size(); ++i) {
                int32_t v = ji_arr[i];
                if (!local_to_global.empty()) {
                    // Remap LOCAL -> GLOBAL. Out-of-range local indices
                    // get -1 (will be clamped to 0 with zero weight
                    // downstream); unmapped joints (Skeleton didn't
                    // have that name) also -1.
                    if (v >= 0 && v < static_cast<int32_t>(local_to_global.size())) {
                        v = local_to_global[v];
                    } else {
                        v = -1;
                    }
                }
                ibuf[i] = v;
            }
            for (size_t i = 0; i < jw_arr.size(); ++i) wbuf[i] = jw_arr[i];

            // Zero out weights for any unresolved bones (-1) so the
            // GPU's `skin.joints[bone]` lookup doesn't read OOB.
            for (size_t i = 0; i < ibuf.size(); ++i) {
                if (ibuf[i] < 0) { ibuf[i] = 0; wbuf[i] = 0.0f; }
            }
            idtx_mesh_set_skinning(mesh, bpv, ibuf.data(), wbuf.data());
        }
    }

    return mesh;
}

// Locate the UsdPreviewSurface shader subprim of a UsdShadeMaterial.
// In export we always name it "PreviewSurface"; in import we accept
// any child shader whose id is UsdPreviewSurface, so we round-trip
// material files authored by other tools too.
static pxr::UsdShadeShader find_preview_surface(pxr::UsdShadeMaterial const& mat)
{
    for (auto const& child : mat.GetPrim().GetChildren()) {
        pxr::UsdShadeShader shader(child);
        if (!shader) continue;
        pxr::TfToken id;
        if (shader.GetIdAttr() && shader.GetIdAttr().Get(&id)
            && id == pxr::TfToken("UsdPreviewSurface")) {
            return shader;
        }
    }
    return pxr::UsdShadeShader();
}

// If `inp` is driven by a UsdUVTexture, return that texture's `file` asset path
// (resolved when the stage resolved it, else the authored path); empty otherwise.
// The host reads the actual bytes from the scene texture table — see idtx_scene.
static std::string connected_texture_path(pxr::UsdShadeInput const& inp)
{
    pxr::UsdShadeConnectableAPI src;
    pxr::TfToken srcName;
    pxr::UsdShadeAttributeType srcType;
    if (!inp || !inp.GetConnectedSource(&src, &srcName, &srcType)) return {};
    pxr::UsdShadeShader tex(src.GetPrim());
    if (!tex) return {};
    auto fileInp = tex.GetInput(pxr::TfToken("file"));
    if (!fileInp) return {};
    pxr::SdfAssetPath asset;
    if (!fileInp.Get(&asset)) return {};
    const std::string resolved = asset.GetResolvedPath();
    return resolved.empty() ? asset.GetAssetPath() : resolved;
}

// Read a UsdShadeMaterial -> idtx_material_t. Always succeeds (returns
// at minimum a defaulted handle) so the path-to-index map stays in
// sync with the avatar's material list.
idtx_material_t* read_material(pxr::UsdPrim const& prim)
{
    pxr::UsdShadeMaterial mat(prim);
    idtx_material_t* out = idtx_material_create();
    idtx_material_set_name(out, prim.GetName().GetString().c_str());

    if (auto shader = find_preview_surface(mat)) {
        pxr::GfVec3f diffuse(1.0f, 1.0f, 1.0f);
        float opacity = 1.0f;
        float metallic = 0.0f;
        float roughness = 0.5f;
        if (auto inp = shader.GetInput(pxr::TfToken("diffuseColor"))) {
            // A texture-driven base color leaves diffuse at its fallback (often
            // white); capture the texture path so the host can load the image.
            if (std::string tex = connected_texture_path(inp); !tex.empty())
                idtx_material_set_base_color_texture(out, tex.c_str());
            else
                inp.Get(&diffuse);
        }
        if (auto inp = shader.GetInput(pxr::TfToken("opacity")))
            inp.Get(&opacity);
        if (auto inp = shader.GetInput(pxr::TfToken("metallic")))
            inp.Get(&metallic);
        if (auto inp = shader.GetInput(pxr::TfToken("roughness")))
            inp.Get(&roughness);
        idtx_material_set_base_color(out, diffuse[0], diffuse[1], diffuse[2], opacity);
        idtx_material_set_metallic(out, metallic);
        idtx_material_set_roughness(out, roughness);
        if (auto inp = shader.GetInput(pxr::TfToken("normal"))) {
            if (std::string tex = connected_texture_path(inp); !tex.empty())
                idtx_material_set_normal_texture(out, tex.c_str());
        }
    }

    // MToon overlay if VSekaiMToonAPI is applied on the material prim.
    if (prim.HasAPI(pxr::TfToken("VSekaiMToonAPI"))) {
        pxr::GfVec3f shade(0.5f, 0.5f, 0.5f);
        pxr::GfVec3f rim(0.0f, 0.0f, 0.0f);
        float outline = 0.0f;
        if (auto a = prim.GetAttribute(pxr::TfToken("v_sekai:mtoon:shadeColor")))
            a.Get(&shade);
        if (auto a = prim.GetAttribute(pxr::TfToken("v_sekai:mtoon:rimColor")))
            a.Get(&rim);
        if (auto a = prim.GetAttribute(pxr::TfToken("v_sekai:mtoon:outlineWidth")))
            a.Get(&outline);
        idtx_material_set_mtoon_shade_color(out, shade[0], shade[1], shade[2]);
        idtx_material_set_mtoon_rim_color(out, rim[0], rim[1], rim[2]);
        idtx_material_set_mtoon_outline_width(out, outline);
    }
    return out;
}

// Read a spring chain from a prim carrying VSekaiSpringBoneAPI.
static idtx_spring_chain_t* read_spring_chain(pxr::UsdPrim const& prim)
{
    if (!prim.IsValid() || !prim.HasAPI(pxr::TfToken("VSekaiSpringBoneAPI"))) return nullptr;
    auto* chain = idtx_spring_chain_create();
    idtx_spring_chain_set_name(chain, prim.GetName().GetString().c_str());

    float stiffness = 1.0f, drag = 0.4f, grav_p = 0.0f, hit_r = 0.02f;
    if (auto a = prim.GetAttribute(pxr::TfToken("v_sekai:springBone:stiffness")))    a.Get(&stiffness);
    if (auto a = prim.GetAttribute(pxr::TfToken("v_sekai:springBone:drag")))         a.Get(&drag);
    if (auto a = prim.GetAttribute(pxr::TfToken("v_sekai:springBone:gravityPower"))) a.Get(&grav_p);
    if (auto a = prim.GetAttribute(pxr::TfToken("v_sekai:springBone:hitRadius")))    a.Get(&hit_r);
    idtx_spring_chain_set_dynamics(chain, stiffness, drag, grav_p, hit_r);

    pxr::GfVec3f gdir(0, -1, 0);
    if (auto a = prim.GetAttribute(pxr::TfToken("v_sekai:springBone:gravityDir"))) a.Get(&gdir);
    idtx_spring_chain_set_gravity_dir(chain, gdir[0], gdir[1], gdir[2]);

    pxr::VtArray<int> joints;
    if (auto a = prim.GetAttribute(pxr::TfToken("v_sekai:springBone:joints"))) a.Get(&joints);
    if (!joints.empty()) {
        std::vector<int32_t> j(joints.size());
        for (size_t i = 0; i < joints.size(); ++i) j[i] = joints[i];
        idtx_spring_chain_set_joints(chain, static_cast<int32_t>(j.size()), j.data());
    }

    // Schema reserves `v_sekai:springBone:colliders` as a rel (the
    // engine-side resolution form). The wire format the post-export
    // hook + the C++ exporter share is the sibling attribute
    // `v_sekai:springBone:colliderIndices` (int[]), which is what the
    // C ABI's flat collider-index table consumes.
    pxr::VtArray<int> cols;
    if (auto a = prim.GetAttribute(pxr::TfToken("v_sekai:springBone:colliderIndices"))) a.Get(&cols);
    if (cols.empty()) {
        if (auto a = prim.GetAttribute(pxr::TfToken("v_sekai:springBone:colliders"))) a.Get(&cols);
    }
    for (auto const& c : cols) idtx_spring_chain_add_collider(chain, c);

    return chain;
}

// Read a spring collider from a prim carrying VSekaiSpringBoneColliderAPI.
static idtx_spring_collider_t* read_spring_collider(pxr::UsdPrim const& prim)
{
    if (!prim.IsValid() || !prim.HasAPI(pxr::TfToken("VSekaiSpringBoneColliderAPI"))) return nullptr;
    auto* col = idtx_spring_collider_create();
    idtx_spring_collider_set_name(col, prim.GetName().GetString().c_str());

    pxr::TfToken shape;
    if (auto a = prim.GetAttribute(pxr::TfToken("v_sekai:springBone:collider:shape"))) a.Get(&shape);
    idtx_spring_collider_set_shape(col,
        shape == pxr::TfToken("capsule") ? IDTX_COLLIDER_CAPSULE : IDTX_COLLIDER_SPHERE);

    int attached = -1;
    if (auto a = prim.GetAttribute(pxr::TfToken("v_sekai:springBone:collider:attachedBone")))
        a.Get(&attached);
    idtx_spring_collider_set_attached_bone(col, attached);

    pxr::GfVec3f off(0, 0, 0);
    if (auto a = prim.GetAttribute(pxr::TfToken("v_sekai:springBone:collider:offset"))) a.Get(&off);
    idtx_spring_collider_set_offset(col, off[0], off[1], off[2]);

    float radius = 0.05f;
    if (auto a = prim.GetAttribute(pxr::TfToken("v_sekai:springBone:collider:radius"))) a.Get(&radius);
    idtx_spring_collider_set_radius(col, radius);

    pxr::GfVec3f tail(0, 0, 0);
    if (auto a = prim.GetAttribute(pxr::TfToken("v_sekai:springBone:collider:tail"))) a.Get(&tail);
    idtx_spring_collider_set_tail(col, tail[0], tail[1], tail[2]);

    return col;
}

// Read a physics collider from a prim carrying PhysicsCollisionAPI.
// Maps the prim type back to idtx_physics_shape, picks up V-Sekai
// tapered extension attributes when present. Returns NULL if the
// prim's shape isn't one we handle.
static idtx_physics_collider_t* read_physics_collider(pxr::UsdPrim const& prim)
{
    if (!prim.IsValid() || !prim.HasAPI(pxr::TfToken("PhysicsCollisionAPI"))) return nullptr;
    auto* out = idtx_physics_collider_create();
    idtx_physics_collider_set_name(out, prim.GetName().GetString().c_str());

    // Local transform.
    pxr::UsdGeomXformable xf(prim);
    if (xf) {
        pxr::GfMatrix4d m(1.0); bool resets = false;
        xf.GetLocalTransformation(&m, &resets);
        float t[16]; idtx::core::gf_matrix_to_float16(m, t);
        idtx_physics_collider_set_transform(out, t);
    }

    if (auto a = prim.GetAttribute(pxr::TfToken("v_sekai:physics:attachedBone"))) {
        int b = -1; a.Get(&b);
        idtx_physics_collider_set_attached_bone(out, b);
    }

    // Tapered? — V-Sekai-extension flag overrides the geometric prim type.
    bool tapered = false;
    if (auto a = prim.GetAttribute(pxr::TfToken("v_sekai:physics:tapered"))) a.Get(&tapered);
    if (tapered) {
        float top = 0.5f, bot = 0.5f, mid = 1.0f;
        if (auto a = prim.GetAttribute(pxr::TfToken("v_sekai:physics:topRadius")))    a.Get(&top);
        if (auto a = prim.GetAttribute(pxr::TfToken("v_sekai:physics:bottomRadius"))) a.Get(&bot);
        if (auto a = prim.GetAttribute(pxr::TfToken("v_sekai:physics:midHeight")))    a.Get(&mid);
        pxr::TfToken sub;
        if (auto a = prim.GetAttribute(pxr::TfToken("v_sekai:physics:taperedShape"))) a.Get(&sub);
        if (sub == pxr::TfToken("cylinder")) {
            idtx_physics_collider_set_tapered_cylinder(out, top, bot, mid);
        } else {
            idtx_physics_collider_set_tapered_capsule(out, top, bot, mid);
        }
        return out;
    }

    if (prim.IsA<pxr::UsdGeomSphere>()) {
        double radius = 1.0;
        pxr::UsdGeomSphere(prim).GetRadiusAttr().Get(&radius);
        idtx_physics_collider_set_sphere(out, static_cast<float>(radius));
    } else if (prim.IsA<pxr::UsdGeomCapsule>()) {
        double radius = 0.5, height = 1.0;
        auto cap = pxr::UsdGeomCapsule(prim);
        cap.GetRadiusAttr().Get(&radius);
        cap.GetHeightAttr().Get(&height);
        idtx_physics_collider_set_capsule(out, static_cast<float>(radius), static_cast<float>(height));
    } else if (prim.IsA<pxr::UsdGeomCylinder>()) {
        double radius = 0.5, height = 1.0;
        auto cyl = pxr::UsdGeomCylinder(prim);
        cyl.GetRadiusAttr().Get(&radius);
        cyl.GetHeightAttr().Get(&height);
        idtx_physics_collider_set_cylinder(out, static_cast<float>(radius), static_cast<float>(height));
    } else if (prim.IsA<pxr::UsdGeomCube>()) {
        // UsdGeomCube has a single `size` attr — half-extents come from
        // the local-transform scale. Decompose the upper-left 3x3 of
        // the transform we already read.
        float t[16]; idtx_physics_collider_get_transform(out, t);
        float sx = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
        float sy = std::sqrt(t[4] * t[4] + t[5] * t[5] + t[6] * t[6]);
        float sz = std::sqrt(t[8] * t[8] + t[9] * t[9] + t[10] * t[10]);
        // Strip the scale from the stored transform so the box is unit.
        float clean[16]; std::memcpy(clean, t, sizeof clean);
        if (sx > 1e-6f) for (int k = 0; k < 3; ++k) clean[k]     /= sx;
        if (sy > 1e-6f) for (int k = 0; k < 3; ++k) clean[4 + k] /= sy;
        if (sz > 1e-6f) for (int k = 0; k < 3; ++k) clean[8 + k] /= sz;
        idtx_physics_collider_set_transform(out, clean);
        idtx_physics_collider_set_box(out, sx, sy, sz);
    } else {
        idtx_physics_collider_destroy(out);
        return nullptr;
    }
    return out;
}

}  // namespace idtx::core::detail

extern "C" IDTX_CORE_API idtx_avatar_t* idtx_core_import_avatar_from_usd(const char* path)
{
    if (path == nullptr) return nullptr;
    pxr::UsdStageRefPtr stage = pxr::UsdStage::Open(std::string(path));
    if (!stage) return nullptr;

    pxr::UsdPrim root = idtx::core::detail::pick_avatar_root(stage);
    if (!root.IsValid()) return nullptr;

    idtx_avatar_t* avatar = idtx_avatar_create();
    idtx_avatar_set_name(avatar, root.GetName().GetString().c_str());

    // Round-trip provenance: remember the stage we came from so a later
    // idtx_core_export_avatar_to_usd_ex() can author deltas against it
    // (overlay / layer-only / flatten modes) without the host re-passing
    // the source path. Use the stage's resolved root-layer identifier so
    // sublayered / packaged stages resolve back to the right asset.
    {
        std::string src = stage->GetRootLayer()
            ? stage->GetRootLayer()->GetIdentifier()
            : std::string(path);
        idtx_avatar_set_source_usd_path(avatar, src.c_str());
    }

    // Recover source-VRM-version provenance from the root prim's
    // customData. Empty when the avatar was never upgraded.
    pxr::VtDictionary cd = root.GetCustomData();
    auto it = cd.find("vSekai:upgrade:fromVrm");
    if (it != cd.end() && it->second.IsHolding<std::string>()) {
        idtx_avatar_set_source_vrm_version(
            avatar, it->second.Get<std::string>().c_str());
    }

    // Read root transform if the root prim is Xformable.
    if (auto xf = pxr::UsdGeomXformable(root)) {
        pxr::GfMatrix4d local_xform(1.0);
        bool resets = false;
        xf.GetLocalTransformation(&local_xform, &resets);
        float m[16];
        idtx::core::gf_matrix_to_float16(local_xform, m);
        idtx_avatar_set_root_transform(avatar, m);
    }

    if (auto skel_prim = idtx::core::detail::find_first_skeleton_prim(root)) {
        if (auto* skel = idtx::core::detail::read_skeleton(skel_prim)) {
            idtx_avatar_set_skeleton(avatar, skel);
        }
    }

    // Two-pass walk: first collect materials so we can resolve mesh
    // bindings without having to grow a setter on idtx_avatar.
    std::unordered_map<std::string, int32_t> mat_path_to_index;
    for (auto const& prim : pxr::UsdPrimRange(root)) {
        if (prim.IsA<pxr::UsdShadeMaterial>()) {
            idtx_material_t* mat = idtx::core::detail::read_material(prim);
            int32_t idx = idtx_avatar_add_material(avatar, mat);
            mat_path_to_index[prim.GetPath().GetString()] = idx;
        }
    }

    for (auto const& prim : pxr::UsdPrimRange(root)) {
        if (!prim.IsA<pxr::UsdGeomMesh>()) continue;
        idtx_mesh_t* mesh = idtx::core::detail::read_mesh(prim);
        if (mesh == nullptr) continue;

        // Material binding resolution. Two paths:
        //   1. ComputeBoundMaterial — requires MaterialBindingAPI to be
        //      applied on the prim. Works for assets authored through
        //      UsdShadeMaterialBindingAPI::Bind() (our own exports do
        //      this implicitly).
        //   2. Direct material:binding rel lookup — for fixtures and
        //      third-party USDs that author the rel without applying
        //      the API explicitly. Common on hand-authored .usda files.
        int32_t mat_index = -1;
        pxr::UsdShadeMaterialBindingAPI binding(prim);
        if (binding) {
            pxr::UsdShadeMaterial bound = binding.ComputeBoundMaterial();
            if (bound) {
                auto it = mat_path_to_index.find(bound.GetPath().GetString());
                if (it != mat_path_to_index.end()) mat_index = it->second;
            }
        }
        if (mat_index < 0) {
            pxr::UsdRelationship rel = prim.GetRelationship(pxr::TfToken("material:binding"));
            if (rel) {
                pxr::SdfPathVector targets;
                rel.GetTargets(&targets);
                if (!targets.empty()) {
                    auto it = mat_path_to_index.find(targets[0].GetString());
                    if (it != mat_path_to_index.end()) mat_index = it->second;
                }
            }
        }
        idtx_avatar_add_mesh(avatar, mesh, mat_index);
    }

    // Physics colliders — walk for PhysicsCollisionAPI-applied prims.
    for (auto const& prim : pxr::UsdPrimRange(root)) {
        if (auto* pc = idtx::core::detail::read_physics_collider(prim)) {
            idtx_avatar_add_physics_collider(avatar, pc);
        }
    }

    // Spring bones — colliders first so chains can reference them by index.
    // Indices in the USD attributes correspond to walk order under the
    // SpringBones scope, which is what the exporter also writes.
    for (auto const& prim : pxr::UsdPrimRange(root)) {
        if (auto* col = idtx::core::detail::read_spring_collider(prim)) {
            idtx_avatar_add_spring_collider(avatar, col);
        }
    }
    for (auto const& prim : pxr::UsdPrimRange(root)) {
        if (auto* chain = idtx::core::detail::read_spring_chain(prim)) {
            idtx_avatar_add_spring_chain(avatar, chain);
        }
    }

    // Coordinate-system normalisation. VRM 1.0 is Y-up; USD's
    // UsdGeomGetStageUpAxis can be Y or Z (Blender authors Z-up by
    // default, scoot from Maya / Houdini authors Y-up). When the
    // source is Z-up, bake the rotation INTO every position, normal,
    // bind matrix, and the root rest transforms so the idtx_avatar
    // lives natively in Y-up. Avoids fudging with a glTF root-node
    // rotation that downstream consumers can ignore or strip.
    //
    // Rotation matrix (USD row-major):
    //   ( (1, 0, 0, 0),
    //     (0, 0,-1, 0),
    //     (0, 1, 0, 0),
    //     (0, 0, 0, 1) )
    // takes (x, y_zup, z_zup) -> (x, z_zup, -y_zup). +Z up -> +Y up.
    pxr::TfToken upAxis = pxr::UsdGeomGetStageUpAxis(stage);
    if (upAxis == pxr::UsdGeomTokens->z) {
        pxr::GfMatrix4d R(1.0);
        R.SetRow(1, pxr::GfVec4d(0, 0, -1, 0));
        R.SetRow(2, pxr::GfVec4d(0, 1,  0, 0));

        auto rotate_vec3 = [&](float& x, float& y, float& z) {
            pxr::GfVec4f v(x, y, z, 0.0f);   // direction (w=0)
            pxr::GfVec4d v_d(v[0], v[1], v[2], 0.0);
            v_d = v_d * R;
            x = static_cast<float>(v_d[0]);
            y = static_cast<float>(v_d[1]);
            z = static_cast<float>(v_d[2]);
        };

        auto rotate_mat4 = [&](float m[16]) {
            pxr::GfMatrix4d M = idtx::core::float16_to_gf_matrix(m);
            // World-space transforms (bind) need to be rotated into
            // the new world frame: M' = M * R (since USD uses
            // row-vector convention, premultiplying by the input
            // basis change is on the RIGHT).
            M = M * R;
            idtx::core::gf_matrix_to_float16(M, m);
        };

        // Bone bind matrices (world-space) and root bones' rest
        // matrices (relative-to-world). Non-root bones' rests are
        // joint-local — the parent's frame already carries the
        // rotation, so the local transform stays unchanged.
        if (auto* skel = idtx_avatar_get_skeleton(avatar)) {
            int32_t bc = idtx_skeleton_get_bone_count(skel);
            for (int32_t i = 0; i < bc; ++i) {
                float m[16];
                idtx_skeleton_get_bone_bind(skel, i, m);
                rotate_mat4(m);
                idtx_skeleton_set_bone_bind(skel, i, m);

                if (idtx_skeleton_get_bone_parent(skel, i) < 0) {
                    idtx_skeleton_get_bone_rest(skel, i, m);
                    rotate_mat4(m);
                    idtx_skeleton_set_bone_rest(skel, i, m);
                }
            }
        }

        // Mesh positions + normals. For SKINNED meshes, vertex
        // positions live in the (bind-time) mesh-local frame; the
        // bind matrices we just rotated reproject them on the GPU.
        // For UNSKINNED meshes, the mesh node's transform handles
        // positioning. In both cases the per-vertex data is in a
        // Z-up frame and needs the rotation baked in so consumers
        // that respect Y-up convention render correctly.
        int32_t mesh_count_local = idtx_avatar_get_mesh_count(avatar);
        for (int32_t mi = 0; mi < mesh_count_local; ++mi) {
            auto* mh = idtx_avatar_get_mesh(avatar, mi);
            if (mh == nullptr) continue;
            int32_t vc = idtx_mesh_get_vertex_count(mh);
            if (vc <= 0) continue;
            std::vector<float> pos(static_cast<size_t>(vc) * 3);
            idtx_mesh_get_positions(mh, pos.data());
            for (int32_t v = 0; v < vc; ++v) {
                rotate_vec3(pos[v*3], pos[v*3+1], pos[v*3+2]);
            }
            std::vector<float> nrm;
            if (idtx_mesh_has_normals(mh)) {
                nrm.resize(static_cast<size_t>(vc) * 3);
                idtx_mesh_get_normals(mh, nrm.data());
                for (int32_t v = 0; v < vc; ++v) {
                    rotate_vec3(nrm[v*3], nrm[v*3+1], nrm[v*3+2]);
                }
            }
            // Re-set vertices (positions + optionally normals; keep
            // existing uvs / colors). idtx_mesh_set_vertices reads
            // existing buffers for the optional channels.
            std::vector<float> uvs, cols;
            if (idtx_mesh_has_uvs(mh)) {
                uvs.resize(static_cast<size_t>(vc) * 2);
                idtx_mesh_get_uvs(mh, uvs.data());
            }
            if (idtx_mesh_has_colors(mh)) {
                cols.resize(static_cast<size_t>(vc) * 4);
                idtx_mesh_get_colors(mh, cols.data());
            }
            idtx_mesh_set_vertices(mh, vc, pos.data(),
                nrm.empty() ? nullptr : nrm.data(),
                uvs.empty() ? nullptr : uvs.data(),
                cols.empty() ? nullptr : cols.data());
        }

        // Spring-bone gravity dirs (world-space directions).
        int32_t chain_count_local = idtx_avatar_get_spring_chain_count(avatar);
        for (int32_t i = 0; i < chain_count_local; ++i) {
            auto* ch = idtx_avatar_get_spring_chain(avatar, i);
            float g[3]; idtx_spring_chain_get_gravity_dir(ch, g);
            rotate_vec3(g[0], g[1], g[2]);
            idtx_spring_chain_set_gravity_dir(ch, g[0], g[1], g[2]);
        }

        // Avatar root transform — pre-rotation it was an identity
        // (or whatever the SkelRoot's local xform encoded) in the
        // Z-up frame. We already rotated the data we care about
        // INTO Y-up, so the root_transform stays as-is — it
        // represents "this avatar's identity transform in the new
        // Y-up world frame".
    }

    return avatar;
}
