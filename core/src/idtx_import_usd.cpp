// Copyright 2026 V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// idtx_core_import_avatar_from_usd — read a USD file and rebuild an
// idtx_avatar_t* from it. Reverse direction of idtx_export_usd.cpp;
// powers the future Unity import path.

#include "idtx_core/idtx_core.h"
#include "idtx_core/idtx_scene.h"
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

    // Double-sided: USD authors `doubleSided` per-mesh, but engines key it
    // per-material, so we carry it on the material via the VSekaiMaterialAPI
    // schema attribute `v_sekai:doubleSided` (our round-trip form). Foreign
    // USD without the schema is handled at mesh-read time (see ConvertMesh).
    if (auto a = prim.GetAttribute(pxr::TfToken("v_sekai:doubleSided"))) {
        bool ds = false;
        if (a.Get(&ds) && ds) {
            idtx_material_set_double_sided(out, 1);
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
    // ONE reader. Geometry (meshes + skeleton + materials) AND the coordinate-
    // frame change of basis come from the shared scene converter — the same code
    // path the Godot host uses. We build the flat avatar from the converted
    // (Y-up, fully baked) scene, then layer on the stage-only VRM extras below.
    // This replaces the former bespoke read_mesh / read_skeleton + hand-rolled
    // up-axis rotation, which diverged from the converter and mis-handled the
    // SkelRoot's placement (root change is not a full conversion).
    idtx_scene_t* scene = idtx_core_import_scene_from_usd(path);
    if (scene == nullptr) return nullptr;
    idtx_avatar_t* avatar = idtx_scene_build_avatar(scene);
    idtx_core_scene_destroy(scene);
    if (avatar == nullptr) return nullptr;

    // Reopen the stage for the extras the flat scene does not carry (provenance,
    // physics colliders, spring bones). USD caches the layer, so this is cheap.
    pxr::UsdStageRefPtr stage = pxr::UsdStage::Open(std::string(path));
    if (!stage) return avatar;            // geometry is already valid

    pxr::UsdPrim root = idtx::core::detail::pick_avatar_root(stage);
    if (!root.IsValid()) return avatar;
    idtx_avatar_set_name(avatar, root.GetName().GetString().c_str());

    // Round-trip provenance: source layer identifier (so a later export can
    // author deltas against it) + source-VRM-upgrade version from customData.
    {
        std::string src = stage->GetRootLayer()
            ? stage->GetRootLayer()->GetIdentifier()
            : std::string(path);
        idtx_avatar_set_source_usd_path(avatar, src.c_str());
    }
    {
        pxr::VtDictionary cd = root.GetCustomData();
        auto it = cd.find("vSekai:upgrade:fromVrm");
        if (it != cd.end() && it->second.IsHolding<std::string>()) {
            idtx_avatar_set_source_vrm_version(
                avatar, it->second.Get<std::string>().c_str());
        }
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

    // Coordinate-system normalisation for the world-space EXTRAS only. The
    // geometry (mesh verts/normals, skeleton bind/rest) was already rebased into
    // the converter's Y-up universe frame by idtx_scene_build_avatar — we must
    // NOT touch it again here. Spring-bone gravity is an authored world-space
    // direction the flat scene doesn't carry, so it still needs the same change
    // of basis. Collider offsets are bone-relative and ride the baked skeleton.
    //
    //   Z-up -> Y-up:  (x, y, z) -> (x,  z, -y)
    //   X-up -> Y-up:  (x, y, z) -> (-y, x,  z)
    pxr::TfToken upAxis = pxr::UsdGeomGetStageUpAxis(stage);
    if (upAxis == pxr::UsdGeomTokens->z || upAxis == pxr::UsdGeomTokens->x) {
        const bool z_up = (upAxis == pxr::UsdGeomTokens->z);
        auto rebase_dir = [&](float g[3]) {
            const float x = g[0], y = g[1], z = g[2];
            if (z_up) { g[0] = x;  g[1] = z; g[2] = -y; }
            else      { g[0] = -y; g[1] = x; g[2] = z;  }
        };
        int32_t chain_count_local = idtx_avatar_get_spring_chain_count(avatar);
        for (int32_t i = 0; i < chain_count_local; ++i) {
            auto* ch = idtx_avatar_get_spring_chain(avatar, i);
            float g[3]; idtx_spring_chain_get_gravity_dir(ch, g);
            rebase_dir(g);
            idtx_spring_chain_set_gravity_dir(ch, g[0], g[1], g[2]);
        }
    }

    return avatar;
}
