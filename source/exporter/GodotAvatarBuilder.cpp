// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

#include "GodotAvatarBuilder.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/capsule_shape3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/csg_shape3d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/cylinder_shape3d.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/physics_body3d.hpp>
#include <godot_cpp/classes/shape3d.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/classes/sphere_shape3d.hpp>
#include <godot_cpp/classes/spring_bone_collision3d.hpp>
#include <godot_cpp/classes/spring_bone_collision_capsule3d.hpp>
#include <godot_cpp/classes/spring_bone_collision_sphere3d.hpp>
#include <godot_cpp/classes/spring_bone_simulator3d.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/transform3d.hpp>

#include <cmath>
#include <unordered_map>
#include <vector>

namespace idtxflow::exporter
{

// Pack a Godot Transform3D into a row-major float[16] — the layout
// idtx_skeleton / idtx_avatar expect.
static void GodotTransformToFloat16(godot::Transform3D const& t, float out[16])
{
    // Godot Transform3D: 3x3 basis (rows xyz) + Vector3 origin.
    // idtx convention (matches USD GfMatrix4d): row-major homogeneous
    // 4x4 with translation in row 3 cols 0..2 and w=1 at [3][3].
    out[0]  = static_cast<float>(t.basis[0].x);
    out[1]  = static_cast<float>(t.basis[0].y);
    out[2]  = static_cast<float>(t.basis[0].z);
    out[3]  = 0.0f;

    out[4]  = static_cast<float>(t.basis[1].x);
    out[5]  = static_cast<float>(t.basis[1].y);
    out[6]  = static_cast<float>(t.basis[1].z);
    out[7]  = 0.0f;

    out[8]  = static_cast<float>(t.basis[2].x);
    out[9]  = static_cast<float>(t.basis[2].y);
    out[10] = static_cast<float>(t.basis[2].z);
    out[11] = 0.0f;

    out[12] = static_cast<float>(t.origin.x);
    out[13] = static_cast<float>(t.origin.y);
    out[14] = static_cast<float>(t.origin.z);
    out[15] = 1.0f;
}

// Depth-first search for the first Skeleton3D descendant. nullptr if
// none. The MVP assumes one skeleton per avatar; multi-skeleton
// avatars would need a list-returning variant.
static godot::Skeleton3D* FindFirstSkeleton(godot::Node* node)
{
    if (node == nullptr) return nullptr;
    auto* as_skel = godot::Object::cast_to<godot::Skeleton3D>(node);
    if (as_skel != nullptr) return as_skel;
    int n = node->get_child_count();
    for (int i = 0; i < n; ++i) {
        if (auto* found = FindFirstSkeleton(node->get_child(i))) return found;
    }
    return nullptr;
}

static ::idtx_skeleton_t* BuildSkeleton(godot::Skeleton3D* skel)
{
    if (skel == nullptr) return nullptr;
    ::idtx_skeleton_t* out = ::idtx_skeleton_create();
    godot::String name = skel->get_name();
    idtx_skeleton_set_name(out, name.utf8().get_data());

    int n = skel->get_bone_count();
    for (int i = 0; i < n; ++i) {
        godot::String bone_name = skel->get_bone_name(i);
        int parent = skel->get_bone_parent(i);

        float rest[16];
        GodotTransformToFloat16(skel->get_bone_rest(i), rest);
        float bind[16];
        GodotTransformToFloat16(skel->get_bone_global_rest(i), bind);

        idtx_skeleton_add_bone(
            out,
            bone_name.utf8().get_data(),
            parent,
            rest,
            bind);
    }
    return out;
}

// Translate one MeshInstance3D surface into an idtx_mesh_t. Multi-
// surface meshes call this once per surface; the caller manages
// adding each to the avatar. `source_node` (when non-null) is queried
// for the n-gon sidecar metadata key
// "idtx:original_face_vertex_counts/<surface_index>" (PackedInt32Array)
// — when present, that override propagates through to the USD writer's
// faceVertexCounts attribute.
static ::idtx_mesh_t* BuildMeshFromSurface(
    godot::Ref<godot::Mesh> const& mesh,
    int surface_index,
    godot::String const& base_name,
    godot::Node3D* source_node = nullptr)
{
    if (mesh.is_null()) return nullptr;
    godot::Array arrays = mesh->surface_get_arrays(surface_index);
    if (arrays.size() <= godot::Mesh::ARRAY_VERTEX) return nullptr;

    godot::PackedVector3Array verts   = arrays[godot::Mesh::ARRAY_VERTEX];
    godot::PackedInt32Array   indices = arrays[godot::Mesh::ARRAY_INDEX];
    if (verts.size() == 0) return nullptr;
    // CSG bakes produce UN-INDEXED triangle soup: each triangle has
    // its own three vertex slots (no sharing between adjacent
    // faces). Synthesising an identity [0..N-1] index buffer keeps
    // the geometry but breaks downstream tris-to-quads (which
    // relies on shared edges = shared vertex indices). Weld
    // identical positions FIRST so adjacent triangles share corner
    // indices, then build the index buffer over the welded array.
    if (indices.size() == 0) {
        // Hash positions with a small grid quantum so floating-point
        // noise on perfectly-coincident corners (CSG output is
        // exact-coincident, but rounding can introduce ~ULP drift)
        // doesn't defeat the weld.
        constexpr float kWeldQuantum = 1e-6f;
        struct Key { int32_t x, y, z; };
        struct KeyHash {
            size_t operator()(Key const& k) const noexcept {
                size_t h = std::hash<int32_t>()(k.x);
                h ^= std::hash<int32_t>()(k.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
                h ^= std::hash<int32_t>()(k.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
                return h;
            }
        };
        struct KeyEq {
            bool operator()(Key const& a, Key const& b) const noexcept {
                return a.x == b.x && a.y == b.y && a.z == b.z;
            }
        };
        std::unordered_map<Key, int32_t, KeyHash, KeyEq> pos_to_new;
        pos_to_new.reserve(verts.size());
        godot::PackedVector3Array welded_verts;
        std::vector<int32_t> remap(verts.size());
        for (int i = 0; i < verts.size(); ++i) {
            godot::Vector3 p = verts[i];
            Key k{ static_cast<int32_t>(std::lround(p.x / kWeldQuantum)),
                   static_cast<int32_t>(std::lround(p.y / kWeldQuantum)),
                   static_cast<int32_t>(std::lround(p.z / kWeldQuantum)) };
            auto it = pos_to_new.find(k);
            if (it == pos_to_new.end()) {
                int32_t new_idx = static_cast<int32_t>(welded_verts.size());
                welded_verts.push_back(p);
                pos_to_new.emplace(k, new_idx);
                remap[i] = new_idx;
            } else {
                remap[i] = it->second;
            }
        }
        // Identity index buffer over the ORIGINAL ordering, then
        // remapped through the weld table — preserves face ordering.
        indices.resize(verts.size());
        for (int i = 0; i < verts.size(); ++i) indices[i] = remap[i];
        // Replace verts + drop the per-vertex normal/uv arrays
        // (their interleave matches the old vertex ordering; on the
        // un-indexed → indexed transition they'd point at wrong
        // post-weld vertices). The USD writer doesn't currently
        // emit normals/uvs for CSG-derived meshes — see follow-up.
        verts = welded_verts;
        // Strip the conflicting per-vertex attribute arrays so the
        // logic below doesn't try to read them with mismatched
        // counts.
        if (arrays.size() > godot::Mesh::ARRAY_NORMAL) {
            arrays[godot::Mesh::ARRAY_NORMAL] = godot::Variant();
        }
        if (arrays.size() > godot::Mesh::ARRAY_TEX_UV) {
            arrays[godot::Mesh::ARRAY_TEX_UV] = godot::Variant();
        }
        if (arrays.size() > godot::Mesh::ARRAY_COLOR) {
            arrays[godot::Mesh::ARRAY_COLOR] = godot::Variant();
        }
    }

    ::idtx_mesh_t* out = ::idtx_mesh_create();
    godot::String name = base_name + godot::String("_") + godot::String::num_int64(surface_index);
    idtx_mesh_set_name(out, name.utf8().get_data());

    int vc = verts.size();
    std::vector<float> positions(static_cast<size_t>(vc) * 3);
    for (int i = 0; i < vc; ++i) {
        godot::Vector3 v = verts[i];
        positions[i * 3 + 0] = v.x;
        positions[i * 3 + 1] = v.y;
        positions[i * 3 + 2] = v.z;
    }

    std::vector<float> normals;
    if (arrays.size() > godot::Mesh::ARRAY_NORMAL) {
        godot::PackedVector3Array nrm = arrays[godot::Mesh::ARRAY_NORMAL];
        if (nrm.size() == vc) {
            normals.resize(static_cast<size_t>(vc) * 3);
            for (int i = 0; i < vc; ++i) {
                godot::Vector3 n = nrm[i];
                normals[i * 3 + 0] = n.x;
                normals[i * 3 + 1] = n.y;
                normals[i * 3 + 2] = n.z;
            }
        }
    }

    std::vector<float> uvs;
    if (arrays.size() > godot::Mesh::ARRAY_TEX_UV) {
        godot::PackedVector2Array uv = arrays[godot::Mesh::ARRAY_TEX_UV];
        if (uv.size() == vc) {
            uvs.resize(static_cast<size_t>(vc) * 2);
            for (int i = 0; i < vc; ++i) {
                godot::Vector2 u = uv[i];
                uvs[i * 2 + 0] = u.x;
                uvs[i * 2 + 1] = u.y;
            }
        }
    }

    std::vector<float> colors;
    if (arrays.size() > godot::Mesh::ARRAY_COLOR) {
        godot::PackedColorArray cs = arrays[godot::Mesh::ARRAY_COLOR];
        if (cs.size() == vc) {
            colors.resize(static_cast<size_t>(vc) * 4);
            for (int i = 0; i < vc; ++i) {
                godot::Color c = cs[i];
                colors[i * 4 + 0] = c.r;
                colors[i * 4 + 1] = c.g;
                colors[i * 4 + 2] = c.b;
                colors[i * 4 + 3] = c.a;
            }
        }
    }

    idtx_mesh_set_vertices(
        out, vc,
        positions.data(),
        normals.empty() ? nullptr : normals.data(),
        uvs.empty()     ? nullptr : uvs.data(),
        colors.empty()  ? nullptr : colors.data());

    std::vector<int32_t> idx_buf(indices.size());
    for (int i = 0; i < indices.size(); ++i) idx_buf[i] = indices[i];
    idtx_mesh_set_indices(out, indices.size(), idx_buf.data());

    // n-gon sidecar lookup. The importer (CHI-251) stashes the
    // original USD faceVertexCounts on the MeshInstance3D's metadata
    // under "idtx:original_face_vertex_counts/<surface>" when the
    // source mesh wasn't pure triangles. If present and the sum
    // matches our index count, propagate to the idtx_mesh so the
    // USD writer re-emits the n-gons instead of triangulating.
    if (source_node != nullptr) {
        godot::String key = godot::String("idtx:original_face_vertex_counts/")
            + godot::String::num_int64(surface_index);
        if (source_node->has_meta(key)) {
            godot::PackedInt32Array fvc = source_node->get_meta(key);
            if (fvc.size() > 0) {
                int64_t sum = 0;
                for (int i = 0; i < fvc.size(); ++i) sum += fvc[i];
                if (sum == indices.size()) {
                    std::vector<int32_t> fvc_buf(fvc.size());
                    for (int i = 0; i < fvc.size(); ++i) fvc_buf[i] = fvc[i];
                    idtx_mesh_set_face_vertex_counts(out, fvc.size(), fvc_buf.data());
                }
            }
        }
    }

    // Skinning — Godot stores 4 bones/vertex by default. Both arrays
    // must be present and length-matched to set skinning data.
    if (arrays.size() > godot::Mesh::ARRAY_WEIGHTS) {
        godot::PackedInt32Array   bi = arrays[godot::Mesh::ARRAY_BONES];
        godot::PackedFloat32Array bw = arrays[godot::Mesh::ARRAY_WEIGHTS];
        if (bi.size() == bw.size() && bi.size() > 0 && (bi.size() % vc) == 0) {
            int bpv = bi.size() / vc;
            std::vector<int32_t> ibuf(bi.size());
            std::vector<float>   wbuf(bw.size());
            for (int i = 0; i < bi.size(); ++i) ibuf[i] = bi[i];
            for (int i = 0; i < bw.size(); ++i) wbuf[i] = bw[i];
            idtx_mesh_set_skinning(out, bpv, ibuf.data(), wbuf.data());
        }
    }

    return out;
}

// MToon 0.x detection on a ShaderMaterial. godot-vrm 0.x imports
// produce a ShaderMaterial bound to its in-house MToon shader, whose
// uniforms follow the original UniVRM MToon 0.x naming convention
// (_Color / _MainTex / _ShadeColor / _RimColor / _OutlineWidth / etc).
// We detect via the presence of any one of those uniforms.
static bool LooksLikeMToon0(godot::ShaderMaterial* sm)
{
    if (sm == nullptr) return false;
    // Probe via Variant API — get_shader_parameter returns null
    // Variant for unknown uniforms.
    static const char* const probes[] = {
        "_ShadeColor", "_RimColor", "_OutlineWidth", "_ShadeShift", "_ShadeToony"
    };
    for (auto const* k : probes) {
        if (sm->get_shader_parameter(k).get_type() != godot::Variant::NIL) return true;
    }
    return false;
}

// Read MToon 0.x uniforms off a ShaderMaterial and populate the
// MToon 1.0 fields on the idtx_material handle. Uniform name map
// matches the UniVRM 0.x → 1.0 migration tool's; godot-vrm preserves
// the same names verbatim. Unmapped uniforms are silently ignored
// rather than emit warnings — partial-MToon avatars still round-trip.
static void StampMToonFromShader(godot::ShaderMaterial* sm, ::idtx_material_t* out)
{
    if (sm == nullptr || out == nullptr) return;

    auto var = [&](const char* k) { return sm->get_shader_parameter(k); };
    auto has = [&](const char* k) { return var(k).get_type() != godot::Variant::NIL; };

    // _ShadeColor (Color)
    if (has("_ShadeColor")) {
        godot::Color c = var("_ShadeColor");
        idtx_material_set_mtoon_shade_color(out, c.r, c.g, c.b);
    }
    // _RimColor (Color) — MToon 1.0 parametricRimColorFactor (RGB only)
    if (has("_RimColor")) {
        godot::Color c = var("_RimColor");
        idtx_material_set_mtoon_rim_color(out, c.r, c.g, c.b);
    }
    // _OutlineWidth (float) — MToon 1.0 outlineWidthFactor (meters)
    if (has("_OutlineWidth")) {
        idtx_material_set_mtoon_outline_width(out, (float)(double)var("_OutlineWidth"));
    }
    // PBR fallbacks come from _Color (baseColor) since BuildMaterial's
    // StandardMaterial3D path didn't run for ShaderMaterials.
    if (has("_Color")) {
        godot::Color c = var("_Color");
        idtx_material_set_base_color(out, c.r, c.g, c.b, c.a);
    }
}

// Translate a Godot Material into an idtx_material_t. Handles
// StandardMaterial3D (PBR baseline) and ShaderMaterial-backed MToon
// 0.x materials emitted by godot-vrm 0.x. Unknown material types
// produce a default idtx_material.
static ::idtx_material_t* BuildMaterial(godot::Ref<godot::Material> const& mat)
{
    if (mat.is_null()) return nullptr;
    ::idtx_material_t* out = ::idtx_material_create();
    godot::String name = mat->get_name();
    if (name.is_empty()) name = godot::String("Material");
    idtx_material_set_name(out, name.utf8().get_data());

    if (auto* std_mat = godot::Object::cast_to<godot::StandardMaterial3D>(mat.ptr())) {
        godot::Color albedo = std_mat->get_albedo();
        idtx_material_set_base_color(out, albedo.r, albedo.g, albedo.b, albedo.a);
        idtx_material_set_metallic(out, std_mat->get_metallic());
        idtx_material_set_roughness(out, std_mat->get_roughness());
    } else if (auto* shader_mat = godot::Object::cast_to<godot::ShaderMaterial>(mat.ptr())) {
        if (LooksLikeMToon0(shader_mat)) {
            StampMToonFromShader(shader_mat, out);
        }
        // Other ShaderMaterials fall through with idtx_material defaults
        // (white base color, metallic=0, roughness=0.5). Caller can
        // override later via the C ABI if needed.
    }
    return out;
}

// Walk node + descendants, collecting every MeshInstance3D into the
// avatar. Each surface produces one idtx_mesh_t; surface materials get
// deduplicated into the avatar's material list by pointer identity.
struct MaterialCache
{
    std::vector<godot::Material*> seen;
    std::vector<int32_t>          indices;  // parallel to seen
};

static int32_t AddOrLookupMaterial(
    ::idtx_avatar_t* avatar,
    MaterialCache& cache,
    godot::Ref<godot::Material> const& mat)
{
    if (mat.is_null()) return -1;
    godot::Material* ptr = mat.ptr();
    for (size_t i = 0; i < cache.seen.size(); ++i) {
        if (cache.seen[i] == ptr) return cache.indices[i];
    }
    int32_t idx = idtx_avatar_add_material(avatar, BuildMaterial(mat));
    cache.seen.push_back(ptr);
    cache.indices.push_back(idx);
    return idx;
}

// Resolve a Skeleton3D bone name to its idtx_skeleton bone index.
// The walker uses bone *order in Skeleton3D* as the avatar's idtx
// bone order (see BuildSkeleton), so a direct name lookup is fine.
static int32_t BoneIndexFromName(godot::Skeleton3D* skel, godot::String const& name)
{
    if (skel == nullptr) return -1;
    int n = skel->get_bone_count();
    for (int i = 0; i < n; ++i) {
        if (skel->get_bone_name(i) == name) return i;
    }
    return -1;
}

// Pre-walk all SpringBoneCollision3D nodes anywhere under the avatar
// root, building a Node* -> idtx collider index map. We register every
// collision node up-front so a chain can reference any of them by
// NodePath, regardless of where it lives in the scene tree.
static void CollectColliders(
    godot::Node* node,
    ::idtx_avatar_t* avatar,
    godot::Skeleton3D* skel,
    std::unordered_map<godot::SpringBoneCollision3D*, int32_t>& out)
{
    if (node == nullptr) return;
    if (auto* col = godot::Object::cast_to<godot::SpringBoneCollision3D>(node)) {
        ::idtx_spring_collider_t* h = ::idtx_spring_collider_create();
        idtx_spring_collider_set_name(h, godot::String(col->get_name()).utf8().get_data());
        idtx_spring_collider_set_attached_bone(h,
            BoneIndexFromName(skel, col->get_bone_name()));

        godot::Vector3 off = col->get_position_offset();
        idtx_spring_collider_set_offset(h, off.x, off.y, off.z);

        if (auto* sph = godot::Object::cast_to<godot::SpringBoneCollisionSphere3D>(col)) {
            idtx_spring_collider_set_shape(h, IDTX_COLLIDER_SPHERE);
            idtx_spring_collider_set_radius(h, sph->get_radius());
        } else if (auto* cap = godot::Object::cast_to<godot::SpringBoneCollisionCapsule3D>(col)) {
            idtx_spring_collider_set_shape(h, IDTX_COLLIDER_CAPSULE);
            idtx_spring_collider_set_radius(h, cap->get_radius());
            // Capsule "tail" in VRMC_springBone is the line-segment end.
            // Godot's SpringBoneCollisionCapsule3D stores the segment
            // along local +Y of length `height`, so tail = offset + (0, height, 0).
            float h_v = cap->get_height();
            idtx_spring_collider_set_tail(h, off.x, off.y + h_v, off.z);
        } else {
            // Plane / other shapes: not directly representable in VRMC's
            // sphere|capsule model. Fall through to a sphere with radius 0
            // so the round-trip path still produces a slot.
            idtx_spring_collider_set_shape(h, IDTX_COLLIDER_SPHERE);
            idtx_spring_collider_set_radius(h, 0.0f);
        }
        int32_t idx = idtx_avatar_add_spring_collider(avatar, h);
        out[col] = idx;
    }
    int n = node->get_child_count();
    for (int i = 0; i < n; ++i) {
        CollectColliders(node->get_child(i), avatar, skel, out);
    }
}

// Walk a SpringBoneSimulator3D node and emit one idtx_spring_chain per
// setting. Joint bone names map to skeleton bone indices; per-setting
// dynamics (stiffness / drag / gravity) get copied via Godot's setting-
// level getters (which return the value the joints inherit when
// individual_config is off — fine for the MVP).
static void HarvestSpringSimulator(
    godot::SpringBoneSimulator3D* sim,
    ::idtx_avatar_t* avatar,
    godot::Skeleton3D* skel,
    godot::Node* root,
    std::unordered_map<godot::SpringBoneCollision3D*, int32_t> const& collider_map)
{
    if (sim == nullptr || skel == nullptr) return;
    int setting_count = sim->get_setting_count();
    for (int s = 0; s < setting_count; ++s) {
        ::idtx_spring_chain_t* chain = ::idtx_spring_chain_create();
        godot::String name = godot::String(sim->get_name()) + godot::String("_") + godot::String::num_int64(s);
        idtx_spring_chain_set_name(chain, name.utf8().get_data());

        // Joint bone indices — drop joints whose bone isn't in the skeleton.
        std::vector<int32_t> bone_idxs;
        int jc = sim->get_joint_count(s);
        for (int j = 0; j < jc; ++j) {
            godot::String bone_name = sim->get_joint_bone_name(s, j);
            int32_t bi = BoneIndexFromName(skel, bone_name);
            if (bi >= 0) bone_idxs.push_back(bi);
        }
        if (!bone_idxs.empty()) {
            idtx_spring_chain_set_joints(chain,
                static_cast<int32_t>(bone_idxs.size()), bone_idxs.data());
        }

        // Dynamics — pull from the setting-level getters. When the user
        // enabled per-joint config, joints[0]'s values land here via
        // Godot's API surface; close enough for the round trip.
        float stiff = sim->get_stiffness(s);
        float drag  = sim->get_drag(s);
        float grav  = sim->get_gravity(s);
        float radius = sim->get_radius(s);
        idtx_spring_chain_set_dynamics(chain, stiff, drag, grav, radius);

        // gravity_direction has no getter on the simulator-wide API, so
        // we sample joint[0]'s gravity_direction via the per-joint getter
        // when available — defaults to (0, -1, 0) otherwise. Godot 4.x's
        // SpringBoneSimulator3D doesn't expose a get_joint_gravity_direction,
        // so we leave the idtx default in place.
        // idtx_spring_chain_set_gravity_dir(chain, 0, -1, 0);  // (idtx default)

        // Collider references — resolve each collision NodePath to a
        // SpringBoneCollision3D pointer, then look up the registered
        // collider index.
        int cc = sim->get_collision_count(s);
        for (int c = 0; c < cc; ++c) {
            godot::NodePath path = sim->get_collision_path(s, c);
            if (path.is_empty()) continue;
            godot::Node* resolved = sim->get_node_or_null(path);
            if (resolved == nullptr && root != nullptr) {
                resolved = root->get_node_or_null(path);
            }
            if (auto* col = godot::Object::cast_to<godot::SpringBoneCollision3D>(resolved)) {
                auto it = collider_map.find(col);
                if (it != collider_map.end()) {
                    idtx_spring_chain_add_collider(chain, it->second);
                }
            }
        }
        idtx_avatar_add_spring_chain(avatar, chain);
    }
}

// Recursive descendant scan for SpringBoneSimulator3D nodes.
static void CollectSpringSimulators(
    godot::Node* node,
    ::idtx_avatar_t* avatar,
    godot::Skeleton3D* skel,
    godot::Node* root,
    std::unordered_map<godot::SpringBoneCollision3D*, int32_t> const& collider_map)
{
    if (node == nullptr) return;
    if (auto* sim = godot::Object::cast_to<godot::SpringBoneSimulator3D>(node)) {
        HarvestSpringSimulator(sim, avatar, skel, root, collider_map);
    }
    int n = node->get_child_count();
    for (int i = 0; i < n; ++i) {
        CollectSpringSimulators(node->get_child(i), avatar, skel, root, collider_map);
    }
}

// Walk for PhysicsBody3D descendants (StaticBody3D / RigidBody3D /
// CharacterBody3D / Area3D). For each, find its child CollisionShape3D
// nodes and emit one idtx_physics_collider per shape. The collider's
// transform is the world-relative local transform of the
// CollisionShape3D node.
static void CollectPhysicsColliders(
    godot::Node* node,
    ::idtx_avatar_t* avatar)
{
    if (node == nullptr) return;

    if (godot::Object::cast_to<godot::PhysicsBody3D>(node) != nullptr
        || node->get_class() == godot::String("Area3D")) {
        // For each CollisionShape3D child, emit a collider.
        int cn = node->get_child_count();
        for (int i = 0; i < cn; ++i) {
            auto* cs = godot::Object::cast_to<godot::CollisionShape3D>(node->get_child(i));
            if (cs == nullptr) continue;
            godot::Ref<godot::Shape3D> shape = cs->get_shape();
            if (shape.is_null()) continue;

            ::idtx_physics_collider_t* h = ::idtx_physics_collider_create();
            idtx_physics_collider_set_name(h, godot::String(cs->get_name()).utf8().get_data());
            float tm[16];
            GodotTransformToFloat16(cs->get_transform(), tm);
            idtx_physics_collider_set_transform(h, tm);

            // The V-Sekai fork ships a TaperedCapsuleShape3D /
            // TaperedCylinderShape3D (v-sekai-multiplayer-fabric/godot
            // @6d88ebde). We dispatch via class name since they're
            // module-provided and not in godot-cpp's generated bindings.
            godot::String klass = shape->get_class();
            if (klass == godot::String("TaperedCapsuleShape3D")) {
                float top = static_cast<float>(static_cast<double>(shape->call("get_top_radius")));
                float bot = static_cast<float>(static_cast<double>(shape->call("get_bottom_radius")));
                float mid = static_cast<float>(static_cast<double>(shape->call("get_mid_height")));
                idtx_physics_collider_set_tapered_capsule(h, top, bot, mid);
            } else if (klass == godot::String("TaperedCylinderShape3D")) {
                float top = static_cast<float>(static_cast<double>(shape->call("get_top_radius")));
                float bot = static_cast<float>(static_cast<double>(shape->call("get_bottom_radius")));
                float hgt = static_cast<float>(static_cast<double>(shape->call("get_height")));
                idtx_physics_collider_set_tapered_cylinder(h, top, bot, hgt);
            } else if (auto* sph = godot::Object::cast_to<godot::SphereShape3D>(shape.ptr())) {
                idtx_physics_collider_set_sphere(h, sph->get_radius());
            } else if (auto* box = godot::Object::cast_to<godot::BoxShape3D>(shape.ptr())) {
                godot::Vector3 sz = box->get_size();
                // BoxShape3D's `size` is full extents; idtx stores half-extents.
                idtx_physics_collider_set_box(h, sz.x * 0.5f, sz.y * 0.5f, sz.z * 0.5f);
            } else if (auto* cap = godot::Object::cast_to<godot::CapsuleShape3D>(shape.ptr())) {
                idtx_physics_collider_set_capsule(h, cap->get_radius(), cap->get_height());
            } else if (auto* cyl = godot::Object::cast_to<godot::CylinderShape3D>(shape.ptr())) {
                idtx_physics_collider_set_cylinder(h, cyl->get_radius(), cyl->get_height());
            } else {
                // Unsupported shape (concave mesh, heightmap, etc.) —
                // free the handle and skip rather than emit garbage.
                idtx_physics_collider_destroy(h);
                continue;
            }
            idtx_avatar_add_physics_collider(avatar, h);
        }
    }

    int n = node->get_child_count();
    for (int i = 0; i < n; ++i) {
        CollectPhysicsColliders(node->get_child(i), avatar);
    }
}

static void CollectMeshes(
    godot::Node* node,
    ::idtx_avatar_t* avatar,
    MaterialCache& cache)
{
    if (node == nullptr) return;
    if (auto* mi = godot::Object::cast_to<godot::MeshInstance3D>(node)) {
        godot::Ref<godot::Mesh> mesh = mi->get_mesh();
        if (mesh.is_valid()) {
            int surface_count = mesh->get_surface_count();
            godot::String base = mi->get_name();
            for (int s = 0; s < surface_count; ++s) {
                ::idtx_mesh_t* m = BuildMeshFromSurface(mesh, s, base, mi);
                if (m == nullptr) continue;
                godot::Ref<godot::Material> surf_mat = mi->get_active_material(s);
                int32_t mat_idx = AddOrLookupMaterial(avatar, cache, surf_mat);
                idtx_avatar_add_mesh(avatar, m, mat_idx);
            }
        }
    } else if (auto* csg = godot::Object::cast_to<godot::CSGShape3D>(node)) {
        // CSG: only the TOPMOST CSGShape3D in a CSG tree owns the
        // baked result. Mirror Godot's glTF exporter
        // (modules/gltf/gltf_document.cpp _convert_csg_shape_to_gltf):
        // call update_shape() to force the bake, then read the
        // ArrayMesh at index 1 of get_meshes() (index 0 is the
        // Transform3D). Children of a non-root CSG node have already
        // been folded into the root's baked output, so the walker
        // skips them.
        if (csg->is_root_shape()) {
            // `update_shape()` only queues a deferred bake — the
            // actual ArrayMesh isn't ready until a subsequent main-
            // loop tick, which doesn't happen inside a headless
            // single-shot --script invocation. `bake_static_mesh()`
            // does the same bake synchronously and returns the
            // ArrayMesh in-line. Same data, just blocks the caller
            // until the geometry is ready.
            // CSG bakes are normally deferred via call_deferred, which
            // never fires in a headless single-shot --script run. The
            // bound name in ClassDB is `_update_shape` (underscore
            // prefix — convention for "internal" methods that godot-
            // cpp's binding generator omits from typed headers).
            // Calling via Object::call dispatches the bake
            // synchronously so the root_mesh is ready before we read
            // it back via bake_static_mesh().
            csg->call("_update_shape");
            godot::Ref<godot::Mesh> baked = csg->bake_static_mesh();
            if (baked.is_valid()) {
                int surface_count = baked->get_surface_count();
                godot::String base = csg->get_name();
                godot::Transform3D xform = csg->get_transform();
                for (int s = 0; s < surface_count; ++s) {
                    ::idtx_mesh_t* m = BuildMeshFromSurface(baked, s, base, csg);
                    if (m == nullptr) continue;
                    // Bake the CSG node's local transform into the
                    // vertex positions (CSG meshes are un-skinned,
                    // so positions can live in scene-local space).
                    // idtx_mesh doesn't carry a per-mesh xform yet;
                    // for skinned meshes vertex positions stay in
                    // mesh-local space because bone transforms
                    // reproject them.
                    int32_t vc = idtx_mesh_get_vertex_count(m);
                    if (vc > 0) {
                        std::vector<float> pos(static_cast<size_t>(vc) * 3);
                        idtx_mesh_get_positions(m, pos.data());
                        for (int32_t v = 0; v < vc; ++v) {
                            godot::Vector3 p(pos[v*3], pos[v*3+1], pos[v*3+2]);
                            p = xform.xform(p);
                            pos[v*3] = p.x;
                            pos[v*3+1] = p.y;
                            pos[v*3+2] = p.z;
                        }
                        std::vector<float> nrm;
                        if (idtx_mesh_has_normals(m)) {
                            nrm.resize(static_cast<size_t>(vc) * 3);
                            idtx_mesh_get_normals(m, nrm.data());
                            // Normals rotate by basis only (no
                            // translation). For non-uniform scale we
                            // would want the inverse-transpose; CSG
                            // shapes typically use uniform/no scale.
                            for (int32_t v = 0; v < vc; ++v) {
                                godot::Vector3 n(nrm[v*3], nrm[v*3+1], nrm[v*3+2]);
                                n = xform.basis.xform(n).normalized();
                                nrm[v*3] = n.x;
                                nrm[v*3+1] = n.y;
                                nrm[v*3+2] = n.z;
                            }
                        }
                        std::vector<float> uvs, cols;
                        if (idtx_mesh_has_uvs(m)) {
                            uvs.resize(static_cast<size_t>(vc) * 2);
                            idtx_mesh_get_uvs(m, uvs.data());
                        }
                        if (idtx_mesh_has_colors(m)) {
                            cols.resize(static_cast<size_t>(vc) * 4);
                            idtx_mesh_get_colors(m, cols.data());
                        }
                        idtx_mesh_set_vertices(m, vc, pos.data(),
                            nrm.empty() ? nullptr : nrm.data(),
                            uvs.empty() ? nullptr : uvs.data(),
                            cols.empty() ? nullptr : cols.data());
                    }
                    // Material override on the CSG root wins;
                    // otherwise per-surface material on the baked
                    // mesh; otherwise a default-initialised
                    // material picked up by AddOrLookupMaterial.
                    godot::Ref<godot::Material> mat = csg->get_material_override();
                    if (mat.is_null()) mat = baked->surface_get_material(s);
                    int32_t mat_idx = AddOrLookupMaterial(avatar, cache, mat);
                    idtx_avatar_add_mesh(avatar, m, mat_idx);
                }
            }
            // Don't recurse into a CSG root's children — they're
            // already baked into the root's output.
            return;
        }
        // Non-root CSG: skip; the root above us baked it.
        return;
    }
    int n = node->get_child_count();
    for (int i = 0; i < n; ++i) {
        CollectMeshes(node->get_child(i), avatar, cache);
    }
}

// Detect a VRM 0.x avatar by looking for the legacy node names that
// godot-vrm 0.x imports leave in the scene. The presence of any one of
// these is treated as a strong signal — VRM 0.0 has +Z-forward; VRM 1.0
// has −Z-forward, so we need to know which one to record (and
// eventually flip) on export.
static bool LooksLikeVrm0(godot::Node* node)
{
    if (node == nullptr) return false;
    godot::String klass = node->get_class();
    if (klass == godot::String("VRMSpringBone")
        || klass == godot::String("VRMSpringBoneColliderGroup")
        || klass == godot::String("VRMFirstPerson")) {
        return true;
    }
    if (node->has_meta(godot::StringName("vrm_version"))) {
        godot::String v = node->get_meta(godot::StringName("vrm_version"));
        if (v.begins_with("0.")) return true;
    }
    int n = node->get_child_count();
    for (int i = 0; i < n; ++i) {
        if (LooksLikeVrm0(node->get_child(i))) return true;
    }
    return false;
}

::idtx_avatar_t* BuildIdtxAvatarFromGodotScene(godot::Node3D* root)
{
    if (root == nullptr) return nullptr;

    ::idtx_avatar_t* avatar = ::idtx_avatar_create();
    godot::String name = root->get_name();
    idtx_avatar_set_name(avatar, name.utf8().get_data());

    bool is_vrm0 = LooksLikeVrm0(root);
    if (is_vrm0) {
        idtx_avatar_set_source_vrm_version(avatar, "0.x");
    }

    float root_xform[16];
    GodotTransformToFloat16(root->get_transform(), root_xform);

    // VRM 0.0 → 1.0 orientation flip: rotate 180° about Y so the avatar
    // ends up −Z-forward (VRM 1.0 convention) instead of +Z-forward.
    // The flip is `Rotate_Y_180 * root_xform`, applied in-place.
    if (is_vrm0) {
        // Y-180 rotation matrix (row-major, identity translation):
        //   ⎡-1  0  0  0⎤
        //   ⎢ 0  1  0  0⎥
        //   ⎢ 0  0 -1  0⎥
        //   ⎣ 0  0  0  1⎦
        float flipped[16];
        for (int r = 0; r < 4; ++r) {
            // row 0 and row 2 of identity * (-1); row 1 unchanged.
            float sign = (r == 0 || r == 2) ? -1.0f : 1.0f;
            for (int c = 0; c < 4; ++c) {
                flipped[r * 4 + c] = sign * root_xform[r * 4 + c];
            }
        }
        idtx_avatar_set_root_transform(avatar, flipped);
    } else {
        idtx_avatar_set_root_transform(avatar, root_xform);
    }

    godot::Skeleton3D* found_skel = FindFirstSkeleton(root);
    if (found_skel != nullptr) {
        idtx_avatar_set_skeleton(avatar, BuildSkeleton(found_skel));
    }

    MaterialCache cache;
    CollectMeshes(root, avatar, cache);

    // Physics colliders — independent of spring bones / skeleton.
    CollectPhysicsColliders(root, avatar);

    // Spring bones — register colliders first so chain.collider_path
    // references resolve to indices; then walk simulators.
    if (found_skel != nullptr) {
        std::unordered_map<godot::SpringBoneCollision3D*, int32_t> collider_map;
        CollectColliders(root, avatar, found_skel, collider_map);
        CollectSpringSimulators(root, avatar, found_skel, root, collider_map);
    }

    return avatar;
}

}  // namespace idtxflow::exporter
