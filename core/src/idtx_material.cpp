// Copyright 2026 V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

// IDTX_CORE_BUILDING_DLL is set by scons/idtxcore.py CPPDEFINES.
#include "idtx_core/idtx_core.h"

#include <cstring>
#include <string>

struct idtx_material
{
    std::string name;
    float base_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float metallic   = 0.0f;
    float roughness  = 0.5f;
    idtx_alpha_mode_t alpha_mode = IDTX_ALPHA_OPAQUE;
    float alpha_cutoff = 0.5f;
    bool double_sided = false;
    std::string base_color_texture;
    std::string normal_texture;

    bool mtoon = false;
    float mtoon_shade_color[3] = {0.5f, 0.5f, 0.5f};
    float mtoon_rim_color[3]   = {0.0f, 0.0f, 0.0f};
    float mtoon_outline_width  = 0.0f;
};

extern "C" IDTX_CORE_API idtx_material_t* idtx_material_create(void)
{
    return new idtx_material();
}

extern "C" IDTX_CORE_API void idtx_material_destroy(idtx_material_t* mat)
{
    delete mat;
}

extern "C" IDTX_CORE_API void idtx_material_set_name(idtx_material_t* mat, const char* name)
{
    if (mat == nullptr) return;
    mat->name = (name != nullptr) ? name : "";
}

extern "C" IDTX_CORE_API const char* idtx_material_get_name(const idtx_material_t* mat)
{
    return (mat != nullptr) ? mat->name.c_str() : "";
}

extern "C" IDTX_CORE_API void idtx_material_set_base_color(idtx_material_t* mat, float r, float g, float b, float a)
{
    if (mat == nullptr) return;
    mat->base_color[0] = r; mat->base_color[1] = g; mat->base_color[2] = b; mat->base_color[3] = a;
}

extern "C" IDTX_CORE_API void idtx_material_get_base_color(const idtx_material_t* mat, float out_rgba[4])
{
    if (out_rgba == nullptr) return;
    if (mat == nullptr) { out_rgba[0] = out_rgba[1] = out_rgba[2] = out_rgba[3] = 1.0f; return; }
    std::memcpy(out_rgba, mat->base_color, sizeof(float) * 4);
}

extern "C" IDTX_CORE_API void idtx_material_set_metallic(idtx_material_t* mat, float metallic)
{ if (mat != nullptr) mat->metallic = metallic; }

extern "C" IDTX_CORE_API void idtx_material_set_roughness(idtx_material_t* mat, float roughness)
{ if (mat != nullptr) mat->roughness = roughness; }

extern "C" IDTX_CORE_API float idtx_material_get_metallic(const idtx_material_t* mat)
{ return (mat != nullptr) ? mat->metallic : 0.0f; }

extern "C" IDTX_CORE_API float idtx_material_get_roughness(const idtx_material_t* mat)
{ return (mat != nullptr) ? mat->roughness : 0.5f; }

extern "C" IDTX_CORE_API void idtx_material_set_alpha_mode(idtx_material_t* mat, idtx_alpha_mode_t mode)
{ if (mat != nullptr) mat->alpha_mode = mode; }

extern "C" IDTX_CORE_API idtx_alpha_mode_t idtx_material_get_alpha_mode(const idtx_material_t* mat)
{ return (mat != nullptr) ? mat->alpha_mode : IDTX_ALPHA_OPAQUE; }

extern "C" IDTX_CORE_API void idtx_material_set_alpha_cutoff(idtx_material_t* mat, float cutoff)
{ if (mat != nullptr) mat->alpha_cutoff = cutoff; }

extern "C" IDTX_CORE_API float idtx_material_get_alpha_cutoff(const idtx_material_t* mat)
{ return (mat != nullptr) ? mat->alpha_cutoff : 0.5f; }

extern "C" IDTX_CORE_API void idtx_material_set_double_sided(idtx_material_t* mat, int double_sided)
{ if (mat != nullptr) mat->double_sided = (double_sided != 0); }

extern "C" IDTX_CORE_API int idtx_material_get_double_sided(const idtx_material_t* mat)
{ return (mat != nullptr && mat->double_sided) ? 1 : 0; }

extern "C" IDTX_CORE_API void idtx_material_set_base_color_texture(idtx_material_t* mat, const char* path)
{ if (mat != nullptr) mat->base_color_texture = (path != nullptr) ? path : ""; }

extern "C" IDTX_CORE_API const char* idtx_material_get_base_color_texture(const idtx_material_t* mat)
{ return (mat != nullptr) ? mat->base_color_texture.c_str() : ""; }

extern "C" IDTX_CORE_API void idtx_material_set_normal_texture(idtx_material_t* mat, const char* path)
{ if (mat != nullptr) mat->normal_texture = (path != nullptr) ? path : ""; }

extern "C" IDTX_CORE_API const char* idtx_material_get_normal_texture(const idtx_material_t* mat)
{ return (mat != nullptr) ? mat->normal_texture.c_str() : ""; }

extern "C" IDTX_CORE_API void idtx_material_set_mtoon_shade_color(idtx_material_t* mat, float r, float g, float b)
{
    if (mat == nullptr) return;
    mat->mtoon = true;
    mat->mtoon_shade_color[0] = r; mat->mtoon_shade_color[1] = g; mat->mtoon_shade_color[2] = b;
}

extern "C" IDTX_CORE_API void idtx_material_set_mtoon_rim_color(idtx_material_t* mat, float r, float g, float b)
{
    if (mat == nullptr) return;
    mat->mtoon = true;
    mat->mtoon_rim_color[0] = r; mat->mtoon_rim_color[1] = g; mat->mtoon_rim_color[2] = b;
}

extern "C" IDTX_CORE_API void idtx_material_set_mtoon_outline_width(idtx_material_t* mat, float width)
{
    if (mat == nullptr) return;
    mat->mtoon = true;
    mat->mtoon_outline_width = width;
}

extern "C" IDTX_CORE_API int32_t idtx_material_is_mtoon(const idtx_material_t* mat)
{ return (mat != nullptr && mat->mtoon) ? 1 : 0; }

extern "C" IDTX_CORE_API void idtx_material_get_mtoon_shade_color(const idtx_material_t* mat, float out_rgb[3])
{
    if (out_rgb == nullptr) return;
    if (mat == nullptr) { out_rgb[0] = out_rgb[1] = out_rgb[2] = 0.5f; return; }
    std::memcpy(out_rgb, mat->mtoon_shade_color, sizeof(float) * 3);
}

extern "C" IDTX_CORE_API void idtx_material_get_mtoon_rim_color(const idtx_material_t* mat, float out_rgb[3])
{
    if (out_rgb == nullptr) return;
    if (mat == nullptr) { out_rgb[0] = out_rgb[1] = out_rgb[2] = 0.0f; return; }
    std::memcpy(out_rgb, mat->mtoon_rim_color, sizeof(float) * 3);
}

extern "C" IDTX_CORE_API float idtx_material_get_mtoon_outline_width(const idtx_material_t* mat)
{ return (mat != nullptr) ? mat->mtoon_outline_width : 0.0f; }
