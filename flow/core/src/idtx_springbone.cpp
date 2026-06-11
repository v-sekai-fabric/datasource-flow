// Copyright 2026 V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

#include "idtx_core/idtx_core.h"

#include <cstring>
#include <string>
#include <vector>

struct idtx_spring_chain
{
    std::string name;
    std::vector<int32_t> joints;
    std::vector<int32_t> colliders;
    float stiffness     = 1.0f;
    float drag          = 0.4f;
    float gravity_power = 0.0f;
    float hit_radius    = 0.02f;
    float gravity_dir[3] = {0.0f, -1.0f, 0.0f};
};

struct idtx_spring_collider
{
    std::string name;
    int32_t attached_bone = -1;
    idtx_collider_shape_t shape = IDTX_COLLIDER_SPHERE;
    float offset[3] = {0.0f, 0.0f, 0.0f};
    float radius = 0.05f;
    float tail[3]   = {0.0f, 0.0f, 0.0f};
};

extern "C" {

IDTX_CORE_API idtx_spring_chain_t* idtx_spring_chain_create(void) { return new idtx_spring_chain(); }
IDTX_CORE_API void idtx_spring_chain_destroy(idtx_spring_chain_t* c) { delete c; }

IDTX_CORE_API void idtx_spring_chain_set_name(idtx_spring_chain_t* c, const char* name)
{ if (c) c->name = (name ? name : ""); }
IDTX_CORE_API const char* idtx_spring_chain_get_name(const idtx_spring_chain_t* c)
{ return c ? c->name.c_str() : ""; }

IDTX_CORE_API void idtx_spring_chain_set_joints(idtx_spring_chain_t* c, int32_t count, const int32_t* bone_indices)
{
    if (!c || count < 0 || (count > 0 && !bone_indices)) return;
    c->joints.assign(bone_indices, bone_indices + count);
}

IDTX_CORE_API void idtx_spring_chain_set_dynamics(
    idtx_spring_chain_t* c, float stiffness, float drag, float gravity_power, float hit_radius)
{
    if (!c) return;
    c->stiffness = stiffness;
    c->drag = drag;
    c->gravity_power = gravity_power;
    c->hit_radius = hit_radius;
}

IDTX_CORE_API void idtx_spring_chain_set_gravity_dir(idtx_spring_chain_t* c, float x, float y, float z)
{ if (c) { c->gravity_dir[0] = x; c->gravity_dir[1] = y; c->gravity_dir[2] = z; } }

IDTX_CORE_API void idtx_spring_chain_add_collider(idtx_spring_chain_t* c, int32_t collider_index)
{ if (c && collider_index >= 0) c->colliders.push_back(collider_index); }

IDTX_CORE_API int32_t idtx_spring_chain_get_joint_count(const idtx_spring_chain_t* c)
{ return c ? static_cast<int32_t>(c->joints.size()) : 0; }
IDTX_CORE_API int32_t idtx_spring_chain_get_joint(const idtx_spring_chain_t* c, int32_t index)
{
    if (!c || index < 0 || index >= static_cast<int32_t>(c->joints.size())) return -1;
    return c->joints[index];
}
IDTX_CORE_API float idtx_spring_chain_get_stiffness(const idtx_spring_chain_t* c)     { return c ? c->stiffness     : 0.0f; }
IDTX_CORE_API float idtx_spring_chain_get_drag(const idtx_spring_chain_t* c)          { return c ? c->drag          : 0.0f; }
IDTX_CORE_API float idtx_spring_chain_get_gravity_power(const idtx_spring_chain_t* c) { return c ? c->gravity_power : 0.0f; }
IDTX_CORE_API float idtx_spring_chain_get_hit_radius(const idtx_spring_chain_t* c)    { return c ? c->hit_radius    : 0.0f; }
IDTX_CORE_API void  idtx_spring_chain_get_gravity_dir(const idtx_spring_chain_t* c, float out[3])
{
    if (!out) return;
    if (!c) { out[0] = 0; out[1] = -1; out[2] = 0; return; }
    std::memcpy(out, c->gravity_dir, sizeof(float) * 3);
}
IDTX_CORE_API int32_t idtx_spring_chain_get_collider_count(const idtx_spring_chain_t* c)
{ return c ? static_cast<int32_t>(c->colliders.size()) : 0; }
IDTX_CORE_API int32_t idtx_spring_chain_get_collider(const idtx_spring_chain_t* c, int32_t index)
{
    if (!c || index < 0 || index >= static_cast<int32_t>(c->colliders.size())) return -1;
    return c->colliders[index];
}

IDTX_CORE_API idtx_spring_collider_t* idtx_spring_collider_create(void) { return new idtx_spring_collider(); }
IDTX_CORE_API void idtx_spring_collider_destroy(idtx_spring_collider_t* c) { delete c; }

IDTX_CORE_API void idtx_spring_collider_set_name(idtx_spring_collider_t* c, const char* name)
{ if (c) c->name = (name ? name : ""); }
IDTX_CORE_API const char* idtx_spring_collider_get_name(const idtx_spring_collider_t* c)
{ return c ? c->name.c_str() : ""; }

IDTX_CORE_API void idtx_spring_collider_set_attached_bone(idtx_spring_collider_t* c, int32_t bone_index)
{ if (c) c->attached_bone = bone_index; }
IDTX_CORE_API int32_t idtx_spring_collider_get_attached_bone(const idtx_spring_collider_t* c)
{ return c ? c->attached_bone : -1; }

IDTX_CORE_API void idtx_spring_collider_set_shape(idtx_spring_collider_t* c, idtx_collider_shape_t shape)
{ if (c) c->shape = shape; }
IDTX_CORE_API idtx_collider_shape_t idtx_spring_collider_get_shape(const idtx_spring_collider_t* c)
{ return c ? c->shape : IDTX_COLLIDER_SPHERE; }

IDTX_CORE_API void idtx_spring_collider_set_offset(idtx_spring_collider_t* c, float x, float y, float z)
{ if (c) { c->offset[0] = x; c->offset[1] = y; c->offset[2] = z; } }
IDTX_CORE_API void idtx_spring_collider_get_offset(const idtx_spring_collider_t* c, float out[3])
{
    if (!out) return;
    if (!c) { out[0] = out[1] = out[2] = 0.0f; return; }
    std::memcpy(out, c->offset, sizeof(float) * 3);
}
IDTX_CORE_API void idtx_spring_collider_set_radius(idtx_spring_collider_t* c, float radius)
{ if (c) c->radius = radius; }
IDTX_CORE_API float idtx_spring_collider_get_radius(const idtx_spring_collider_t* c)
{ return c ? c->radius : 0.0f; }
IDTX_CORE_API void idtx_spring_collider_set_tail(idtx_spring_collider_t* c, float x, float y, float z)
{ if (c) { c->tail[0] = x; c->tail[1] = y; c->tail[2] = z; } }
IDTX_CORE_API void idtx_spring_collider_get_tail(const idtx_spring_collider_t* c, float out[3])
{
    if (!out) return;
    if (!c) { out[0] = out[1] = out[2] = 0.0f; return; }
    std::memcpy(out, c->tail, sizeof(float) * 3);
}

}  // extern "C"
