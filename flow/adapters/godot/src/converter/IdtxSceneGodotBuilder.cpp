/**************************************************************************/
/*  IdtxSceneGodotBuilder.cpp                                            */
/**************************************************************************/
/* Copyright 2026 V-Sekai contributors.                                   */
/* SPDX-License-Identifier: Apache-2.0 OR MPL-2.0                         */
/**************************************************************************/

#include "IdtxSceneGodotBuilder.h"

#include <array>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/skin.hpp>
#include <godot_cpp/classes/animation.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/node_path.hpp>

#include "idtx_core/idtx_scene.h"
#include "idtx_core/idtx_core.h"

#include "nodes/UsdXFormNode3D.h"
#include "nodes/UsdMeshInstanceNode3D.h"
#include "nodes/UsdSkeletonNode3D.h"
#include "nodes/UsdStaticBodyNode3D.h"

using namespace godot;

namespace idtxflow {

namespace {

// The 16 floats are row-major (USD row-vector convention): basis rows are
// m[0..2]/m[4..6]/m[8..10], translation m[12..14] — matches the old
// UsdGodotTypeConverter::toTransform.
Transform3D to_transform(const float m[16]) {
    Basis basis(Vector3(m[0], m[1], m[2]), Vector3(m[4], m[5], m[6]), Vector3(m[8], m[9], m[10]));
    return Transform3D(basis, Vector3(m[12], m[13], m[14]));
}

// The DISPLAY name for a bone: just the leaf joint (last path component). USD
// joint names are full ancestor chains ("root/hips/spine/..."); the Skeleton3D
// hierarchy already encodes parenting via bone parents, so flattening the whole
// path into the name is redundant and unreadable. ':' is still stripped (Godot
// forbids it in bone names). Uniqueness is enforced at the add_bone call site.
String leaf_bone_name(const char* usd_name) {
    String s = String(usd_name);
    const int slash = s.rfind("/");
    if (slash >= 0) {
        s = s.substr(slash + 1);
    }
    return s.replace(":", "_");
}

// Look up a scene texture by its path key and decode its raw bytes into a Godot
// texture. Format is chosen by file extension, with a jpg/png fallback for the
// usdz-resolved keys whose extension is ambiguous. Returns null if absent/bad.
Ref<Texture2D> load_scene_texture(idtx_scene_t* scene, const char* key) {
    if (!key || !*key) {
        return Ref<Texture2D>();
    }
    const int32_t n = idtx_scene_get_texture_count(scene);
    for (int32_t i = 0; i < n; ++i) {
        idtx_texture_t* t = idtx_scene_get_texture(scene, i);
        if (String(idtx_texture_get_name(t)) != String(key)) {
            continue;
        }
        const int32_t bc = idtx_texture_get_byte_count(t);
        if (bc <= 0) {
            return Ref<Texture2D>();
        }
        PackedByteArray buf;
        buf.resize(bc);
        idtx_texture_get_bytes(t, buf.ptrw());
        Ref<Image> img;
        img.instantiate();
        // Choose the decoder by SNIFFING the magic bytes, not the key's
        // extension. usdz-resolved keys look like "foo.usdz[0/bar.png]" (they end
        // in ']'), so an extension test mis-routes every packaged texture to the
        // JPEG path: load_jpg_from_buffer then fails on the PNG bytes and spams
        // "libjpeg ... ERR_PARSE_ERROR" before the fallback succeeds. The byte
        // signature is unambiguous and silences that noise.
        Error e = ERR_UNAVAILABLE;
        const uint8_t* p = buf.ptr();
        const int32_t len = buf.size();
        const bool is_png  = len >= 8 && p[0] == 0x89 && p[1] == 0x50 && p[2] == 0x4E && p[3] == 0x47;
        const bool is_jpeg = len >= 3 && p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF;
        const bool is_webp = len >= 12 && p[0] == 'R' && p[1] == 'I' && p[2] == 'F' && p[3] == 'F'
                                       && p[8] == 'W' && p[9] == 'E' && p[10] == 'B' && p[11] == 'P';
        if (is_png) {
            e = img->load_png_from_buffer(buf);
        } else if (is_jpeg) {
            e = img->load_jpg_from_buffer(buf);
        } else if (is_webp) {
            e = img->load_webp_from_buffer(buf);
        } else {
            // Unknown signature: fall back to the key's extension, PNG first.
            const String lower = String(key).to_lower();
            if (lower.ends_with(".jpg") || lower.ends_with(".jpeg")) {
                e = img->load_jpg_from_buffer(buf);
            } else {
                e = img->load_png_from_buffer(buf);
            }
        }
        if (e != OK) {
            return Ref<Texture2D>();
        }
        return ImageTexture::create_from_image(img);
    }
    return Ref<Texture2D>();
}

// Build an MToon ShaderMaterial from the vendored Godot-MToon-Shader for a
// material flagged MToon in-core (its origin was a toon shader — lilToon / SCSS /
// MToon — mapped to v_sekai:mtoon on export). Toon materials are UNLIT cel
// shading, so routing them through StandardMaterial3D's PBR gives wrong results
// (a lilToon _Glossiness of 1 lands as roughness 0 -> mirror). Returns null if
// the shader resource is missing so the caller can fall back to PBR.
Ref<Material> build_mtoon_material(idtx_scene_t* scene, const idtx_material_t* m) {
    static Ref<Shader> mtoon_shader;
    if (mtoon_shader.is_null()) {
        mtoon_shader = ResourceLoader::get_singleton()->load(
            "res://addons/Godot-MToon-Shader/mtoon.gdshader");
    }
    if (mtoon_shader.is_null()) {
        return Ref<Material>();  // addon absent -> caller uses the PBR path
    }
    Ref<ShaderMaterial> mat;
    mat.instantiate();
    mat->set_shader(mtoon_shader);
    if (const char* mname = idtx_material_get_name(m); mname && mname[0] != '\0') {
        mat->set_name(String(mname));
    }
    float rgba[4];
    idtx_material_get_base_color(m, rgba);
    mat->set_shader_parameter("_Color", Color(rgba[0], rgba[1], rgba[2], rgba[3]));
    float shade[3];
    idtx_material_get_mtoon_shade_color(m, shade);
    mat->set_shader_parameter("_ShadeColor", Color(shade[0], shade[1], shade[2], 1.0f));
    float rim[3];
    idtx_material_get_mtoon_rim_color(m, rim);
    mat->set_shader_parameter("_RimColor", Color(rim[0], rim[1], rim[2], 1.0f));
    mat->set_shader_parameter("_OutlineWidth", idtx_material_get_mtoon_outline_width(m));
    // Albedo doubles as the shade texture (MToon's default), so the cel band keeps
    // the same art when no dedicated shade map exists.
    if (Ref<Texture2D> tex = load_scene_texture(scene, idtx_material_get_base_color_texture(m)); tex.is_valid()) {
        mat->set_shader_parameter("_MainTex", tex);
        mat->set_shader_parameter("_ShadeTexture", tex);
    }
    if (Ref<Texture2D> ntex = load_scene_texture(scene, idtx_material_get_normal_texture(m)); ntex.is_valid()) {
        mat->set_shader_parameter("_BumpMap", ntex);
    }
    return mat;
}

// Build the Godot material for a node — the single material path for the whole
// builder. A material flagged MToon in-core becomes an MToon ShaderMaterial (the
// toon look); otherwise it prefers the node's bound idtx_material (UsdPreviewSurface
// base color / metallic / roughness, converted in-core); when the node has none
// (primitives, or a mesh with no bound material), falls back to its display color
// (constant interp -> albedo, else vertex-color).
Ref<Material> build_material_index(idtx_scene_t* scene, idtx_node_t* node, int32_t mi) {
    if (const idtx_material_t* mm = (mi >= 0) ? idtx_scene_get_material(scene, mi) : nullptr;
        mm && idtx_material_is_mtoon(mm) != 0) {
        if (Ref<Material> mtoon = build_mtoon_material(scene, mm); mtoon.is_valid()) {
            return mtoon;
        }
        // else: fall through to the PBR path below.
    }
    Ref<StandardMaterial3D> mat;
    mat.instantiate();

    const idtx_material_t* m = (mi >= 0) ? idtx_scene_get_material(scene, mi) : nullptr;
    if (m) {
        if (const char* mname = idtx_material_get_name(m); mname && mname[0] != '\0') {
            mat->set_name(String(mname));
        }
        float rgba[4];
        idtx_material_get_base_color(m, rgba);
        const Color albedo(rgba[0], rgba[1], rgba[2], rgba[3]);
        mat->set_albedo(albedo);
        mat->set_metallic(idtx_material_get_metallic(m));
        mat->set_roughness(idtx_material_get_roughness(m));
        // Texture maps extracted in-core from the (possibly usdz-packed) stage.
        if (Ref<Texture2D> albedo_tex = load_scene_texture(scene, idtx_material_get_base_color_texture(m)); albedo_tex.is_valid()) {
            mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, albedo_tex);
        }
        if (Ref<Texture2D> normal_tex = load_scene_texture(scene, idtx_material_get_normal_texture(m)); normal_tex.is_valid()) {
            mat->set_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING, true);
            mat->set_texture(BaseMaterial3D::TEXTURE_NORMAL, normal_tex);
        }
        switch (idtx_material_get_alpha_mode(m)) {
            case IDTX_ALPHA_MASK: {
                mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
                mat->set_alpha_scissor_threshold(idtx_material_get_alpha_cutoff(m));
            } break;
            case IDTX_ALPHA_BLEND: {
                mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
            } break;
            default: {
                if (albedo.a < 1.0f) {
                    mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
                }
            } break;
        }
        // Double-sided -> disable back-face culling so both faces render.
        if (idtx_material_get_double_sided(m) != 0) {
            mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
        }
        return mat;
    }

    // No bound material: fall back to the node's display color.
    const int32_t cc = idtx_node_get_display_color_count(node);
    if (cc > 0 && idtx_node_get_color_interpolation(node) == IDTX_COLOR_INTERP_CONSTANT) {
        std::vector<float> rgba(cc * 4);
        idtx_node_get_display_colors(node, rgba.data());
        const Color albedo(rgba[0], rgba[1], rgba[2], rgba[3]);
        mat->set_albedo(albedo);
        if (albedo.a < 1.0f) {
            mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        }
    } else {
        mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
    }
    return mat;
}

// Material for a node's own bound material (the single-material path).
Ref<Material> build_material(idtx_scene_t* scene, idtx_node_t* node) {
    return build_material_index(scene, node, idtx_node_get_material_index(node));
}

// Quantization factor for weld keys: positions/normals are rounded to this many
// steps per unit so tiny float differences do not split a smoothing vertex.
static constexpr float WELD_QUANT = 4096.0f;

// Integer key identifying a SMOOTHING vertex by its quantized base position and
// base normal (the mesh's smoothing groups). Coincident corners that share a
// position and a normal are one smooth vertex; a hard edge (same position,
// different authored normal) stays split. Mirrors Blender's glTF importer weld.
struct WeldKey {
    int64_t v[6];
    bool operator<(const WeldKey& other) const {
        for (int i = 0; i < 6; ++i) {
            if (v[i] != other.v[i]) {
                return v[i] < other.v[i];
            }
        }
        return false;
    }
};

static WeldKey MakeWeldKey(const Vector3& position, const Vector3& normal) {
    WeldKey key;
    key.v[0] = static_cast<int64_t>(Math::round(position.x * WELD_QUANT));
    key.v[1] = static_cast<int64_t>(Math::round(position.y * WELD_QUANT));
    key.v[2] = static_cast<int64_t>(Math::round(position.z * WELD_QUANT));
    key.v[3] = static_cast<int64_t>(Math::round(normal.x * WELD_QUANT));
    key.v[4] = static_cast<int64_t>(Math::round(normal.y * WELD_QUANT));
    key.v[5] = static_cast<int64_t>(Math::round(normal.z * WELD_QUANT));
    return key;
}

// Per-vertex normals respecting the source's smoothing groups (Blender's
// normals_split_get). The partition is keyed on the BASE (position, normal) via
// keyVerts/keyNormals; face normals are measured from facePositions -- pass the
// base positions for the rest normals, or the morphed positions for a shape's
// normals. Both use the SAME partition, so a face a shape does not move yields an
// identical normal in both -> zero delta there -> shapes stay independent. tris
// hold Godot's reversed winding, so the true outward normal is (C-A) x (B-A).
static PackedVector3Array ComputeGroupedNormals(
        const PackedVector3Array& facePositions,
        const PackedVector3Array& keyVerts,
        const PackedVector3Array& keyNormals,
        const PackedInt32Array& tris) {
    const int64_t vertexCount = facePositions.size();
    PackedVector3Array normals;
    normals.resize(vertexCount);

    std::map<WeldKey, Vector3> accumulated;
    for (int64_t t = 0; t + 2 < tris.size(); t += 3) {
        const int a = tris[t];
        const int b = tris[t + 1];
        const int c = tris[t + 2];
        const Vector3 faceNormal =
            (facePositions[c] - facePositions[a]).cross(facePositions[b] - facePositions[a]);
        accumulated[MakeWeldKey(keyVerts[a], keyNormals[a])] += faceNormal;
        accumulated[MakeWeldKey(keyVerts[b], keyNormals[b])] += faceNormal;
        accumulated[MakeWeldKey(keyVerts[c], keyNormals[c])] += faceNormal;
    }
    for (int64_t i = 0; i < vertexCount; ++i) {
        const Vector3 accumulatedNormal = accumulated[MakeWeldKey(keyVerts[i], keyNormals[i])];
        if (accumulatedNormal.length_squared() < 1e-20f) {
            normals[i] = Vector3(0.0f, 1.0f, 0.0f); // degenerate: fall back to UP
        } else {
            normals[i] = accumulatedNormal.normalized();
        }
    }
    return normals;
}

// idtx_mesh -> Godot ArrayMesh (single surface; subsets were merged in-core).
Ref<ArrayMesh> build_array_mesh(idtx_mesh_t* mesh) {
    Ref<ArrayMesh> out;
    out.instantiate();
    if (!mesh) return out;
    const int32_t vc = idtx_mesh_get_vertex_count(mesh);
    const int32_t ic = idtx_mesh_get_index_count(mesh);
    if (vc <= 0 || ic <= 0) return out;

    std::vector<float> pos(vc * 3);
    idtx_mesh_get_positions(mesh, pos.data());
    PackedVector3Array verts; verts.resize(vc);
    for (int32_t i = 0; i < vc; ++i) verts[i] = Vector3(pos[i*3], pos[i*3+1], pos[i*3+2]);

    std::vector<int32_t> idx(ic);
    idtx_mesh_get_indices(mesh, idx.data());
    PackedInt32Array tris; tris.resize(ic);
    // Reverse each triangle's winding for Godot. The core keeps the canonical
    // USD/glTF CCW-front winding (correct for USD round-trips and three.js/viser),
    // but Godot's PRIMITIVE_TRIANGLES treats CLOCKWISE as front-facing under
    // CULL_BACK (verified: SurfaceTool.generate_normals() yields -Z for a CCW
    // triangle). Feeding the CCW winding unchanged culls the outward-normal face
    // and renders every mesh inside-out. Swap the 2nd/3rd index per triangle so
    // the front face Godot keeps is the one whose authored normal points outward.
    for (int32_t t = 0; t + 2 < ic; t += 3) {
        tris[t]     = idx[t];
        tris[t + 1] = idx[t + 2];
        tris[t + 2] = idx[t + 1];
    }

    Array arrays; arrays.resize(Mesh::ARRAY_MAX);
    arrays[Mesh::ARRAY_VERTEX] = verts;
    arrays[Mesh::ARRAY_INDEX] = tris;

    PackedVector3Array baseNormals;  // kept for the blend-shape normal recompute below
    const bool base_has_normals = idtx_mesh_has_normals(mesh);
    if (base_has_normals) {
        std::vector<float> n(vc * 3); idtx_mesh_get_normals(mesh, n.data());
        baseNormals.resize(vc);
        for (int32_t i = 0; i < vc; ++i) baseNormals[i] = Vector3(n[i*3], n[i*3+1], n[i*3+2]);
        arrays[Mesh::ARRAY_NORMAL] = baseNormals;  // authored normals pass through unchanged
    }
    if (idtx_mesh_has_uvs(mesh)) {
        std::vector<float> u(vc * 2); idtx_mesh_get_uvs(mesh, u.data());
        PackedVector2Array uvs; uvs.resize(vc);
        for (int32_t i = 0; i < vc; ++i) uvs[i] = Vector2(u[i*2], u[i*2+1]);
        arrays[Mesh::ARRAY_TEX_UV] = uvs;
    }
    if (idtx_mesh_has_colors(mesh)) {
        std::vector<float> c(vc * 4); idtx_mesh_get_colors(mesh, c.data());
        PackedColorArray cols; cols.resize(vc);
        for (int32_t i = 0; i < vc; ++i) cols[i] = Color(c[i*4], c[i*4+1], c[i*4+2], c[i*4+3]);
        arrays[Mesh::ARRAY_COLOR] = cols;
    }

    // Skin influences (only present on skinned meshes; bpv==0 for static ones).
    // Godot requires exactly 4 or 8 bones+weights per vertex; pad/clamp the
    // core's per-vertex stride to whichever target fits, flagging 8-bone surfaces.
    uint64_t flags = 0;
    const int32_t bpv = idtx_mesh_get_bones_per_vertex(mesh);
    if (bpv > 0) {
        const int32_t target = (bpv <= 4) ? 4 : 8;
        const int32_t copy = (bpv < target) ? bpv : target;
        std::vector<int32_t> bi(vc * bpv);
        std::vector<float> wt(vc * bpv);
        idtx_mesh_get_bone_indices(mesh, bi.data());
        idtx_mesh_get_weights(mesh, wt.data());
        PackedInt32Array bones; bones.resize(vc * target);
        PackedFloat32Array weights; weights.resize(vc * target);
        for (int32_t v = 0; v < vc; ++v) {
            for (int32_t k = 0; k < target; ++k) {
                if (k < copy) {
                    bones[v * target + k] = bi[v * bpv + k];
                    weights[v * target + k] = wt[v * bpv + k];
                } else {
                    bones[v * target + k] = 0;
                    weights[v * target + k] = 0.0f;
                }
            }
        }
        arrays[Mesh::ARRAY_BONES] = bones;
        arrays[Mesh::ARRAY_WEIGHTS] = weights;
        if (target == 8) {
            flags |= Mesh::ARRAY_FLAG_USE_8_BONE_WEIGHTS;
        }
    }

    // Blend shapes (morph targets). Godot stores blend-shape normals octahedral-
    // encoded (unit direction only) and the skeleton compute shader normalizes on
    // decode, so RELATIVE-mode "morph - base" deltas are invalid (magnitude lost; a
    // zero delta encodes to garbage). Match Godot's own glTF importer instead: use
    // NORMALIZED mode and store ABSOLUTE morphed positions and ABSOLUTE unit normals.
    // The shader then evaluates normalize((1 - sum w)*base + sum w*absolute) -- for
    // positions this is algebraically the additive-delta result, and every stored
    // normal stays unit, hence octahedral-lossless.
    TypedArray<Array> blend_arrays;
    float max_bs_delta = 0.0f;  // largest morph offset, to size the custom AABB
    const int32_t bs_count = idtx_mesh_get_blendshape_count(mesh);
    if (bs_count > 0) {
        out->set_blend_shape_mode(Mesh::BLEND_SHAPE_MODE_NORMALIZED);
        const bool has_n = base_has_normals;


        for (int32_t b = 0; b < bs_count; ++b) {
            out->add_blend_shape(StringName(idtx_mesh_get_blendshape_name(mesh, b)));

            // ABSOLUTE morphed positions: base + delta.
            std::vector<float> dp(vc * 3); idtx_mesh_get_blendshape_position_deltas(mesh, b, dp.data());
            PackedVector3Array bverts; bverts.resize(vc);
            for (int32_t i = 0; i < vc; ++i) {
                bverts[i] = verts[i] + Vector3(dp[i*3], dp[i*3+1], dp[i*3+2]);
                max_bs_delta = Math::max(max_bs_delta, Math::abs(dp[i*3]));
                max_bs_delta = Math::max(max_bs_delta, Math::abs(dp[i*3+1]));
                max_bs_delta = Math::max(max_bs_delta, Math::abs(dp[i*3+2]));
            }
            Array bs_arr; bs_arr.resize(Mesh::ARRAY_MAX);
            bs_arr[Mesh::ARRAY_VERTEX] = bverts;

            // ABSOLUTE unit normals (Godot rejects a surface whose blend shapes do
            // not carry the same Vertex/Normal arrays as the base, so every shape
            // must supply a normal array whenever the base has normals).
            if (has_n) {
                // absolute = normalize(base + authored delta). A shape with no
                // authored offsets has zero deltas and reproduces the base
                // normal exactly; nothing is recomputed at conversion.
                std::vector<float> dn(vc * 3); idtx_mesh_get_blendshape_normal_deltas(mesh, b, dn.data());
                PackedVector3Array bnorm; bnorm.resize(vc);
                for (int32_t i = 0; i < vc; ++i) {
                    const Vector3 nn = baseNormals[i] + Vector3(dn[i*3], dn[i*3+1], dn[i*3+2]);
                    bnorm[i] = (nn.length_squared() < 1e-20f) ? baseNormals[i] : nn.normalized();
                }
                bs_arr[Mesh::ARRAY_NORMAL] = bnorm;
            }
            blend_arrays.push_back(bs_arr);
        }
    }

    out->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, blend_arrays, Dictionary(), flags);

    // Set a custom AABB covering the base geometry grown by the largest morph
    // offset, so editor framing and culling stay correct across the full morph
    // range instead of relying on Godot's per-mode auto AABB.
    if (verts.size() > 0) {
        AABB box(verts[0], Vector3());
        for (int32_t i = 1; i < verts.size(); ++i) box.expand_to(verts[i]);
        box = box.grow(max_bs_delta);
        out->set_custom_aabb(box);
    }
    return out;
}

// Apply each blend shape's current/default weight onto the MeshInstance3D (the
// mesh must already be set so the shapes exist). Mirrors the configured pose
// the exporter captured; a RESET to 0 gives the rest pose.

// USD in-betweens, promoted to their own shapes by the core: a primary and its
// in-betweens form a group sorted by position, and the primary's single weight
// evaluates through the piecewise-linear basis (base at 0, in-betweens at their
// positions, primary at 1) into per-member values — USD's interpolation, exactly.
struct BlendGroups {
    // primary name -> [(shape index, position)] ascending; size 1 = plain shape.
    std::map<std::string, std::vector<std::pair<int32_t, float>>> groups;
    std::vector<std::string> names;
};

static BlendGroups collect_blend_groups(idtx_mesh_t* mesh) {
    BlendGroups bg;
    const int32_t n = idtx_mesh_get_blendshape_count(mesh);
    for (int32_t b = 0; b < n; ++b) {
        bg.names.push_back(idtx_mesh_get_blendshape_name(mesh, b));
        const char* primary = idtx_mesh_get_blendshape_primary(mesh, b);
        const std::string key = (primary && primary[0]) ? primary : bg.names.back();
        bg.groups[key].push_back({b, idtx_mesh_get_blendshape_position(mesh, b)});
    }
    for (auto& g : bg.groups)
        std::sort(g.second.begin(), g.second.end(),
                  [](const auto& x, const auto& y) { return x.second < y.second; });
    return bg;
}

static void eval_blend_hat(const std::vector<std::pair<int32_t, float>>& entries,
                           float w, std::vector<std::pair<int32_t, float>>& out) {
    out.clear();
    for (const auto& e : entries) out.push_back({e.first, 0.0f});
    if (entries.empty() || w <= 0.0f) return;
    const size_t n = entries.size();
    if (w >= entries[n - 1].second) {
        // past the last member its own axis scales linearly (UsdSkel's >1 rule)
        out[n - 1].second = w / entries[n - 1].second;
        return;
    }
    float prev = 0.0f;
    for (size_t k = 0; k < n; ++k) {
        const float pos = entries[k].second;
        if (w <= pos) {
            const float t = (pos - prev) > 0.0f ? (w - prev) / (pos - prev) : 1.0f;
            if (k > 0) out[k - 1].second = 1.0f - t;
            out[k].second = t;
            return;
        }
        prev = pos;
    }
}

void apply_blend_shape_weights(MeshInstance3D* mi, idtx_mesh_t* mesh) {
    if (mi == nullptr || mesh == nullptr) return;
    const int32_t bs_count = idtx_mesh_get_blendshape_count(mesh);
    for (int32_t b = 0; b < bs_count; ++b) {
        mi->set_blend_shape_value(b, idtx_mesh_get_blendshape_weight(mesh, b));
    }
    // Spread each grouped primary's authored weight through the basis.
    BlendGroups bg = collect_blend_groups(mesh);
    std::vector<std::pair<int32_t, float>> vals;
    for (const auto& g : bg.groups) {
        if (g.second.size() < 2) continue;
        const float w = idtx_mesh_get_blendshape_weight(mesh, g.second.back().first);
        eval_blend_hat(g.second, w, vals);
        for (const auto& v : vals) mi->set_blend_shape_value(v.first, v.second);
    }
}

// A node always needs a non-empty name; an empty one makes Godot fall back to
// "@ClassName@id" (which then bakes into the cached .tscn). Some USD prims have
// no usable name (the default/pseudo-root, variant-composed prims), so fall back
// to the prim path's leaf, then a generic default.
String node_display_name(idtx_node_t* node) {
    String name = String(idtx_node_get_name(node));
    if (!name.strip_edges().is_empty()) {
        return name;
    }
    name = String(idtx_node_get_path(node)).get_file();
    if (!name.strip_edges().is_empty()) {
        return name;
    }
    return String("UsdNode");
}

Node3D* build_one(idtx_scene_t* scene, idtx_node_t* node) {
    const idtx_node_kind_t kind = idtx_node_get_kind(node);
    float m[16]; idtx_node_get_local_transform(node, m);
    const Transform3D xform = to_transform(m);

    switch (kind) {
        case IDTX_NODE_XFORM:
        case IDTX_NODE_COLLISION_ROOT: {
            auto* n = memnew(UsdXformNode3D);
            n->set_transform(xform);
            if (kind == IDTX_NODE_COLLISION_ROOT) {
                float col[3]; const char* ident = nullptr; int32_t en = 0, hl = 0;
                idtx_node_get_collision_root(node, col, &ident, &en, &hl);
                n->set_meta("collision_enabled", (bool)en);
                n->set_meta("highlightable", (bool)hl);
                n->set_meta("highlight_color", Color(col[0], col[1], col[2]));
                n->set_meta("identifier", String(ident ? ident : ""));
            }
            return n;
        }
        case IDTX_NODE_CUBE: {
            Ref<BoxMesh> box; box.instantiate();
            double s = idtx_node_get_cube_size(node); box->set_size(Vector3(s, s, s));
            box->set_material(build_material(scene, node));
            auto* n = memnew(UsdMeshInstanceNode3D); n->set_mesh(box); n->set_transform(xform); return n;
        }
        case IDTX_NODE_CYLINDER: {
            Ref<CylinderMesh> cyl; cyl.instantiate();
            double r, h; idtx_axis_t a; idtx_node_get_cylinder(node, &r, &h, &a);
            cyl->set_top_radius(r); cyl->set_bottom_radius(r); cyl->set_height(h);
            cyl->set_material(build_material(scene, node));
            auto* n = memnew(UsdMeshInstanceNode3D); n->set_mesh(cyl); n->set_transform(xform); return n;
        }
        case IDTX_NODE_CONE: {
            Ref<CylinderMesh> cyl; cyl.instantiate();
            double r, h; idtx_axis_t a; idtx_node_get_cone(node, &r, &h, &a);
            cyl->set_top_radius(0.0); cyl->set_bottom_radius(r); cyl->set_height(h);
            cyl->set_material(build_material(scene, node));
            auto* n = memnew(UsdMeshInstanceNode3D); n->set_mesh(cyl); n->set_transform(xform); return n;
        }
        case IDTX_NODE_SPHERE: {
            Ref<SphereMesh> sph; sph.instantiate();
            double r = idtx_node_get_sphere_radius(node); sph->set_radius(r); sph->set_height(r * 2.0);
            sph->set_material(build_material(scene, node));
            auto* n = memnew(UsdMeshInstanceNode3D); n->set_mesh(sph); n->set_transform(xform); return n;
        }
        case IDTX_NODE_MESH: {
            Ref<ArrayMesh> mesh = build_array_mesh(idtx_node_get_mesh(node));
            if (mesh->get_surface_count() > 0) mesh->surface_set_material(0, build_material(scene, node));
            auto* n = memnew(UsdMeshInstanceNode3D); n->set_mesh(mesh);
            apply_blend_shape_weights(n, idtx_node_get_mesh(node));
            n->set_transform(xform); return n;
        }
        case IDTX_NODE_SKELETON: {
            auto* sk = memnew(UsdSkeletonNode3D);
            if (idtx_skeleton_t* skel = idtx_node_get_skeleton(node)) {
                const int32_t bc = idtx_skeleton_get_bone_count(skel);
                // Maps each animation track's NodePath (sanitized joint name) to a
                // bone index; UsdSkeletonNode3D::_process uses it to drive poses.
                Dictionary joint_bone_map;
                for (int32_t b = 0; b < bc; ++b) {
                    const char* raw = idtx_skeleton_get_bone_name(skel, b);
                    // Visible name = leaf joint. add_bone rejects duplicates (and
                    // returns -1, which would desync bone indices from the skinning
                    // data), so resolve a free name up front via find_bone (-1 when
                    // the name is unused) before adding.
                    String display = leaf_bone_name(raw);
                    if (display.is_empty()) {
                        display = String("bone");
                    }
                    String unique = display;
                    for (int32_t suffix = 2; sk->find_bone(unique) != -1; ++suffix) {
                        unique = display + "_" + String::num_int64(suffix);
                    }
                    int32_t bi = sk->add_bone(unique);
                    sk->set_bone_parent(bi, idtx_skeleton_get_bone_parent(skel, b));
                    float rest[16]; idtx_skeleton_get_bone_rest(skel, b, rest);
                    sk->set_bone_rest(bi, to_transform(rest));
                    // Join key is the raw, full USD joint path. Dictionary/NodePath
                    // keys have none of add_bone's ':' / '/' restrictions, and USD
                    // joint paths are unique, so the track -> bone-index lookup stays
                    // unambiguous even when leaf display names collide — no lossy
                    // sanitization needed (the matching track path is built the same).
                    joint_bone_map[NodePath(String(raw))] = bi;
                }
                sk->reset_bone_poses();
                sk->set_joint_to_bone_map(joint_bone_map);
            }
            sk->set_transform(xform);
            // Build the skeletal animation clip (per-joint translation/rotation/
            // scale tracks). Playback is driven by UsdSkeletonNode3D::_process,
            // which resolves each track's path through the joint->bone map above.
            if (idtx_anim_t* a = idtx_node_get_animation(node)) {
                Ref<Animation> anim;
                anim.instantiate();
                anim->set_length(idtx_anim_get_length(a));
                // In-between groups from the first skinned mesh: a grouped
                // primary's weight track bakes through the basis into one track
                // per member, with keys inserted where the weight crosses a
                // member position between authored samples -- baking only at
                // authored times leaves an in-between no sample lands on flat.
                BlendGroups bg;
                if (idtx_mesh_t* gm = idtx_node_get_skinned_mesh(node)) {
                    bg = collect_blend_groups(gm);
                }
                const int32_t tc = idtx_anim_get_track_count(a);
                for (int32_t t = 0; t < tc; ++t) {
                    const idtx_anim_track_type_t tt = idtx_anim_track_get_type(a, t);
                    const int32_t kc = idtx_anim_track_get_key_count(a, t);
                    if (tt == IDTX_ANIM_TRACK_BLEND_WEIGHT) {
                        const std::string shape = idtx_anim_track_get_bone_name(a, t);
                        auto git = bg.groups.find(shape);
                        const bool grouped = git != bg.groups.end() && git->second.size() > 1;
                        if (!grouped) {
                            const int32_t ti = anim->add_track(Animation::TYPE_BLEND_SHAPE);
                            anim->track_set_path(ti, NodePath(String(shape.c_str())));
                            for (int32_t k = 0; k < kc; ++k) {
                                anim->blend_shape_track_insert_key(ti, idtx_anim_track_get_key_time(a, t, k),
                                                                   idtx_anim_track_get_key_float(a, t, k));
                            }
                            continue;
                        }
                        std::vector<double> times;
                        std::vector<float> ws;
                        for (int32_t k = 0; k < kc; ++k) {
                            const double t0 = idtx_anim_track_get_key_time(a, t, k);
                            const float w0 = idtx_anim_track_get_key_float(a, t, k);
                            times.push_back(t0); ws.push_back(w0);
                            if (k + 1 >= kc) continue;
                            const double t1 = idtx_anim_track_get_key_time(a, t, k + 1);
                            const float w1 = idtx_anim_track_get_key_float(a, t, k + 1);
                            if (!(t1 > t0) || w0 == w1) continue;
                            const float lo = std::min(w0, w1), hi = std::max(w0, w1);
                            for (const auto& e : git->second) {
                                if (e.second > lo && e.second < hi) {
                                    times.push_back(t0 + (t1 - t0) * double((e.second - w0) / (w1 - w0)));
                                    ws.push_back(e.second);
                                }
                            }
                        }
                        std::vector<size_t> order(times.size());
                        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
                        std::sort(order.begin(), order.end(),
                                  [&](size_t x, size_t y) { return times[x] < times[y]; });
                        std::vector<int32_t> member_tracks;
                        for (const auto& e : git->second) {
                            const int32_t ti = anim->add_track(Animation::TYPE_BLEND_SHAPE);
                            anim->track_set_path(ti, NodePath(String(bg.names[e.first].c_str())));
                            member_tracks.push_back(ti);
                        }
                        std::vector<std::pair<int32_t, float>> vals;
                        for (size_t oi : order) {
                            eval_blend_hat(git->second, ws[oi], vals);
                            for (size_t m = 0; m < vals.size(); ++m) {
                                anim->blend_shape_track_insert_key(member_tracks[m], times[oi], vals[m].second);
                            }
                        }
                        continue;
                    }
                    Animation::TrackType gt = Animation::TYPE_POSITION_3D;
                    if (tt == IDTX_ANIM_TRACK_ROTATION) {
                        gt = Animation::TYPE_ROTATION_3D;
                    } else if (tt == IDTX_ANIM_TRACK_SCALE) {
                        gt = Animation::TYPE_SCALE_3D;
                    }
                    const int32_t ti = anim->add_track(gt);
                    // The path holds the joint name; UsdSkeletonNode3D::_process
                    // resolves it, not the scene tree.
                    anim->track_set_path(ti, NodePath(String(idtx_anim_track_get_bone_name(a, t))));
                    for (int32_t k = 0; k < kc; ++k) {
                        const double time = idtx_anim_track_get_key_time(a, t, k);
                        if (tt == IDTX_ANIM_TRACK_ROTATION) {
                            float q[4]; idtx_anim_track_get_key_quat(a, t, k, q);
                            anim->rotation_track_insert_key(ti, time, Quaternion(q[0], q[1], q[2], q[3]));
                        } else {
                            float v[3]; idtx_anim_track_get_key_vec3(a, t, k, v);
                            if (tt == IDTX_ANIM_TRACK_SCALE) {
                                anim->scale_track_insert_key(ti, time, Vector3(v[0], v[1], v[2]));
                            } else {
                                anim->position_track_insert_key(ti, time, Vector3(v[0], v[1], v[2]));
                            }
                        }
                    }
                }
                sk->set_animation(anim);
            }
            // Attach the skinned mesh as a MeshInstance3D child and bind GPU skin
            // deformation: build_array_mesh emits per-vertex bone/weight arrays, the
            // MeshInstance points at this Skeleton3D (via _notification on parenting),
            // and a Skin derived from the bone rests maps mesh space -> bone space.
            // One skinned MeshInstance per source skin target, each with its own
            // material (the converter keeps them separate so the authored
            // materials survive). idtx_node_get_skinned_mesh is [0].
            const int32_t smc = idtx_node_get_skinned_mesh_count(node);
            for (int32_t si = 0; si < smc; ++si) {
                idtx_mesh_t* sm = idtx_node_get_skinned_mesh_at(node, si);
                if (!sm) { continue; }
                Ref<ArrayMesh> mesh = build_array_mesh(sm);
                if (mesh->get_surface_count() > 0) {
                    mesh->surface_set_material(
                        0, build_material_index(scene, node, idtx_node_get_skinned_mesh_material(node, si)));
                    auto* mi = memnew(UsdMeshInstanceNode3D);
                    mi->set_mesh(mesh);
                    apply_blend_shape_weights(mi, sm);
                    mi->set_skeleton(sk);
                    const char* mn = idtx_mesh_get_name(sm);
                    mi->set_name((mn && mn[0] != '\0')
                        ? String(mn)
                        : (smc > 1 ? String("Skin_") + String::num_int64(si) : String("Skin")));
                    sk->add_child(mi, true);
                    mi->set_skin(sk->create_skin_from_rest_transforms());
                }
            }
            return sk;
        }
        case IDTX_NODE_COLLISION: {
            auto* n = memnew(UsdStaticBodyNode3D);
            n->set_transformData(xform);
            idtx_collision_shape_t shape; idtx_axis_t axis; double h, r;
            idtx_node_get_collision(node, &shape, &axis, &h, &r);
            static const char* SHAPE[] = {"Cube","Sphere","Capsule","Cylinder","Cone","Mesh"};
            n->set_collision_shape(std::string((shape >= 0 && shape < 6) ? SHAPE[shape] : "Cube"));
            PackedStringArray types; const int32_t tc = idtx_node_get_collision_type_count(node);
            types.resize(tc); for (int32_t i = 0; i < tc; ++i) types[i] = String(idtx_node_get_collision_type(node, i));
            n->set_collision_type(types);
            n->set_axis(axis == IDTX_AXIS_X ? Vector3(1,0,0) : axis == IDTX_AXIS_Z ? Vector3(0,0,1) : Vector3(0,1,0));
            if (h) n->set_height(h);
            if (r) n->set_radius(r);
            return n;
        }
    }
    return nullptr;
}

}  // namespace

std::vector<Node3D*> BuildGodotNodesFromScene(idtx_scene* scene) {
    std::vector<Node3D*> roots;
    auto* sc = reinterpret_cast<idtx_scene_t*>(scene);
    if (!sc) return roots;

    const int32_t count = idtx_scene_get_node_count(sc);
    std::vector<Node3D*> built(count, nullptr);

    for (int32_t i = 0; i < count; ++i) {
        idtx_node_t* node = idtx_scene_get_node(sc, i);
        Node3D* n = build_one(sc, node);
        built[i] = n;
        if (!n) continue;
        n->set_name(node_display_name(node));
        n->set_meta("USD_NODE", true);
        if (IUsdNode3D* un = IUsdNode3D::from_node(n)) {
            un->set_prim_name(idtx_node_get_name(node));
            un->set_prim_path(idtx_node_get_path(node));
        }
    }

    // Parent + collect roots. Nodes are depth-first so parents precede children.
    for (int32_t i = 0; i < count; ++i) {
        if (!built[i]) continue;
        const int32_t p = idtx_node_get_parent(idtx_scene_get_node(sc, i));
        // force_readable_name = true: never let Godot fall back to "@Class@id"
        // for a name (which then bakes into the cached .tscn).
        if (p >= 0 && p < count && built[p]) built[p]->add_child(built[i], true);
        else roots.push_back(built[i]);
    }

    // The up-axis change of basis is fully baked into the geometry + transforms
    // by the core converter, which always emits Y-up (idtx_scene_get_up_axis is
    // always IDTX_AXIS_Y now), so there is NO host-side root rotation — a root
    // rotation was never a full conversion. Only the stage's metersPerUnit scale
    // remains a host concern.
    const float mpu = (float)idtx_scene_get_meters_per_unit(sc);
    for (Node3D* root : roots) {
        root->set_scale(root->get_scale() * mpu);
    }
    return roots;
}

}  // namespace idtxflow
