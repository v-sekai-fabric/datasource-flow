/**************************************************************************/
/*  IdtxSceneGodotBuilder.cpp                                            */
/**************************************************************************/
/* Copyright 2026 The openusd-fabric authors / V-Sekai contributors.      */
/* SPDX-License-Identifier: Apache-2.0 OR MPL-2.0                         */
/**************************************************************************/

#include "IdtxSceneGodotBuilder.h"

#include <string>

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/skin.hpp>
#include <godot_cpp/classes/animation.hpp>
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

// Build the Godot material for a node — the single material path for the whole
// builder. Prefers the node's bound idtx_material (UsdPreviewSurface base color /
// metallic / roughness, converted in-core); when the node has none (primitives,
// or a mesh with no bound material), falls back to its display color (constant
// interp -> albedo, else vertex-color). Texture maps from the material's image
// paths are a follow-up (needs usdz asset extraction).
Ref<StandardMaterial3D> build_material(idtx_scene_t* scene, idtx_node_t* node) {
    Ref<StandardMaterial3D> mat;
    mat.instantiate();

    const int32_t mi = idtx_node_get_material_index(node);
    const idtx_material_t* m = (mi >= 0) ? idtx_scene_get_material(scene, mi) : nullptr;
    if (m) {
        float rgba[4];
        idtx_material_get_base_color(m, rgba);
        const Color albedo(rgba[0], rgba[1], rgba[2], rgba[3]);
        mat->set_albedo(albedo);
        mat->set_metallic(idtx_material_get_metallic(m));
        mat->set_roughness(idtx_material_get_roughness(m));
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
    for (int32_t i = 0; i < ic; ++i) tris[i] = idx[i];

    Array arrays; arrays.resize(Mesh::ARRAY_MAX);
    arrays[Mesh::ARRAY_VERTEX] = verts;
    arrays[Mesh::ARRAY_INDEX] = tris;

    if (idtx_mesh_has_normals(mesh)) {
        std::vector<float> n(vc * 3); idtx_mesh_get_normals(mesh, n.data());
        PackedVector3Array nrm; nrm.resize(vc);
        for (int32_t i = 0; i < vc; ++i) nrm[i] = Vector3(n[i*3], n[i*3+1], n[i*3+2]);
        arrays[Mesh::ARRAY_NORMAL] = nrm;
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

    out->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, TypedArray<Array>(), Dictionary(), flags);
    return out;
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
            auto* n = memnew(UsdMeshInstanceNode3D); n->set_mesh(mesh); n->set_transform(xform); return n;
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
                const int32_t tc = idtx_anim_get_track_count(a);
                for (int32_t t = 0; t < tc; ++t) {
                    const idtx_anim_track_type_t tt = idtx_anim_track_get_type(a, t);
                    Animation::TrackType gt = Animation::TYPE_POSITION_3D;
                    if (tt == IDTX_ANIM_TRACK_ROTATION) {
                        gt = Animation::TYPE_ROTATION_3D;
                    } else if (tt == IDTX_ANIM_TRACK_SCALE) {
                        gt = Animation::TYPE_SCALE_3D;
                    }
                    const int32_t ti = anim->add_track(gt);
                    anim->track_set_path(ti, NodePath(String(idtx_anim_track_get_bone_name(a, t))));
                    const int32_t kc = idtx_anim_track_get_key_count(a, t);
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
            if (idtx_mesh_t* sm = idtx_node_get_skinned_mesh(node)) {
                Ref<ArrayMesh> mesh = build_array_mesh(sm);
                if (mesh->get_surface_count() > 0) {
                    mesh->surface_set_material(0, build_material(scene, node));
                    auto* mi = memnew(UsdMeshInstanceNode3D);
                    mi->set_mesh(mesh);
                    mi->set_skeleton(sk);
                    mi->set_name("Skin");
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

    // Coordinate fix-up at the root (ConvertStagePostProcess, now host-side):
    // swing up-axis to Godot's Y-up + scale by metersPerUnit.
    const idtx_axis_t up = idtx_scene_get_up_axis(sc);
    const float mpu = (float)idtx_scene_get_meters_per_unit(sc);
    for (Node3D* root : roots) {
        if (up == IDTX_AXIS_Z)      root->rotate_x(Math::deg_to_rad(-90.0));
        else if (up == IDTX_AXIS_X) root->rotate_z(Math::deg_to_rad(90.0));
        root->set_scale(root->get_scale() * mpu);
    }
    return roots;
}

}  // namespace idtxflow
