// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

#include "idtx_core/idtx_core.h"

#include <cstring>
#include <string>

struct idtx_physics_collider
{
    std::string name;
    idtx_physics_shape_t shape = IDTX_PHYSICS_BOX;
    int32_t attached_bone = -1;
    float transform[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };
    float dimensions[3] = {0.5f, 0.5f, 0.5f};
};

extern "C" {

IDTX_CORE_API idtx_physics_collider_t* idtx_physics_collider_create(void)
{
    return new idtx_physics_collider();
}

IDTX_CORE_API void idtx_physics_collider_destroy(idtx_physics_collider_t* col)
{
    delete col;
}

IDTX_CORE_API void idtx_physics_collider_set_name(idtx_physics_collider_t* col, const char* name)
{
    if (col == nullptr) return;
    col->name = (name != nullptr) ? name : "";
}

IDTX_CORE_API const char* idtx_physics_collider_get_name(const idtx_physics_collider_t* col)
{
    return (col != nullptr) ? col->name.c_str() : "";
}

IDTX_CORE_API idtx_physics_shape_t idtx_physics_collider_get_shape(const idtx_physics_collider_t* col)
{
    return (col != nullptr) ? col->shape : IDTX_PHYSICS_BOX;
}

IDTX_CORE_API void idtx_physics_collider_set_transform(idtx_physics_collider_t* col, const float matrix[16])
{
    if (col == nullptr || matrix == nullptr) return;
    std::memcpy(col->transform, matrix, sizeof(float) * 16);
}

IDTX_CORE_API void idtx_physics_collider_get_transform(const idtx_physics_collider_t* col, float out[16])
{
    if (out == nullptr) return;
    if (col == nullptr) {
        std::memset(out, 0, sizeof(float) * 16);
        out[0] = out[5] = out[10] = out[15] = 1.0f;
        return;
    }
    std::memcpy(out, col->transform, sizeof(float) * 16);
}

IDTX_CORE_API void idtx_physics_collider_set_attached_bone(idtx_physics_collider_t* col, int32_t bone_index)
{
    if (col != nullptr) col->attached_bone = bone_index;
}

IDTX_CORE_API int32_t idtx_physics_collider_get_attached_bone(const idtx_physics_collider_t* col)
{
    return (col != nullptr) ? col->attached_bone : -1;
}

IDTX_CORE_API void idtx_physics_collider_set_box(idtx_physics_collider_t* col, float hx, float hy, float hz)
{
    if (col == nullptr) return;
    col->shape = IDTX_PHYSICS_BOX;
    col->dimensions[0] = hx; col->dimensions[1] = hy; col->dimensions[2] = hz;
}

IDTX_CORE_API void idtx_physics_collider_set_sphere(idtx_physics_collider_t* col, float radius)
{
    if (col == nullptr) return;
    col->shape = IDTX_PHYSICS_SPHERE;
    col->dimensions[0] = radius; col->dimensions[1] = 0.0f; col->dimensions[2] = 0.0f;
}

IDTX_CORE_API void idtx_physics_collider_set_capsule(idtx_physics_collider_t* col, float radius, float height)
{
    if (col == nullptr) return;
    col->shape = IDTX_PHYSICS_CAPSULE;
    col->dimensions[0] = radius; col->dimensions[1] = height; col->dimensions[2] = 0.0f;
}

IDTX_CORE_API void idtx_physics_collider_set_cylinder(idtx_physics_collider_t* col, float radius, float height)
{
    if (col == nullptr) return;
    col->shape = IDTX_PHYSICS_CYLINDER;
    col->dimensions[0] = radius; col->dimensions[1] = height; col->dimensions[2] = 0.0f;
}

IDTX_CORE_API void idtx_physics_collider_set_tapered_capsule(idtx_physics_collider_t* col,
    float top_radius, float bottom_radius, float mid_height)
{
    if (col == nullptr) return;
    col->shape = IDTX_PHYSICS_TAPERED_CAPSULE;
    col->dimensions[0] = top_radius;
    col->dimensions[1] = bottom_radius;
    col->dimensions[2] = mid_height;
}

IDTX_CORE_API void idtx_physics_collider_set_tapered_cylinder(idtx_physics_collider_t* col,
    float top_radius, float bottom_radius, float height)
{
    if (col == nullptr) return;
    col->shape = IDTX_PHYSICS_TAPERED_CYLINDER;
    col->dimensions[0] = top_radius;
    col->dimensions[1] = bottom_radius;
    col->dimensions[2] = height;
}

IDTX_CORE_API void idtx_physics_collider_get_dimensions(const idtx_physics_collider_t* col, float out[3])
{
    if (out == nullptr) return;
    if (col == nullptr) { out[0] = out[1] = out[2] = 0.0f; return; }
    std::memcpy(out, col->dimensions, sizeof(float) * 3);
}

}  // extern "C"
