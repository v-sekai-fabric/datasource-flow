// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// idtx_core — engine-agnostic C ABI for the idtx-flow avatar pipeline.
//
// All public symbols here are extern "C" with primitive / opaque-handle
// types only. No Godot, no Unity, no C++ STL in the API surface. This
// header is what both libidtxflow (Godot GDExtension) and libidtx_unity
// (Unity P/Invoke) consume.

#ifndef IDTX_CORE_H
#define IDTX_CORE_H

#include <stddef.h>
#include <stdint.h>

// Three build configurations:
//   1. Building libidtx_core as a shared lib (SCons emits .dll/.so/.dylib).
//      `IDTX_CORE_BUILDING_DLL` is defined; symbols get dllexport on
//      Windows, default visibility elsewhere.
//   2. Consuming libidtx_core as a shared lib (GDExtension, CLI).
//      Neither define is set; symbols get dllimport on Windows.
//   3. Statically linking libidtx_core into a host binary (Godot engine
//      module that compiles us into the engine binary). Define
//      `IDTX_CORE_STATIC` so symbols carry no import/export decoration
//      at all — required on Windows because dllimport against a static
//      lib yields LNK2019/LNK4217. Same source tree, three configs.
#if defined(IDTX_CORE_STATIC)
#  define IDTX_CORE_API
#elif defined(_WIN32)
#  ifdef IDTX_CORE_BUILDING_DLL
#    define IDTX_CORE_API __declspec(dllexport)
#  else
#    define IDTX_CORE_API __declspec(dllimport)
#  endif
#else
#  define IDTX_CORE_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Returns a NUL-terminated semver string baked at build time. Stable
// pointer — callers must not free.
IDTX_CORE_API const char* idtx_core_version(void);

// ---------------------------------------------------------------------
// Opaque handle types.
// Every public function takes/returns these as pointers; the underlying
// layout is private to libidtx_core and may change without breaking ABI.
// ---------------------------------------------------------------------

typedef struct idtx_skeleton         idtx_skeleton_t;
typedef struct idtx_mesh             idtx_mesh_t;
typedef struct idtx_material         idtx_material_t;
typedef struct idtx_spring_chain     idtx_spring_chain_t;
typedef struct idtx_spring_collider  idtx_spring_collider_t;
typedef struct idtx_physics_collider idtx_physics_collider_t;
typedef struct idtx_avatar           idtx_avatar_t;

typedef enum idtx_collider_shape
{
    IDTX_COLLIDER_SPHERE  = 0,
    IDTX_COLLIDER_CAPSULE = 1,
} idtx_collider_shape_t;

// Physics-collider primitive shape — separate enum from spring bones
// because the physics layer covers a wider range (boxes, cylinders,
// and the V-Sekai tapered variants that ship in the godot fork at
// v-sekai-multiplayer-fabric/godot@6d88ebde).
typedef enum idtx_physics_shape
{
    IDTX_PHYSICS_BOX              = 0,
    IDTX_PHYSICS_SPHERE           = 1,
    IDTX_PHYSICS_CAPSULE          = 2,
    IDTX_PHYSICS_CYLINDER         = 3,
    IDTX_PHYSICS_TAPERED_CAPSULE  = 4,
    IDTX_PHYSICS_TAPERED_CYLINDER = 5,
} idtx_physics_shape_t;

// ---------------------------------------------------------------------
// idtx_skeleton — bone hierarchy with rest + bind transforms.
//   * rest transform: local pose relative to parent bone (Godot's
//                     skeleton->get_bone_rest equivalent).
//   * bind transform: global pose at the avatar's bind moment (Godot's
//                     skeleton->get_bone_global_rest equivalent).
// Transforms are row-major float[16] matrices in the avatar's coordinate
// system. The skeleton's own root transform sits on the avatar handle,
// not here.
// ---------------------------------------------------------------------

IDTX_CORE_API idtx_skeleton_t* idtx_skeleton_create(void);
IDTX_CORE_API void             idtx_skeleton_destroy(idtx_skeleton_t* skel);

IDTX_CORE_API void    idtx_skeleton_set_name(idtx_skeleton_t* skel, const char* name);
IDTX_CORE_API const char* idtx_skeleton_get_name(const idtx_skeleton_t* skel);

IDTX_CORE_API int32_t idtx_skeleton_add_bone(
    idtx_skeleton_t* skel,
    const char* name,
    int32_t parent_index,        // -1 for root bones
    const float rest_matrix[16],
    const float bind_matrix[16]);

IDTX_CORE_API int32_t idtx_skeleton_get_bone_count(const idtx_skeleton_t* skel);
IDTX_CORE_API const char* idtx_skeleton_get_bone_name(const idtx_skeleton_t* skel, int32_t index);
IDTX_CORE_API int32_t  idtx_skeleton_get_bone_parent(const idtx_skeleton_t* skel, int32_t index);
IDTX_CORE_API void     idtx_skeleton_get_bone_rest(const idtx_skeleton_t* skel, int32_t index, float out_matrix[16]);
IDTX_CORE_API void     idtx_skeleton_get_bone_bind(const idtx_skeleton_t* skel, int32_t index, float out_matrix[16]);
IDTX_CORE_API void     idtx_skeleton_set_bone_rest(idtx_skeleton_t* skel, int32_t index, const float matrix[16]);
IDTX_CORE_API void     idtx_skeleton_set_bone_bind(idtx_skeleton_t* skel, int32_t index, const float matrix[16]);

// ---------------------------------------------------------------------
// idtx_mesh — vertex / index / skinning data for a single mesh surface.
// Multi-surface meshes are represented as multiple idtx_mesh_t handles
// owned by the avatar.
//
// All arrays are caller-owned at set time; the mesh copies internally.
// Layouts:
//   positions: vertex_count * 3 floats (x,y,z)
//   normals:   vertex_count * 3 floats (x,y,z) — pass NULL to skip
//   uvs:       vertex_count * 2 floats (u,v)   — pass NULL to skip
//   colors:    vertex_count * 4 floats (r,g,b,a) — pass NULL to skip
//   indices:   index_count    int32s (triangle list)
//
// Skinning is set separately: bones_per_vertex is typically 4. Layout:
//   bone_indices: vertex_count * bones_per_vertex int32s
//   weights:      vertex_count * bones_per_vertex floats (summed to 1)
// ---------------------------------------------------------------------

IDTX_CORE_API idtx_mesh_t* idtx_mesh_create(void);
IDTX_CORE_API void         idtx_mesh_destroy(idtx_mesh_t* mesh);

IDTX_CORE_API void        idtx_mesh_set_name(idtx_mesh_t* mesh, const char* name);
IDTX_CORE_API const char* idtx_mesh_get_name(const idtx_mesh_t* mesh);

IDTX_CORE_API void idtx_mesh_set_vertices(
    idtx_mesh_t* mesh,
    int32_t vertex_count,
    const float* positions,   // required
    const float* normals,     // optional, NULL ok
    const float* uvs,         // optional, NULL ok
    const float* colors);     // optional, NULL ok

IDTX_CORE_API void idtx_mesh_set_indices(
    idtx_mesh_t* mesh,
    int32_t index_count,
    const int32_t* indices);

IDTX_CORE_API void idtx_mesh_set_skinning(
    idtx_mesh_t* mesh,
    int32_t bones_per_vertex,
    const int32_t* bone_indices,
    const float* weights);

// Override the default triangle-list face structure with custom
// per-face vertex counts (e.g. n-gons preserved through a USD
// round-trip). counts[i] is the number of vertices in face i;
// the values must sum to the mesh's index_count or the call is
// ignored. Passing count=0 / NULL clears the override and the
// USD exporter reverts to emitting all-3s.
IDTX_CORE_API void idtx_mesh_set_face_vertex_counts(
    idtx_mesh_t* mesh,
    int32_t count,
    const int32_t* counts);

IDTX_CORE_API int32_t idtx_mesh_get_face_vertex_count_count(const idtx_mesh_t* mesh);
IDTX_CORE_API void    idtx_mesh_get_face_vertex_counts(const idtx_mesh_t* mesh, int32_t* out_counts);

IDTX_CORE_API int32_t idtx_mesh_get_vertex_count(const idtx_mesh_t* mesh);
IDTX_CORE_API int32_t idtx_mesh_get_index_count(const idtx_mesh_t* mesh);
IDTX_CORE_API int32_t idtx_mesh_get_bones_per_vertex(const idtx_mesh_t* mesh);

// Bulk getters — copy out into caller buffers. out_* may be NULL to
// signal "I just want the count." Pass a buffer of the size returned
// by the matching get_*_count function.
IDTX_CORE_API void idtx_mesh_get_positions(const idtx_mesh_t* mesh, float* out_positions);
IDTX_CORE_API void idtx_mesh_get_normals  (const idtx_mesh_t* mesh, float* out_normals);
IDTX_CORE_API void idtx_mesh_get_uvs      (const idtx_mesh_t* mesh, float* out_uvs);
IDTX_CORE_API void idtx_mesh_get_colors   (const idtx_mesh_t* mesh, float* out_colors);
IDTX_CORE_API void idtx_mesh_get_indices  (const idtx_mesh_t* mesh, int32_t* out_indices);
IDTX_CORE_API void idtx_mesh_get_bone_indices(const idtx_mesh_t* mesh, int32_t* out_bone_indices);
IDTX_CORE_API void idtx_mesh_get_weights     (const idtx_mesh_t* mesh, float* out_weights);

IDTX_CORE_API int32_t idtx_mesh_has_normals(const idtx_mesh_t* mesh);
IDTX_CORE_API int32_t idtx_mesh_has_uvs    (const idtx_mesh_t* mesh);
IDTX_CORE_API int32_t idtx_mesh_has_colors (const idtx_mesh_t* mesh);

// Tris-to-quads reconstruction (CHI-253). Walks the mesh's triangle
// list, builds the dual triangle-adjacency graph, runs greedy
// mutual-best matching (each triangle picks its highest-weight
// neighbour; pairs only land if both pick each other), and replaces
// the index buffer + face_vertex_counts in-place with the
// reconstructed quads + remaining triangles.
//
// `planarity_max_degrees` is the planarity gate: triangle-pair
// normals deviating by more than this many degrees never form a
// quad. Typical values: 5 (strict) to 30 (lenient).
//
// Algorithm matches the GPU Slang shader (lean/Fabric/Mesh/
// TrisToQuadsGPU.lean) so CPU and GPU paths produce
// bit-identical output. The CPU path is the default; the GPU
// path is opt-in via a future `--gpu` flag.
//
// Returns the number of quads formed. Returns -1 on error
// (mesh NULL, non-triangle face counts, etc).
IDTX_CORE_API int32_t idtx_mesh_reconstruct_quads(
    idtx_mesh_t* mesh,
    float planarity_max_degrees);

// ---------------------------------------------------------------------
// idtx_material — PBR + MToon material parameters.
//
// PBR fields are the UsdPreviewSurface baseline (used when round-
// tripping to MaterialX or UsdPreviewSurface). MToon fields are the
// VRM-extension overlay; if any mtoon_* setter is called, the material
// becomes flagged as MToon-typed and will round-trip via
// VRMC_materials_mtoon / VSekaiMToonAPI instead of UsdPreviewSurface.
// ---------------------------------------------------------------------

typedef enum idtx_alpha_mode
{
    IDTX_ALPHA_OPAQUE = 0,
    IDTX_ALPHA_MASK   = 1,
    IDTX_ALPHA_BLEND  = 2,
} idtx_alpha_mode_t;

IDTX_CORE_API idtx_material_t* idtx_material_create(void);
IDTX_CORE_API void             idtx_material_destroy(idtx_material_t* mat);

IDTX_CORE_API void        idtx_material_set_name(idtx_material_t* mat, const char* name);
IDTX_CORE_API const char* idtx_material_get_name(const idtx_material_t* mat);

// PBR baseline
IDTX_CORE_API void idtx_material_set_base_color(idtx_material_t* mat, float r, float g, float b, float a);
IDTX_CORE_API void idtx_material_get_base_color(const idtx_material_t* mat, float out_rgba[4]);
IDTX_CORE_API void idtx_material_set_metallic (idtx_material_t* mat, float metallic);
IDTX_CORE_API void idtx_material_set_roughness(idtx_material_t* mat, float roughness);
IDTX_CORE_API float idtx_material_get_metallic (const idtx_material_t* mat);
IDTX_CORE_API float idtx_material_get_roughness(const idtx_material_t* mat);
IDTX_CORE_API void idtx_material_set_alpha_mode(idtx_material_t* mat, idtx_alpha_mode_t mode);
IDTX_CORE_API idtx_alpha_mode_t idtx_material_get_alpha_mode(const idtx_material_t* mat);
IDTX_CORE_API void idtx_material_set_alpha_cutoff(idtx_material_t* mat, float cutoff);
IDTX_CORE_API float idtx_material_get_alpha_cutoff(const idtx_material_t* mat);

// Texture references — string paths. Empty / NULL means unset.
IDTX_CORE_API void        idtx_material_set_base_color_texture(idtx_material_t* mat, const char* path);
IDTX_CORE_API const char* idtx_material_get_base_color_texture(const idtx_material_t* mat);
IDTX_CORE_API void        idtx_material_set_normal_texture(idtx_material_t* mat, const char* path);
IDTX_CORE_API const char* idtx_material_get_normal_texture(const idtx_material_t* mat);

// MToon overlay — calling any of these marks the material as MToon.
IDTX_CORE_API void idtx_material_set_mtoon_shade_color(idtx_material_t* mat, float r, float g, float b);
IDTX_CORE_API void idtx_material_set_mtoon_rim_color  (idtx_material_t* mat, float r, float g, float b);
IDTX_CORE_API void idtx_material_set_mtoon_outline_width(idtx_material_t* mat, float width);
IDTX_CORE_API int32_t idtx_material_is_mtoon(const idtx_material_t* mat);
IDTX_CORE_API void idtx_material_get_mtoon_shade_color(const idtx_material_t* mat, float out_rgb[3]);
IDTX_CORE_API void idtx_material_get_mtoon_rim_color  (const idtx_material_t* mat, float out_rgb[3]);
IDTX_CORE_API float idtx_material_get_mtoon_outline_width(const idtx_material_t* mat);

// ---------------------------------------------------------------------
// idtx_avatar — the top-level container.
//
// Ownership: the avatar OWNS its handles. Adding a handle transfers
// ownership; destroying the avatar frees all attached handles. Pass
// NULL to add_* to detach a slot.
//
// Mesh-to-material binding is by index — material_index[i] is the
// material attached to mesh[i] (or -1 for none).
// Mesh-to-skeleton binding is implicit (single skeleton per avatar
// in this MVP; multi-skeleton support can land if/when needed).
// ---------------------------------------------------------------------

IDTX_CORE_API idtx_avatar_t* idtx_avatar_create(void);
IDTX_CORE_API void           idtx_avatar_destroy(idtx_avatar_t* avatar);

IDTX_CORE_API void        idtx_avatar_set_name(idtx_avatar_t* avatar, const char* name);
IDTX_CORE_API const char* idtx_avatar_get_name(const idtx_avatar_t* avatar);

// Root transform: avatar's pose relative to its containing world.
IDTX_CORE_API void idtx_avatar_set_root_transform(idtx_avatar_t* avatar, const float matrix[16]);
IDTX_CORE_API void idtx_avatar_get_root_transform(const idtx_avatar_t* avatar, float out_matrix[16]);

// Source VRM version this avatar was upgraded from, for stamping
// `customData["vSekai:upgrade:fromVrm"]` on the USD root prim so a
// later round-trip preserves the provenance. Empty string = no
// upgrade happened (avatar was authored as VRM 1.0 directly).
IDTX_CORE_API void        idtx_avatar_set_source_vrm_version(idtx_avatar_t* avatar, const char* version);
IDTX_CORE_API const char* idtx_avatar_get_source_vrm_version(const idtx_avatar_t* avatar);

// Source USD stage this avatar was imported from, recorded by
// idtx_core_import_avatar_from_usd. The layer-aware exporter
// (idtx_core_export_avatar_to_usd_ex) falls back to this path when its
// opts.source_path is NULL, so an import→export round-trip authors
// deltas against the originating stage without the host re-supplying it.
// Empty string = avatar was not imported from USD (authored in-host or
// from VRM). Setting it transfers no ownership; the string is copied.
IDTX_CORE_API void        idtx_avatar_set_source_usd_path(idtx_avatar_t* avatar, const char* path);
IDTX_CORE_API const char* idtx_avatar_get_source_usd_path(const idtx_avatar_t* avatar);

// Skeleton — at most one. Replacing destroys the previous skeleton.
IDTX_CORE_API void idtx_avatar_set_skeleton(idtx_avatar_t* avatar, idtx_skeleton_t* skel);
IDTX_CORE_API idtx_skeleton_t* idtx_avatar_get_skeleton(const idtx_avatar_t* avatar);

// Mesh list. Returns the index assigned. material_index pairs the mesh
// with the avatar's material slot at that index (or -1).
IDTX_CORE_API int32_t idtx_avatar_add_mesh(idtx_avatar_t* avatar, idtx_mesh_t* mesh, int32_t material_index);
IDTX_CORE_API int32_t idtx_avatar_get_mesh_count(const idtx_avatar_t* avatar);
IDTX_CORE_API idtx_mesh_t* idtx_avatar_get_mesh(const idtx_avatar_t* avatar, int32_t index);
IDTX_CORE_API int32_t idtx_avatar_get_mesh_material(const idtx_avatar_t* avatar, int32_t mesh_index);

// Material list.
IDTX_CORE_API int32_t idtx_avatar_add_material(idtx_avatar_t* avatar, idtx_material_t* mat);
IDTX_CORE_API int32_t idtx_avatar_get_material_count(const idtx_avatar_t* avatar);
IDTX_CORE_API idtx_material_t* idtx_avatar_get_material(const idtx_avatar_t* avatar, int32_t index);

// ---------------------------------------------------------------------
// idtx_spring_chain — one VRMC_springBone joint chain. Joints reference
// bone indices in the avatar's skeleton. Dynamics fields match VRM 1.0:
//   stiffness     ([0..1+], how aggressively the chain returns to rest)
//   drag          ([0..1],  per-frame velocity damping)
//   gravity_power ([0..],   gravity scale; gravity_dir is the unit dir)
//   hit_radius    (meters,  collider intersection radius for the joint)
// ---------------------------------------------------------------------

IDTX_CORE_API idtx_spring_chain_t* idtx_spring_chain_create(void);
IDTX_CORE_API void                 idtx_spring_chain_destroy(idtx_spring_chain_t* chain);

IDTX_CORE_API void        idtx_spring_chain_set_name(idtx_spring_chain_t* chain, const char* name);
IDTX_CORE_API const char* idtx_spring_chain_get_name(const idtx_spring_chain_t* chain);

IDTX_CORE_API void idtx_spring_chain_set_joints(
    idtx_spring_chain_t* chain,
    int32_t count,
    const int32_t* bone_indices);   // indices into the avatar's idtx_skeleton

IDTX_CORE_API void idtx_spring_chain_set_dynamics(
    idtx_spring_chain_t* chain,
    float stiffness,
    float drag,
    float gravity_power,
    float hit_radius);

IDTX_CORE_API void idtx_spring_chain_set_gravity_dir(
    idtx_spring_chain_t* chain, float x, float y, float z);

// Append a collider-index reference (into the avatar's spring_collider
// list). Multiple colliders per chain are allowed.
IDTX_CORE_API void idtx_spring_chain_add_collider(idtx_spring_chain_t* chain, int32_t collider_index);

IDTX_CORE_API int32_t idtx_spring_chain_get_joint_count(const idtx_spring_chain_t* chain);
IDTX_CORE_API int32_t idtx_spring_chain_get_joint(const idtx_spring_chain_t* chain, int32_t index);
IDTX_CORE_API float   idtx_spring_chain_get_stiffness(const idtx_spring_chain_t* chain);
IDTX_CORE_API float   idtx_spring_chain_get_drag(const idtx_spring_chain_t* chain);
IDTX_CORE_API float   idtx_spring_chain_get_gravity_power(const idtx_spring_chain_t* chain);
IDTX_CORE_API float   idtx_spring_chain_get_hit_radius(const idtx_spring_chain_t* chain);
IDTX_CORE_API void    idtx_spring_chain_get_gravity_dir(const idtx_spring_chain_t* chain, float out_xyz[3]);
IDTX_CORE_API int32_t idtx_spring_chain_get_collider_count(const idtx_spring_chain_t* chain);
IDTX_CORE_API int32_t idtx_spring_chain_get_collider(const idtx_spring_chain_t* chain, int32_t index);

// ---------------------------------------------------------------------
// idtx_spring_collider — one VRMC_springBone collider primitive.
// Sphere uses offset + radius. Capsule adds a tail point (offset to
// tail forms the line segment, radius is the swept radius).
// ---------------------------------------------------------------------

IDTX_CORE_API idtx_spring_collider_t* idtx_spring_collider_create(void);
IDTX_CORE_API void                    idtx_spring_collider_destroy(idtx_spring_collider_t* col);

IDTX_CORE_API void        idtx_spring_collider_set_name(idtx_spring_collider_t* col, const char* name);
IDTX_CORE_API const char* idtx_spring_collider_get_name(const idtx_spring_collider_t* col);

IDTX_CORE_API void idtx_spring_collider_set_attached_bone(idtx_spring_collider_t* col, int32_t bone_index);
IDTX_CORE_API int32_t idtx_spring_collider_get_attached_bone(const idtx_spring_collider_t* col);

IDTX_CORE_API void idtx_spring_collider_set_shape(idtx_spring_collider_t* col, idtx_collider_shape_t shape);
IDTX_CORE_API idtx_collider_shape_t idtx_spring_collider_get_shape(const idtx_spring_collider_t* col);

IDTX_CORE_API void idtx_spring_collider_set_offset(idtx_spring_collider_t* col, float x, float y, float z);
IDTX_CORE_API void idtx_spring_collider_get_offset(const idtx_spring_collider_t* col, float out_xyz[3]);
IDTX_CORE_API void idtx_spring_collider_set_radius(idtx_spring_collider_t* col, float radius);
IDTX_CORE_API float idtx_spring_collider_get_radius(const idtx_spring_collider_t* col);
IDTX_CORE_API void idtx_spring_collider_set_tail(idtx_spring_collider_t* col, float x, float y, float z);
IDTX_CORE_API void idtx_spring_collider_get_tail(const idtx_spring_collider_t* col, float out_xyz[3]);

// Avatar-level spring chain / collider lists.
IDTX_CORE_API int32_t idtx_avatar_add_spring_chain(idtx_avatar_t* avatar, idtx_spring_chain_t* chain);
IDTX_CORE_API int32_t idtx_avatar_get_spring_chain_count(const idtx_avatar_t* avatar);
IDTX_CORE_API idtx_spring_chain_t* idtx_avatar_get_spring_chain(const idtx_avatar_t* avatar, int32_t index);

IDTX_CORE_API int32_t idtx_avatar_add_spring_collider(idtx_avatar_t* avatar, idtx_spring_collider_t* col);
IDTX_CORE_API int32_t idtx_avatar_get_spring_collider_count(const idtx_avatar_t* avatar);
IDTX_CORE_API idtx_spring_collider_t* idtx_avatar_get_spring_collider(const idtx_avatar_t* avatar, int32_t index);

// ---------------------------------------------------------------------
// idtx_physics_collider — a CollisionShape3D-equivalent attached to a
// PhysicsBody3D (StaticBody / RigidBody / Area / CharacterBody). Lives
// on the avatar separate from spring-bone colliders since the two
// concepts don't overlap: spring colliders affect cloth/hair sims,
// physics colliders affect rigid-body collision.
//
// Shape-specific dimensions are stored as 3 floats. Meaning depends on
// the shape enum (see idtx_physics_collider_get_dimensions docs).
// ---------------------------------------------------------------------

IDTX_CORE_API idtx_physics_collider_t* idtx_physics_collider_create(void);
IDTX_CORE_API void                     idtx_physics_collider_destroy(idtx_physics_collider_t* col);

IDTX_CORE_API void        idtx_physics_collider_set_name(idtx_physics_collider_t* col, const char* name);
IDTX_CORE_API const char* idtx_physics_collider_get_name(const idtx_physics_collider_t* col);

IDTX_CORE_API idtx_physics_shape_t idtx_physics_collider_get_shape(const idtx_physics_collider_t* col);

// Local transform relative to the parent (typically a PhysicsBody3D root).
IDTX_CORE_API void idtx_physics_collider_set_transform(idtx_physics_collider_t* col, const float matrix[16]);
IDTX_CORE_API void idtx_physics_collider_get_transform(const idtx_physics_collider_t* col, float out_matrix[16]);

// Optional bone attachment for skinned physics (e.g. bone-driven
// hitboxes on a Skeleton3D). -1 = world-space body root.
IDTX_CORE_API void    idtx_physics_collider_set_attached_bone(idtx_physics_collider_t* col, int32_t bone_index);
IDTX_CORE_API int32_t idtx_physics_collider_get_attached_bone(const idtx_physics_collider_t* col);

// Shape setters — choose one per collider. Each writes the dimensions
// array AND the shape enum.
IDTX_CORE_API void idtx_physics_collider_set_box(idtx_physics_collider_t* col,
    float half_extent_x, float half_extent_y, float half_extent_z);
IDTX_CORE_API void idtx_physics_collider_set_sphere(idtx_physics_collider_t* col, float radius);
IDTX_CORE_API void idtx_physics_collider_set_capsule(idtx_physics_collider_t* col,
    float radius, float height);
IDTX_CORE_API void idtx_physics_collider_set_cylinder(idtx_physics_collider_t* col,
    float radius, float height);
IDTX_CORE_API void idtx_physics_collider_set_tapered_capsule(idtx_physics_collider_t* col,
    float top_radius, float bottom_radius, float mid_height);
IDTX_CORE_API void idtx_physics_collider_set_tapered_cylinder(idtx_physics_collider_t* col,
    float top_radius, float bottom_radius, float height);

// Bulk getter. Layout by shape:
//   BOX             : [half_extent_x, half_extent_y, half_extent_z]
//   SPHERE          : [radius, 0, 0]
//   CAPSULE         : [radius, height, 0]
//   CYLINDER        : [radius, height, 0]
//   TAPERED_CAPSULE : [top_radius, bottom_radius, mid_height]
//   TAPERED_CYLINDER: [top_radius, bottom_radius, height]
IDTX_CORE_API void idtx_physics_collider_get_dimensions(const idtx_physics_collider_t* col, float out_dims[3]);

// Avatar-level list.
IDTX_CORE_API int32_t                  idtx_avatar_add_physics_collider(idtx_avatar_t* avatar, idtx_physics_collider_t* col);
IDTX_CORE_API int32_t                  idtx_avatar_get_physics_collider_count(const idtx_avatar_t* avatar);
IDTX_CORE_API idtx_physics_collider_t* idtx_avatar_get_physics_collider(const idtx_avatar_t* avatar, int32_t index);

// ---------------------------------------------------------------------
// Top-level I/O entry points.
//
// Returns 0 on success, non-zero on failure. Error categories:
//   1 = invalid argument (NULL avatar / NULL path)
//   2 = USD stage creation failed (e.g. unwritable path)
//   3 = USD write failed (Save() returned false)
// ---------------------------------------------------------------------

IDTX_CORE_API int32_t idtx_core_export_avatar_to_usd(
    const idtx_avatar_t* avatar,
    const char* path);

// ---------------------------------------------------------------------
// Layer-aware USD export (the round-trip / composition-aware path).
//
// idtx_core_export_avatar_to_usd() above is the destructive flat write:
// it CreateNew()s a single-layer stage and authors every prim fresh.
// That loses the layer structure of a stage the avatar was imported
// from — references, payloads, and sublayer routing all collapse.
//
// _ex() is a strict superset. With mode=IDTX_USD_NEW_FLAT and
// source_path=NULL it is byte-for-byte identical to the flat call
// (which is in fact implemented as a forwarder to this function). The
// other three modes consult `source_path` — the stage the avatar was
// imported from — and author the avatar's data as deltas against it:
//
//   IDTX_USD_NEW_FLAT   Fresh single-layer stage. `source_path` ignored.
//                       The legacy behaviour.
//   IDTX_USD_OVERLAY    Open `source_path`, author changed attributes as
//                       `over` opinions on a NEW root layer that sublayers
//                       the source. References/payloads in the source are
//                       preserved; only deltas are written to `path`.
//   IDTX_USD_LAYER_ONLY Write ONLY the delta layer to `path`; the source
//                       is pulled in by a composition REFERENCE arc on the
//                       avatar root (not a sublayer), so the delta is a
//                       standalone stage that incorporates the source
//                       asset. The thinnest artifact.
//
// OVERLAY and LAYER_ONLY differ only in that arc — OVERLAY sublayers the
// source, LAYER_ONLY references it. Both are delta-minimised: the full
// avatar is authored, then every opinion equal to the composed source —
// attribute values, attribute connections, AND relationship targets
// (e.g. material:binding) — is erased and the surviving prims flip from
// `def` to `over`, so an unchanged import->export round-trip yields a
// delta with no prim opinions at all (just the arc to the source).
//   IDTX_USD_FLATTEN    Compose `source_path` + the avatar's deltas, then
//                       flatten the whole layer stack into a single
//                       standalone stage at `path`. References/payloads
//                       are resolved inline.
//
// `edit_target_id`: for a composed source with multiple sublayers, names
// which sublayer the `over` opinions route to (the layer's identifier as
// returned by import-side provenance). NULL = the root/strongest layer.
//
// `reflect_per_prim`: 0 authors the avatar subtree as a unit; 1 mirrors
// each imageable prim individually so callers can edit Xform prims in
// isolation (answers the per-prim-editability question in upstream
// idtx-flow#18 consideration 1).
//
// Provenance: when `source_path` is NULL but the avatar carries an
// imported source path (stamped by idtx_core_import_avatar_from_usd),
// that path is used automatically. An explicit `source_path` overrides.
//
// Error codes extend the flat path:
//   0 = success                 3 = USD write (Save/Export) failed
//   1 = invalid argument        4 = source_path open failed
//   2 = stage creation failed   5 = requested edit_target_id not found
// ---------------------------------------------------------------------

typedef enum idtx_usd_export_mode
{
    IDTX_USD_NEW_FLAT   = 0,  // fresh single-layer stage (legacy default)
    IDTX_USD_OVERLAY    = 1,  // deltas as `over`s on a layer that sublayers source
    IDTX_USD_LAYER_ONLY = 2,  // write only the delta layer; it references source
    IDTX_USD_FLATTEN    = 3,  // compose source + deltas, flatten to standalone
    IDTX_USD_MODE_MAX   = 4,  // sentinel: count of modes / bounds check (Godot convention)
} idtx_usd_export_mode_t;

typedef struct idtx_usd_export_opts
{
    idtx_usd_export_mode_t mode;            // default IDTX_USD_NEW_FLAT
    const char*            source_path;     // imported-from stage; NULL = use avatar provenance / none
    const char*            edit_target_id;  // sublayer to route overs to; NULL = root layer
    int32_t                reflect_per_prim;// 0 = subtree as a unit, 1 = per imageable prim
} idtx_usd_export_opts_t;

// Convenience initializer — sets mode=IDTX_USD_NEW_FLAT and all pointers
// NULL / counts 0. Function (not a header struct literal) for the same
// reason idtx_scn_opts_init is: C forbids non-const file-scope struct
// initializers in a header cleanly.
IDTX_CORE_API void idtx_usd_export_opts_init(idtx_usd_export_opts_t* opts);

// Layer-aware export. `opts` NULL = flat defaults (legacy behaviour).
IDTX_CORE_API int32_t idtx_core_export_avatar_to_usd_ex(
    const idtx_avatar_t* avatar,
    const char* path,
    const idtx_usd_export_opts_t* opts);

// Open a USD stage at `path` and rebuild an idtx_avatar_t* from its
// default prim (or the first prim if no default is set). Returns NULL
// on failure (NULL path, open failed, no usable root prim). Caller
// owns the returned handle and frees with idtx_avatar_destroy.
IDTX_CORE_API idtx_avatar_t* idtx_core_import_avatar_from_usd(
    const char* path);

// VRM 1.0 export — writes a .vrm (glTF binary container) with the
// VRMC_vrm extension. MToon materials get VRMC_materials_mtoon; if a
// skeleton is present, its bone names are looked up against the
// humanoid-bones bridge map for the VRMC_vrm.humanoid.humanBones table.
//
// Error codes match the USD path:
//   0 = success
//   1 = invalid argument
//   2 = file open failed
//   3 = write failed
//   99 = not yet implemented for the requested feature combination
IDTX_CORE_API int32_t idtx_core_export_avatar_to_vrm(
    const idtx_avatar_t* avatar,
    const char* path);

// VRM 1.0 import — parses a .vrm at `path` and rebuilds an
// idtx_avatar_t* from it (inverse of the export).
IDTX_CORE_API idtx_avatar_t* idtx_core_import_avatar_from_vrm(
    const char* path);

// ---------------------------------------------------------------------
// Godot .scn (binary PackedScene) export.
//
// Writes a Godot 4 .scn FORMAT_VERSION=6 binary resource directly,
// without linking Godot. Two file variants:
//   * RSRC (uncompressed) — plain binary, footer "RSRC"
//   * RSCC (zstd block-compressed) — compression mode 2, block size
//                                    4096, footer "RSCC"
//
// Format spec: PredictiveBVH/Codegen/GodotBinary.lean.
// Slang-emitted writers: openusd-fabric/lean/Fabric/Serialization/
//                        GodotScn.lean → godot_scn.slang.
//
// Strings have NO 4-byte padding (validated against Godot 4.7 output).
//
// Unknown classes are emitted as MissingNode/MissingResource per
// godot-proposals#5945, so a server without the GDExtension still
// loads the file without losing property data.
// ---------------------------------------------------------------------

typedef enum idtx_scn_compression
{
    IDTX_SCN_UNCOMPRESSED = 0,  // RSRC magic, no compression
    IDTX_SCN_ZSTD         = 2,  // RSCC magic, zstd block compression
} idtx_scn_compression_t;

typedef struct idtx_scn_opts
{
    idtx_scn_compression_t compression;  // default IDTX_SCN_ZSTD
    int32_t                block_size;   // zstd block size; default 4096
    int32_t                generate_lods; // 0 = off, nonzero = run meshoptimizer
    float                  lod_target_error; // simplification stop; default 1.0
    int32_t                basis_universal; // 0 = leave textures uncompressed,
                                            // nonzero = transcode to PortableCompressedTexture2D
} idtx_scn_opts_t;

// Convenience initializer for idtx_scn_opts_t with sensible defaults.
// (Function rather than a static struct because C doesn't allow
// non-const initializers of file-scope structs in headers cleanly.)
IDTX_CORE_API void idtx_scn_opts_init(idtx_scn_opts_t* opts);

// Returns 0 on success, non-zero on failure.
//   1 = invalid argument
//   2 = file open / write failed
//   3 = compression failed
//   4 = LOD generation failed
//   5 = texture compression failed
IDTX_CORE_API int32_t idtx_core_export_avatar_to_scn(
    const idtx_avatar_t* avatar,
    const char* path,
    const idtx_scn_opts_t* opts);   // NULL = use defaults

// Same as above but writes to a caller-owned buffer. Returns the
// number of bytes written, or negative on error. Pass out_buf=NULL,
// out_cap=0 to query the required size; the function returns the
// negative of the required size in that case (so callers can do a
// two-call pattern).
IDTX_CORE_API int64_t idtx_core_export_avatar_to_scn_buffer(
    const idtx_avatar_t* avatar,
    uint8_t* out_buf,
    size_t out_cap,
    const idtx_scn_opts_t* opts);

// ---------------------------------------------------------------------
// Streaming progress callback.
//
// All long-running entrypoints (import_usd, import_vrm, export_*) may
// invoke this callback zero or more times during their execution to
// report progress. Hosts wire it to engine-specific UI (Godot's
// EditorProgress, a CLI progress bar, etc.). The callback is OPTIONAL;
// libidtx_core works fine without it set.
//
// fraction is in [0.0, 1.0]. message is a NUL-terminated UTF-8 string
// owned by the library — do not free, do not retain past the call.
// user is the opaque pointer passed to set_progress_cb.
//
// Thread safety: callbacks may fire from worker threads inside
// libidtx_core. Hosts must marshal to their main thread if their UI
// library requires it.
// ---------------------------------------------------------------------

typedef void (*idtx_progress_fn)(float fraction, const char* message, void* user);

IDTX_CORE_API void idtx_core_set_progress_cb(idtx_progress_fn cb, void* user);

// ---------------------------------------------------------------------
// Library lifecycle.
//
// idtx_core_init() is idempotent and thread-safe. It performs one-time
// setup: prepends the shipped v_sekai_schema plugin directory to
// PXR_PLUGINPATH_NAME so the codeless API schema is discoverable in
// every host before any UsdStage::Open() call. Hosts SHOULD call this
// once at startup; calling it more than once is a no-op.
//
// `plugin_dir` overrides the default lookup (shared lib's neighbouring
// `share/idtx_core/` directory). Pass NULL to use the default.
//
// Returns 0 on success. Non-zero indicates the schema directory was
// not findable; the library still works, but `v_sekai:*` USD
// attributes will be opaque rather than schema-validated.
// ---------------------------------------------------------------------

IDTX_CORE_API int32_t idtx_core_init(const char* plugin_dir);

#ifdef __cplusplus
}
#endif

#endif // IDTX_CORE_H
