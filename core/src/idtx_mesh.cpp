// Copyright 2026 V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

// IDTX_CORE_BUILDING_DLL is set by scons/idtxcore.py CPPDEFINES.
#include "idtx_core/idtx_core.h"

#include <cstring>
#include <string>
#include <vector>

struct idtx_blendshape
{
    std::string        name;
    float              weight = 0.0f;     // current/default weight; typically 0..1, not clamped
    std::vector<float> position_deltas;  // vertex_count*3
    std::vector<float> normal_deltas;     // vertex_count*3, may be empty
};

struct idtx_mesh
{
    std::string name;
    int32_t vertex_count = 0;
    int32_t index_count  = 0;
    int32_t bones_per_vertex = 0;
    std::vector<float>   positions;
    std::vector<float>   normals;
    std::vector<float>   uvs;
    std::vector<float>   colors;
    std::vector<int32_t> indices;
    std::vector<int32_t> bone_indices;
    std::vector<float>   weights;
    std::vector<int32_t> face_vertex_counts;  // empty = triangle list
    std::vector<idtx_blendshape> blendshapes;
};

static void copy_floats(std::vector<float>& dst, const float* src, size_t count)
{
    dst.assign(src, src + count);
}

static void copy_ints(std::vector<int32_t>& dst, const int32_t* src, size_t count)
{
    dst.assign(src, src + count);
}

extern "C" IDTX_CORE_API idtx_mesh_t* idtx_mesh_create(void)
{
    return new idtx_mesh();
}

extern "C" IDTX_CORE_API void idtx_mesh_destroy(idtx_mesh_t* mesh)
{
    delete mesh;
}

extern "C" IDTX_CORE_API void idtx_mesh_set_name(idtx_mesh_t* mesh, const char* name)
{
    if (mesh == nullptr) return;
    mesh->name = (name != nullptr) ? name : "";
}

extern "C" IDTX_CORE_API const char* idtx_mesh_get_name(const idtx_mesh_t* mesh)
{
    return (mesh != nullptr) ? mesh->name.c_str() : "";
}

extern "C" IDTX_CORE_API void idtx_mesh_set_vertices(
    idtx_mesh_t* mesh,
    int32_t vertex_count,
    const float* positions,
    const float* normals,
    const float* uvs,
    const float* colors)
{
    if (mesh == nullptr || positions == nullptr || vertex_count <= 0) return;
    mesh->vertex_count = vertex_count;
    copy_floats(mesh->positions, positions, static_cast<size_t>(vertex_count) * 3);
    if (normals != nullptr) copy_floats(mesh->normals, normals, static_cast<size_t>(vertex_count) * 3);
    else mesh->normals.clear();
    if (uvs != nullptr) copy_floats(mesh->uvs, uvs, static_cast<size_t>(vertex_count) * 2);
    else mesh->uvs.clear();
    if (colors != nullptr) copy_floats(mesh->colors, colors, static_cast<size_t>(vertex_count) * 4);
    else mesh->colors.clear();
}

extern "C" IDTX_CORE_API void idtx_mesh_set_indices(
    idtx_mesh_t* mesh,
    int32_t index_count,
    const int32_t* indices)
{
    if (mesh == nullptr || indices == nullptr || index_count <= 0) return;
    mesh->index_count = index_count;
    copy_ints(mesh->indices, indices, static_cast<size_t>(index_count));
}

extern "C" IDTX_CORE_API void idtx_mesh_set_skinning(
    idtx_mesh_t* mesh,
    int32_t bones_per_vertex,
    const int32_t* bone_indices,
    const float* weights)
{
    if (mesh == nullptr || bones_per_vertex <= 0 || mesh->vertex_count <= 0) return;
    if (bone_indices == nullptr || weights == nullptr) return;
    mesh->bones_per_vertex = bones_per_vertex;
    size_t n = static_cast<size_t>(mesh->vertex_count) * static_cast<size_t>(bones_per_vertex);
    copy_ints(mesh->bone_indices, bone_indices, n);
    copy_floats(mesh->weights, weights, n);
}

extern "C" IDTX_CORE_API void idtx_mesh_add_blendshape(
    idtx_mesh_t* mesh,
    const char* name,
    float weight,
    const float* position_deltas,
    const float* normal_deltas)
{
    if (mesh == nullptr || position_deltas == nullptr || mesh->vertex_count <= 0) return;
    size_t n = static_cast<size_t>(mesh->vertex_count) * 3;
    idtx_blendshape bs;
    bs.name = (name != nullptr) ? name : "";
    bs.weight = weight;
    copy_floats(bs.position_deltas, position_deltas, n);
    if (normal_deltas != nullptr) copy_floats(bs.normal_deltas, normal_deltas, n);
    mesh->blendshapes.push_back(std::move(bs));
}

extern "C" IDTX_CORE_API float idtx_mesh_get_blendshape_weight(const idtx_mesh_t* mesh, int32_t index)
{
    if (mesh == nullptr || index < 0 || index >= static_cast<int32_t>(mesh->blendshapes.size())) return 0.0f;
    return mesh->blendshapes[static_cast<size_t>(index)].weight;
}

extern "C" IDTX_CORE_API int32_t idtx_mesh_get_blendshape_count(const idtx_mesh_t* mesh)
{
    return (mesh != nullptr) ? static_cast<int32_t>(mesh->blendshapes.size()) : 0;
}

extern "C" IDTX_CORE_API const char* idtx_mesh_get_blendshape_name(const idtx_mesh_t* mesh, int32_t index)
{
    if (mesh == nullptr || index < 0 || index >= static_cast<int32_t>(mesh->blendshapes.size())) return "";
    return mesh->blendshapes[static_cast<size_t>(index)].name.c_str();
}

extern "C" IDTX_CORE_API int32_t idtx_mesh_blendshape_has_normals(const idtx_mesh_t* mesh, int32_t index)
{
    if (mesh == nullptr || index < 0 || index >= static_cast<int32_t>(mesh->blendshapes.size())) return 0;
    return mesh->blendshapes[static_cast<size_t>(index)].normal_deltas.empty() ? 0 : 1;
}

extern "C" IDTX_CORE_API void idtx_mesh_get_blendshape_position_deltas(const idtx_mesh_t* mesh, int32_t index, float* out_deltas)
{
    if (mesh == nullptr || out_deltas == nullptr) return;
    if (index < 0 || index >= static_cast<int32_t>(mesh->blendshapes.size())) return;
    const std::vector<float>& d = mesh->blendshapes[static_cast<size_t>(index)].position_deltas;
    if (!d.empty()) std::memcpy(out_deltas, d.data(), d.size() * sizeof(float));
}

extern "C" IDTX_CORE_API void idtx_mesh_get_blendshape_normal_deltas(const idtx_mesh_t* mesh, int32_t index, float* out_deltas)
{
    if (mesh == nullptr || out_deltas == nullptr) return;
    if (index < 0 || index >= static_cast<int32_t>(mesh->blendshapes.size())) return;
    const std::vector<float>& d = mesh->blendshapes[static_cast<size_t>(index)].normal_deltas;
    if (!d.empty()) std::memcpy(out_deltas, d.data(), d.size() * sizeof(float));
}

extern "C" IDTX_CORE_API int32_t idtx_mesh_get_vertex_count(const idtx_mesh_t* mesh)
{
    return (mesh != nullptr) ? mesh->vertex_count : 0;
}

extern "C" IDTX_CORE_API int32_t idtx_mesh_get_index_count(const idtx_mesh_t* mesh)
{
    return (mesh != nullptr) ? mesh->index_count : 0;
}

extern "C" IDTX_CORE_API int32_t idtx_mesh_get_bones_per_vertex(const idtx_mesh_t* mesh)
{
    return (mesh != nullptr) ? mesh->bones_per_vertex : 0;
}

static void copy_out_floats(const std::vector<float>& src, float* dst)
{
    if (dst == nullptr || src.empty()) return;
    std::memcpy(dst, src.data(), src.size() * sizeof(float));
}

static void copy_out_ints(const std::vector<int32_t>& src, int32_t* dst)
{
    if (dst == nullptr || src.empty()) return;
    std::memcpy(dst, src.data(), src.size() * sizeof(int32_t));
}

extern "C" IDTX_CORE_API void idtx_mesh_get_positions(const idtx_mesh_t* mesh, float* out)
{ if (mesh != nullptr) copy_out_floats(mesh->positions, out); }

extern "C" IDTX_CORE_API void idtx_mesh_get_normals(const idtx_mesh_t* mesh, float* out)
{ if (mesh != nullptr) copy_out_floats(mesh->normals, out); }

extern "C" IDTX_CORE_API void idtx_mesh_get_uvs(const idtx_mesh_t* mesh, float* out)
{ if (mesh != nullptr) copy_out_floats(mesh->uvs, out); }

extern "C" IDTX_CORE_API void idtx_mesh_get_colors(const idtx_mesh_t* mesh, float* out)
{ if (mesh != nullptr) copy_out_floats(mesh->colors, out); }

extern "C" IDTX_CORE_API void idtx_mesh_get_indices(const idtx_mesh_t* mesh, int32_t* out)
{ if (mesh != nullptr) copy_out_ints(mesh->indices, out); }

extern "C" IDTX_CORE_API void idtx_mesh_get_bone_indices(const idtx_mesh_t* mesh, int32_t* out)
{ if (mesh != nullptr) copy_out_ints(mesh->bone_indices, out); }

extern "C" IDTX_CORE_API void idtx_mesh_get_weights(const idtx_mesh_t* mesh, float* out)
{ if (mesh != nullptr) copy_out_floats(mesh->weights, out); }

extern "C" IDTX_CORE_API int32_t idtx_mesh_has_normals(const idtx_mesh_t* mesh)
{ return (mesh != nullptr && !mesh->normals.empty()) ? 1 : 0; }

extern "C" IDTX_CORE_API int32_t idtx_mesh_has_uvs(const idtx_mesh_t* mesh)
{ return (mesh != nullptr && !mesh->uvs.empty()) ? 1 : 0; }

extern "C" IDTX_CORE_API int32_t idtx_mesh_has_colors(const idtx_mesh_t* mesh)
{ return (mesh != nullptr && !mesh->colors.empty()) ? 1 : 0; }

extern "C" IDTX_CORE_API void idtx_mesh_set_face_vertex_counts(
    idtx_mesh_t* mesh, int32_t count, const int32_t* counts)
{
    if (mesh == nullptr) return;
    if (count <= 0 || counts == nullptr) {
        mesh->face_vertex_counts.clear();
        return;
    }
    int64_t sum = 0;
    for (int32_t i = 0; i < count; ++i) sum += counts[i];
    if (sum != mesh->index_count) {
        // Mismatched counts — refuse silently rather than corrupt
        // the mesh on round trip. Caller should check by reading
        // back idtx_mesh_get_face_vertex_count_count().
        return;
    }
    mesh->face_vertex_counts.assign(counts, counts + count);
}

extern "C" IDTX_CORE_API int32_t idtx_mesh_get_face_vertex_count_count(const idtx_mesh_t* mesh)
{ return (mesh != nullptr) ? static_cast<int32_t>(mesh->face_vertex_counts.size()) : 0; }

extern "C" IDTX_CORE_API void idtx_mesh_get_face_vertex_counts(const idtx_mesh_t* mesh, int32_t* out)
{ if (mesh != nullptr) copy_out_ints(mesh->face_vertex_counts, out); }
