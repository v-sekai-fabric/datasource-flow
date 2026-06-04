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
#include <godot_cpp/core/math.hpp>

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

// Default material from the node's display color (constant interp -> albedo,
// else vertex-color), mirroring the old ConvertXxx default-material path.
Ref<StandardMaterial3D> default_material(idtx_node_t* node) {
    Ref<StandardMaterial3D> mat;
    mat.instantiate();
    const int32_t cc = idtx_node_get_display_color_count(node);
    if (cc > 0 && idtx_node_get_color_interpolation(node) == IDTX_COLOR_INTERP_CONSTANT) {
        std::vector<float> rgba(cc * 4);
        idtx_node_get_display_colors(node, rgba.data());
        Color albedo(rgba[0], rgba[1], rgba[2], rgba[3]);
        mat->set_albedo(albedo);
        if (albedo.a < 1.0f) mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
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
    out->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
    return out;
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
            box->set_material(default_material(node));
            auto* n = memnew(UsdMeshInstanceNode3D); n->set_mesh(box); n->set_transform(xform); return n;
        }
        case IDTX_NODE_CYLINDER: {
            Ref<CylinderMesh> cyl; cyl.instantiate();
            double r, h; idtx_axis_t a; idtx_node_get_cylinder(node, &r, &h, &a);
            cyl->set_top_radius(r); cyl->set_bottom_radius(r); cyl->set_height(h);
            cyl->set_material(default_material(node));
            auto* n = memnew(UsdMeshInstanceNode3D); n->set_mesh(cyl); n->set_transform(xform); return n;
        }
        case IDTX_NODE_CONE: {
            Ref<CylinderMesh> cyl; cyl.instantiate();
            double r, h; idtx_axis_t a; idtx_node_get_cone(node, &r, &h, &a);
            cyl->set_top_radius(0.0); cyl->set_bottom_radius(r); cyl->set_height(h);
            cyl->set_material(default_material(node));
            auto* n = memnew(UsdMeshInstanceNode3D); n->set_mesh(cyl); n->set_transform(xform); return n;
        }
        case IDTX_NODE_SPHERE: {
            Ref<SphereMesh> sph; sph.instantiate();
            double r = idtx_node_get_sphere_radius(node); sph->set_radius(r); sph->set_height(r * 2.0);
            sph->set_material(default_material(node));
            auto* n = memnew(UsdMeshInstanceNode3D); n->set_mesh(sph); n->set_transform(xform); return n;
        }
        case IDTX_NODE_MESH: {
            Ref<ArrayMesh> mesh = build_array_mesh(idtx_node_get_mesh(node));
            if (mesh->get_surface_count() > 0) mesh->surface_set_material(0, default_material(node));
            auto* n = memnew(UsdMeshInstanceNode3D); n->set_mesh(mesh); n->set_transform(xform); return n;
        }
        case IDTX_NODE_SKELETON: {
            auto* sk = memnew(UsdSkeletonNode3D);
            if (idtx_skeleton_t* skel = idtx_node_get_skeleton(node)) {
                const int32_t bc = idtx_skeleton_get_bone_count(skel);
                for (int32_t b = 0; b < bc; ++b) {
                    int32_t bi = sk->add_bone(String(idtx_skeleton_get_bone_name(skel, b)));
                    sk->set_bone_parent(bi, idtx_skeleton_get_bone_parent(skel, b));
                    float rest[16]; idtx_skeleton_get_bone_rest(skel, b, rest);
                    sk->set_bone_rest(bi, to_transform(rest));
                }
                sk->reset_bone_poses();
            }
            sk->set_transform(xform);
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
        n->set_name(String(idtx_node_get_name(node)));
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
        if (p >= 0 && p < count && built[p]) built[p]->add_child(built[i]);
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
