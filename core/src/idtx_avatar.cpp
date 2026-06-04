// Copyright 2026 V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

// IDTX_CORE_BUILDING_DLL is set by scons/idtxcore.py CPPDEFINES.
#include "idtx_core/idtx_core.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

struct idtx_avatar
{
    std::string name;
    std::string source_vrm_version;  // "" / "0.x" / "1.0"
    std::string source_usd_path;     // "" or the stage this avatar was imported from
    float root_transform[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };
    idtx_skeleton_t* skeleton = nullptr;
    std::vector<idtx_mesh_t*>     meshes;
    std::vector<int32_t>          mesh_material;   // parallel to meshes
    std::vector<idtx_material_t*> materials;
    std::vector<idtx_spring_chain_t*>     spring_chains;
    std::vector<idtx_spring_collider_t*>  spring_colliders;
    std::vector<idtx_physics_collider_t*> physics_colliders;
    // Decoded texture bytes keyed by the material's texture path. Carried so a
    // host can display textures even when the source is a .usdz package (whose
    // members are not plain files) -- the core resolves them at import time.
    std::vector<std::pair<std::string, std::vector<uint8_t>>> textures;
};

extern "C" IDTX_CORE_API idtx_avatar_t* idtx_avatar_create(void)
{
    return new idtx_avatar();
}

extern "C" IDTX_CORE_API void idtx_avatar_destroy(idtx_avatar_t* avatar)
{
    if (avatar == nullptr) return;
    if (avatar->skeleton != nullptr) idtx_skeleton_destroy(avatar->skeleton);
    for (auto* m : avatar->meshes)    if (m != nullptr) idtx_mesh_destroy(m);
    for (auto* mat : avatar->materials) if (mat != nullptr) idtx_material_destroy(mat);
    for (auto* sc : avatar->spring_chains)    if (sc != nullptr) idtx_spring_chain_destroy(sc);
    for (auto* col : avatar->spring_colliders)  if (col != nullptr) idtx_spring_collider_destroy(col);
    for (auto* pc : avatar->physics_colliders) if (pc != nullptr) idtx_physics_collider_destroy(pc);
    delete avatar;
}

extern "C" IDTX_CORE_API void idtx_avatar_set_name(idtx_avatar_t* avatar, const char* name)
{
    if (avatar == nullptr) return;
    avatar->name = (name != nullptr) ? name : "";
}

extern "C" IDTX_CORE_API const char* idtx_avatar_get_name(const idtx_avatar_t* avatar)
{
    return (avatar != nullptr) ? avatar->name.c_str() : "";
}

extern "C" IDTX_CORE_API void idtx_avatar_set_root_transform(idtx_avatar_t* avatar, const float matrix[16])
{
    if (avatar == nullptr || matrix == nullptr) return;
    std::memcpy(avatar->root_transform, matrix, sizeof(float) * 16);
}

extern "C" IDTX_CORE_API void idtx_avatar_set_source_vrm_version(idtx_avatar_t* avatar, const char* version)
{
    if (avatar == nullptr) return;
    avatar->source_vrm_version = (version != nullptr) ? version : "";
}

extern "C" IDTX_CORE_API const char* idtx_avatar_get_source_vrm_version(const idtx_avatar_t* avatar)
{
    return (avatar != nullptr) ? avatar->source_vrm_version.c_str() : "";
}

extern "C" IDTX_CORE_API void idtx_avatar_set_source_usd_path(idtx_avatar_t* avatar, const char* path)
{
    if (avatar == nullptr) return;
    avatar->source_usd_path = (path != nullptr) ? path : "";
}

extern "C" IDTX_CORE_API const char* idtx_avatar_get_source_usd_path(const idtx_avatar_t* avatar)
{
    return (avatar != nullptr) ? avatar->source_usd_path.c_str() : "";
}

extern "C" IDTX_CORE_API void idtx_avatar_get_root_transform(const idtx_avatar_t* avatar, float out_matrix[16])
{
    if (out_matrix == nullptr) return;
    if (avatar == nullptr) {
        std::memset(out_matrix, 0, sizeof(float) * 16);
        out_matrix[0] = out_matrix[5] = out_matrix[10] = out_matrix[15] = 1.0f;
        return;
    }
    std::memcpy(out_matrix, avatar->root_transform, sizeof(float) * 16);
}

extern "C" IDTX_CORE_API void idtx_avatar_set_skeleton(idtx_avatar_t* avatar, idtx_skeleton_t* skel)
{
    if (avatar == nullptr) return;
    if (avatar->skeleton != nullptr && avatar->skeleton != skel) {
        idtx_skeleton_destroy(avatar->skeleton);
    }
    avatar->skeleton = skel;
}

extern "C" IDTX_CORE_API idtx_skeleton_t* idtx_avatar_get_skeleton(const idtx_avatar_t* avatar)
{
    return (avatar != nullptr) ? avatar->skeleton : nullptr;
}

extern "C" IDTX_CORE_API int32_t idtx_avatar_add_mesh(idtx_avatar_t* avatar, idtx_mesh_t* mesh, int32_t material_index)
{
    if (avatar == nullptr) return -1;
    int32_t idx = static_cast<int32_t>(avatar->meshes.size());
    avatar->meshes.push_back(mesh);
    avatar->mesh_material.push_back(material_index);
    return idx;
}

extern "C" IDTX_CORE_API int32_t idtx_avatar_get_mesh_count(const idtx_avatar_t* avatar)
{
    return (avatar != nullptr) ? static_cast<int32_t>(avatar->meshes.size()) : 0;
}

extern "C" IDTX_CORE_API idtx_mesh_t* idtx_avatar_get_mesh(const idtx_avatar_t* avatar, int32_t index)
{
    if (avatar == nullptr || index < 0 || index >= static_cast<int32_t>(avatar->meshes.size())) return nullptr;
    return avatar->meshes[index];
}

extern "C" IDTX_CORE_API int32_t idtx_avatar_get_mesh_material(const idtx_avatar_t* avatar, int32_t mesh_index)
{
    if (avatar == nullptr || mesh_index < 0 || mesh_index >= static_cast<int32_t>(avatar->mesh_material.size())) return -1;
    return avatar->mesh_material[mesh_index];
}

extern "C" IDTX_CORE_API int32_t idtx_avatar_add_material(idtx_avatar_t* avatar, idtx_material_t* mat)
{
    if (avatar == nullptr) return -1;
    int32_t idx = static_cast<int32_t>(avatar->materials.size());
    avatar->materials.push_back(mat);
    return idx;
}

extern "C" IDTX_CORE_API int32_t idtx_avatar_get_material_count(const idtx_avatar_t* avatar)
{
    return (avatar != nullptr) ? static_cast<int32_t>(avatar->materials.size()) : 0;
}

extern "C" IDTX_CORE_API idtx_material_t* idtx_avatar_get_material(const idtx_avatar_t* avatar, int32_t index)
{
    if (avatar == nullptr || index < 0 || index >= static_cast<int32_t>(avatar->materials.size())) return nullptr;
    return avatar->materials[index];
}

// --- Decoded textures (path key -> bytes) --------------------------------

extern "C" IDTX_CORE_API int32_t idtx_avatar_add_texture(
    idtx_avatar_t* avatar, const char* name, const uint8_t* bytes, int32_t byte_count)
{
    if (avatar == nullptr || name == nullptr || bytes == nullptr || byte_count <= 0) { return -1; }
    const int32_t idx = static_cast<int32_t>(avatar->textures.size());
    avatar->textures.emplace_back(std::string(name),
                                  std::vector<uint8_t>(bytes, bytes + byte_count));
    return idx;
}

extern "C" IDTX_CORE_API int32_t idtx_avatar_get_texture_count(const idtx_avatar_t* avatar)
{
    return (avatar != nullptr) ? static_cast<int32_t>(avatar->textures.size()) : 0;
}

extern "C" IDTX_CORE_API const char* idtx_avatar_get_texture_name(const idtx_avatar_t* avatar, int32_t index)
{
    if (avatar == nullptr || index < 0 || index >= static_cast<int32_t>(avatar->textures.size())) { return ""; }
    return avatar->textures[static_cast<size_t>(index)].first.c_str();
}

extern "C" IDTX_CORE_API int32_t idtx_avatar_get_texture_byte_count(const idtx_avatar_t* avatar, int32_t index)
{
    if (avatar == nullptr || index < 0 || index >= static_cast<int32_t>(avatar->textures.size())) { return 0; }
    return static_cast<int32_t>(avatar->textures[static_cast<size_t>(index)].second.size());
}

extern "C" IDTX_CORE_API void idtx_avatar_get_texture_bytes(const idtx_avatar_t* avatar, int32_t index, uint8_t* out_bytes)
{
    if (avatar == nullptr || out_bytes == nullptr) { return; }
    if (index < 0 || index >= static_cast<int32_t>(avatar->textures.size())) { return; }
    const std::vector<uint8_t>& b = avatar->textures[static_cast<size_t>(index)].second;
    if (!b.empty()) { std::memcpy(out_bytes, b.data(), b.size()); }
}

extern "C" IDTX_CORE_API int32_t idtx_avatar_add_spring_chain(idtx_avatar_t* avatar, idtx_spring_chain_t* chain)
{
    if (avatar == nullptr) return -1;
    int32_t idx = static_cast<int32_t>(avatar->spring_chains.size());
    avatar->spring_chains.push_back(chain);
    return idx;
}
extern "C" IDTX_CORE_API int32_t idtx_avatar_get_spring_chain_count(const idtx_avatar_t* avatar)
{ return avatar ? static_cast<int32_t>(avatar->spring_chains.size()) : 0; }
extern "C" IDTX_CORE_API idtx_spring_chain_t* idtx_avatar_get_spring_chain(const idtx_avatar_t* avatar, int32_t index)
{
    if (!avatar || index < 0 || index >= static_cast<int32_t>(avatar->spring_chains.size())) return nullptr;
    return avatar->spring_chains[index];
}

extern "C" IDTX_CORE_API int32_t idtx_avatar_add_spring_collider(idtx_avatar_t* avatar, idtx_spring_collider_t* col)
{
    if (avatar == nullptr) return -1;
    int32_t idx = static_cast<int32_t>(avatar->spring_colliders.size());
    avatar->spring_colliders.push_back(col);
    return idx;
}
extern "C" IDTX_CORE_API int32_t idtx_avatar_get_spring_collider_count(const idtx_avatar_t* avatar)
{ return avatar ? static_cast<int32_t>(avatar->spring_colliders.size()) : 0; }
extern "C" IDTX_CORE_API idtx_spring_collider_t* idtx_avatar_get_spring_collider(const idtx_avatar_t* avatar, int32_t index)
{
    if (!avatar || index < 0 || index >= static_cast<int32_t>(avatar->spring_colliders.size())) return nullptr;
    return avatar->spring_colliders[index];
}

extern "C" IDTX_CORE_API int32_t idtx_avatar_add_physics_collider(idtx_avatar_t* avatar, idtx_physics_collider_t* col)
{
    if (avatar == nullptr) return -1;
    int32_t idx = static_cast<int32_t>(avatar->physics_colliders.size());
    avatar->physics_colliders.push_back(col);
    return idx;
}
extern "C" IDTX_CORE_API int32_t idtx_avatar_get_physics_collider_count(const idtx_avatar_t* avatar)
{ return avatar ? static_cast<int32_t>(avatar->physics_colliders.size()) : 0; }
extern "C" IDTX_CORE_API idtx_physics_collider_t* idtx_avatar_get_physics_collider(const idtx_avatar_t* avatar, int32_t index)
{
    if (!avatar || index < 0 || index >= static_cast<int32_t>(avatar->physics_colliders.size())) return nullptr;
    return avatar->physics_colliders[index];
}
