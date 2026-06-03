// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

// IDTX_CORE_BUILDING_DLL is set by scons/idtxcore.py CPPDEFINES.
#include "idtx_core/idtx_core.h"

#include <cstring>
#include <string>
#include <vector>

struct idtx_skeleton
{
    std::string name;
    std::vector<std::string> bone_names;
    std::vector<int32_t>     bone_parents;
    std::vector<float>       rest_matrices;   // flat: 16 floats per bone
    std::vector<float>       bind_matrices;   // flat: 16 floats per bone
};

static void copy_matrix16(float dst[16], const float src[16])
{
    std::memcpy(dst, src, sizeof(float) * 16);
}

static void identity_matrix16(float dst[16])
{
    std::memset(dst, 0, sizeof(float) * 16);
    dst[0] = dst[5] = dst[10] = dst[15] = 1.0f;
}

extern "C" IDTX_CORE_API idtx_skeleton_t* idtx_skeleton_create(void)
{
    return new idtx_skeleton();
}

extern "C" IDTX_CORE_API void idtx_skeleton_destroy(idtx_skeleton_t* skel)
{
    delete skel;
}

extern "C" IDTX_CORE_API void idtx_skeleton_set_name(idtx_skeleton_t* skel, const char* name)
{
    if (skel == nullptr) return;
    skel->name = (name != nullptr) ? name : "";
}

extern "C" IDTX_CORE_API const char* idtx_skeleton_get_name(const idtx_skeleton_t* skel)
{
    return (skel != nullptr) ? skel->name.c_str() : "";
}

extern "C" IDTX_CORE_API int32_t idtx_skeleton_add_bone(
    idtx_skeleton_t* skel,
    const char* name,
    int32_t parent_index,
    const float rest_matrix[16],
    const float bind_matrix[16])
{
    if (skel == nullptr) return -1;
    int32_t index = static_cast<int32_t>(skel->bone_names.size());
    skel->bone_names.emplace_back(name != nullptr ? name : "");
    skel->bone_parents.push_back(parent_index);

    float rest[16];
    if (rest_matrix != nullptr) copy_matrix16(rest, rest_matrix); else identity_matrix16(rest);
    skel->rest_matrices.insert(skel->rest_matrices.end(), rest, rest + 16);

    float bind[16];
    if (bind_matrix != nullptr) copy_matrix16(bind, bind_matrix); else identity_matrix16(bind);
    skel->bind_matrices.insert(skel->bind_matrices.end(), bind, bind + 16);
    return index;
}

extern "C" IDTX_CORE_API int32_t idtx_skeleton_get_bone_count(const idtx_skeleton_t* skel)
{
    return (skel != nullptr) ? static_cast<int32_t>(skel->bone_names.size()) : 0;
}

extern "C" IDTX_CORE_API const char* idtx_skeleton_get_bone_name(const idtx_skeleton_t* skel, int32_t index)
{
    if (skel == nullptr || index < 0 || index >= static_cast<int32_t>(skel->bone_names.size())) return "";
    return skel->bone_names[index].c_str();
}

extern "C" IDTX_CORE_API int32_t idtx_skeleton_get_bone_parent(const idtx_skeleton_t* skel, int32_t index)
{
    if (skel == nullptr || index < 0 || index >= static_cast<int32_t>(skel->bone_parents.size())) return -1;
    return skel->bone_parents[index];
}

extern "C" IDTX_CORE_API void idtx_skeleton_get_bone_rest(const idtx_skeleton_t* skel, int32_t index, float out_matrix[16])
{
    if (out_matrix == nullptr) return;
    if (skel == nullptr || index < 0 || index >= static_cast<int32_t>(skel->bone_names.size())) {
        identity_matrix16(out_matrix);
        return;
    }
    copy_matrix16(out_matrix, &skel->rest_matrices[index * 16]);
}

extern "C" IDTX_CORE_API void idtx_skeleton_get_bone_bind(const idtx_skeleton_t* skel, int32_t index, float out_matrix[16])
{
    if (out_matrix == nullptr) return;
    if (skel == nullptr || index < 0 || index >= static_cast<int32_t>(skel->bone_names.size())) {
        identity_matrix16(out_matrix);
        return;
    }
    copy_matrix16(out_matrix, &skel->bind_matrices[index * 16]);
}

extern "C" IDTX_CORE_API void idtx_skeleton_set_bone_rest(idtx_skeleton_t* skel, int32_t index, const float matrix[16])
{
    if (skel == nullptr || matrix == nullptr) return;
    if (index < 0 || index >= static_cast<int32_t>(skel->bone_names.size())) return;
    copy_matrix16(&skel->rest_matrices[index * 16], matrix);
}

extern "C" IDTX_CORE_API void idtx_skeleton_set_bone_bind(idtx_skeleton_t* skel, int32_t index, const float matrix[16])
{
    if (skel == nullptr || matrix == nullptr) return;
    if (index < 0 || index >= static_cast<int32_t>(skel->bone_names.size())) return;
    copy_matrix16(&skel->bind_matrices[index * 16], matrix);
}
