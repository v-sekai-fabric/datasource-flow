// Copyright 2026 V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// Tris-to-quads CPU reconstruction (CHI-253).
//
// Greedy mutual-best matching over the triangle adjacency graph:
//   1. Build edge -> {triangles} map.
//   2. For each shared edge, compute weight (planarity quality).
//   3. For each triangle, find best (highest-weight) neighbour.
//   4. Pair only forms if both triangles picked each other.
//
// Same algorithm as the GPU Slang shader (lean/Fabric/Mesh/
// TrisToQuadsGPU.lean) so CPU and GPU paths produce identical
// output for the same input. Suboptimal vs LEMON's
// MaxWeightedMatching (Edmonds' blossom) — but parallelizable,
// allocation-free in the hot loop, and good enough for the
// dominant authoring case where most triangles have one obvious
// quad partner.

#include "idtx_core/idtx_core.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace {

struct Vec3 {
    float x, y, z;
    Vec3 operator-(Vec3 const& o) const { return {x-o.x, y-o.y, z-o.z}; }
    static Vec3 cross(Vec3 const& a, Vec3 const& b) {
        return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x};
    }
    static float dot(Vec3 const& a, Vec3 const& b) {
        return a.x*b.x + a.y*b.y + a.z*b.z;
    }
    float length() const { return std::sqrt(x*x + y*y + z*z); }
};

struct TriEdge {
    int a, b;  // canonical: a < b
    TriEdge(int x, int y) : a(x < y ? x : y), b(x < y ? y : x) {}
    bool operator==(TriEdge const& o) const { return a == o.a && b == o.b; }
};

struct TriEdgeHash {
    size_t operator()(TriEdge const& e) const noexcept {
        return std::hash<long long>()(
            (static_cast<long long>(e.a) << 32) |
             static_cast<unsigned int>(e.b));
    }
};

Vec3 tri_normal(Vec3 const& p0, Vec3 const& p1, Vec3 const& p2) {
    Vec3 n = Vec3::cross(p1 - p0, p2 - p0);
    float l = n.length();
    if (l > 1e-12f) return {n.x/l, n.y/l, n.z/l};
    return {0, 0, 1};
}

// Returns the third vertex of triangle (v0,v1,v2) given the two
// endpoints of its shared edge; -1 if the edge isn't in it.
int opposite_vertex(int v0, int v1, int v2, int sa, int sb) {
    if (v0 != sa && v0 != sb) return v0;
    if (v1 != sa && v1 != sb) return v1;
    if (v2 != sa && v2 != sb) return v2;
    return -1;
}

}  // namespace

extern "C" int32_t idtx_mesh_reconstruct_quads(
    idtx_mesh_t* mesh,
    float planarity_max_degrees)
{
    if (mesh == nullptr) return -1;

    int32_t idx_count = idtx_mesh_get_index_count(mesh);
    int32_t vert_count = idtx_mesh_get_vertex_count(mesh);
    if (idx_count <= 0 || vert_count <= 0) return 0;

    // Only operates on tri-soup meshes. If face_vertex_counts is
    // already set (mesh already has n-gons), bail with 0 quads.
    int32_t fvc_count = idtx_mesh_get_face_vertex_count_count(mesh);
    if (fvc_count > 0) return 0;
    if (idx_count % 3 != 0) return -1;

    int tri_count = idx_count / 3;
    if (tri_count <= 1) return 0;

    std::vector<int32_t> indices(idx_count);
    idtx_mesh_get_indices(mesh, indices.data());

    std::vector<float> positions_flat(vert_count * 3);
    idtx_mesh_get_positions(mesh, positions_flat.data());
    std::vector<Vec3> positions(vert_count);
    for (int i = 0; i < vert_count; ++i) {
        positions[i] = { positions_flat[3*i + 0],
                         positions_flat[3*i + 1],
                         positions_flat[3*i + 2] };
    }

    // Build edge -> {triangles} map.
    std::unordered_map<TriEdge, std::vector<int>, TriEdgeHash> edge_to_tris;
    edge_to_tris.reserve(tri_count * 3);
    for (int t = 0; t < tri_count; ++t) {
        int i0 = indices[3*t + 0];
        int i1 = indices[3*t + 1];
        int i2 = indices[3*t + 2];
        edge_to_tris[TriEdge(i0, i1)].push_back(t);
        edge_to_tris[TriEdge(i1, i2)].push_back(t);
        edge_to_tris[TriEdge(i2, i0)].push_back(t);
    }

    // Per-triangle "best neighbour" + the shared edge that connects.
    // best_weight starts at -1 so the first valid candidate always wins.
    std::vector<int>   best_neighbour(tri_count, -1);
    std::vector<float> best_weight   (tri_count, -1.0f);
    std::vector<TriEdge> best_edge   (tri_count, TriEdge(-1, -1));

    float planarity_cos_threshold = std::cos(
        planarity_max_degrees * 3.14159265358979f / 180.0f);

    for (auto const& kv : edge_to_tris) {
        if (kv.second.size() != 2) continue;  // boundary or non-manifold
        int ta = kv.second[0];
        int tb = kv.second[1];

        Vec3 const& na_p0 = positions[indices[3*ta + 0]];
        Vec3 const& na_p1 = positions[indices[3*ta + 1]];
        Vec3 const& na_p2 = positions[indices[3*ta + 2]];
        Vec3 const& nb_p0 = positions[indices[3*tb + 0]];
        Vec3 const& nb_p1 = positions[indices[3*tb + 1]];
        Vec3 const& nb_p2 = positions[indices[3*tb + 2]];

        Vec3 na = tri_normal(na_p0, na_p1, na_p2);
        Vec3 nb = tri_normal(nb_p0, nb_p1, nb_p2);
        float cos_nn = Vec3::dot(na, nb);
        if (cos_nn < planarity_cos_threshold) continue;

        // Weight = planarity quality, in [-1, 1] shifted to [0, 2].
        float w = cos_nn + 1.0f;

        if (w > best_weight[ta]) {
            best_weight[ta] = w;
            best_neighbour[ta] = tb;
            best_edge[ta] = kv.first;
        }
        if (w > best_weight[tb]) {
            best_weight[tb] = w;
            best_neighbour[tb] = ta;
            best_edge[tb] = kv.first;
        }
    }

    // Confirm mutual best.
    std::vector<bool> consumed(tri_count, false);
    std::vector<int32_t> new_indices;
    std::vector<int32_t> new_fvc;
    new_indices.reserve(idx_count);
    new_fvc.reserve(tri_count);
    int32_t quad_count = 0;

    for (int t = 0; t < tri_count; ++t) {
        if (consumed[t]) continue;
        int n = best_neighbour[t];
        if (n < 0 || consumed[n]) continue;
        if (best_neighbour[n] != t) continue;  // not mutual

        // Form quad from triangles t, n with shared edge best_edge[t].
        int sa = best_edge[t].a;
        int sb = best_edge[t].b;
        int t_i0 = indices[3*t + 0];
        int t_i1 = indices[3*t + 1];
        int t_i2 = indices[3*t + 2];
        int n_i0 = indices[3*n + 0];
        int n_i1 = indices[3*n + 1];
        int n_i2 = indices[3*n + 2];
        int opp_t = opposite_vertex(t_i0, t_i1, t_i2, sa, sb);
        int opp_n = opposite_vertex(n_i0, n_i1, n_i2, sa, sb);
        if (opp_t < 0 || opp_n < 0) continue;

        // Winding: opp_t, sa, opp_n, sb.
        new_indices.push_back(opp_t);
        new_indices.push_back(sa);
        new_indices.push_back(opp_n);
        new_indices.push_back(sb);
        new_fvc.push_back(4);
        consumed[t] = true;
        consumed[n] = true;
        ++quad_count;
    }

    // Unmatched triangles stay as triangles.
    for (int t = 0; t < tri_count; ++t) {
        if (consumed[t]) continue;
        new_indices.push_back(indices[3*t + 0]);
        new_indices.push_back(indices[3*t + 1]);
        new_indices.push_back(indices[3*t + 2]);
        new_fvc.push_back(3);
    }

    idtx_mesh_set_indices(mesh,
        static_cast<int32_t>(new_indices.size()),
        new_indices.data());
    idtx_mesh_set_face_vertex_counts(mesh,
        static_cast<int32_t>(new_fvc.size()),
        new_fvc.data());

    return quad_count;
}
