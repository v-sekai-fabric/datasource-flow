/**************************************************************************/
/*  idtx_scene.h                                                          */
/**************************************************************************/
/* Copyright 2026 The openusd-fabric authors / V-Sekai contributors.      */
/* SPDX-License-Identifier: Apache-2.0 OR MPL-2.0                         */
/**************************************************************************/

// idtx_scene — an engine-neutral "converted USD stage": a flat tree of typed
// nodes that EVERY host (the idtxcli CLI, the Blender addon, the viser web
// host, and the Godot GDExtension) walks to build its own native entities.
//
// Rationale: the USD-reading half of the stage converter is engine-agnostic;
// only the "emit a native node" hooks differ per host. So the converter lives
// ONCE inside libidtx_core (the single OpenUSD consumer), and the result is
// handed across the flat C ABI below. No pxr/OpenUSD type ever crosses this
// boundary — that is what lets the hosts link zero OpenUSD and reach it purely
// through the dlopen'd core (CHI-312).
//
// This header is the geometry subset (Phase 1a): the scene tree plus xform /
// primitive / mesh / skeleton / material / collision nodes — enough to open a
// stage and render it. Animation tracks, deferred-payload streaming, variant
// editing, and pseudo-instancing land in Phase 1b (see idtx_scene.h additions
// guarded by their own node kinds / getters).
//
// CONVENTIONS — these follow the upstream openusd-fabric converter exactly
// (StageConverter::ConvertStagePostProcess + MeshConverter), NOT reinvented:
//
//   * Coordinate system: right-handed (USD is always right-handed).
//
//   * WINDING is normalized in the core. The mesh reader follows MeshConverter:
//     it reads each gprim's `orientation` attribute — and where unauthored, this
//     pipeline's established default is `leftHanded` (note: USD's own spec
//     fallback is rightHanded; we match upstream, not the spec) — then emits
//     indices/normals in the engine-ready CCW winding. So every idtx_mesh here
//     is consistent; there is deliberately no orientation getter.
//
//   * UP AXIS + UNITS are NOT baked into geometry or transforms. They are
//     exposed (idtx_scene_get_up_axis / idtx_scene_get_meters_per_unit) and the
//     host applies them at the converted ROOT, exactly as ConvertStagePostProcess
//     does — because the swing is engine-specific:
//        - Z-up stage  -> rotate root -90° about X   (for a Y-up engine)
//        - X-up stage  -> rotate root +90° about Z   (for a Y-up engine)
//        - Y-up stage  -> no rotation
//        - a Z-up engine (Blender) rotates differently / not at all
//        - then root scale *= metersPerUnit, SKIPPED for nested-payload stages
//          (their MPU is handled by the referencing prim's scale).
//     metersPerUnit fallback is 0.01 (cm), but authored stages commonly use 1.0
//     (this repo's Miroir does) — always read it, never assume cm or m.
//
//   * USD defines no FORWARD axis — that is the host's choice.

#ifndef IDTX_CORE_IDTX_SCENE_H
#define IDTX_CORE_IDTX_SCENE_H

#include "idtx_core/idtx_core.h"   // IDTX_CORE_API + idtx_mesh_t/idtx_skeleton_t/idtx_material_t

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handles. A scene OWNS its nodes (and the mesh/skeleton handles they
// expose); destroying the scene frees everything. Node/mesh/skeleton pointers
// returned below are borrowed — valid until idtx_core_scene_destroy().
typedef struct idtx_scene idtx_scene_t;
typedef struct idtx_node  idtx_node_t;

// One kind per ConvertXxx hook in the converter. A host switch()es on this to
// decide which native entity to build and which payload getters to call.
typedef enum idtx_node_kind {
    IDTX_NODE_XFORM = 0,        // transform-only prim (UsdGeomXform)
    IDTX_NODE_CUBE,             // UsdGeomCube
    IDTX_NODE_CONE,             // UsdGeomCone
    IDTX_NODE_CYLINDER,         // UsdGeomCylinder
    IDTX_NODE_SPHERE,           // UsdGeomSphere
    IDTX_NODE_MESH,             // UsdGeomMesh (geometry via idtx_node_get_mesh)
    IDTX_NODE_SKELETON,         // UsdSkelRoot/Skeleton (via idtx_node_get_skeleton)
    IDTX_NODE_COLLISION_ROOT,   // Xform carrying the IDTXInteractionAPI
    IDTX_NODE_COLLISION,        // Gprim carrying the IDTXCollisionAPI
} idtx_node_kind_t;

// Collider shape, mirrors IDTXCollisionAPI's `shape` token (Cube/Sphere/...).
typedef enum idtx_collision_shape {
    IDTX_COLLISION_SHAPE_CUBE = 0,
    IDTX_COLLISION_SHAPE_SPHERE,
    IDTX_COLLISION_SHAPE_CAPSULE,
    IDTX_COLLISION_SHAPE_CYLINDER,
    IDTX_COLLISION_SHAPE_CONE,
    IDTX_COLLISION_SHAPE_MESH,
    IDTX_COLLISION_SHAPE_UNKNOWN = -1,
} idtx_collision_shape_t;

// Axis token (UsdGeomTokens->x/y/z), as carried by cone/cylinder/collision.
typedef enum idtx_axis { IDTX_AXIS_X = 0, IDTX_AXIS_Y = 1, IDTX_AXIS_Z = 2 } idtx_axis_t;

// Primvar interpolation for the display color (UsdGeomTokens->constant/vertex/...).
typedef enum idtx_color_interp {
    IDTX_COLOR_INTERP_CONSTANT = 0,
    IDTX_COLOR_INTERP_UNIFORM,
    IDTX_COLOR_INTERP_VARYING,
    IDTX_COLOR_INTERP_VERTEX,
    IDTX_COLOR_INTERP_FACE_VARYING,
} idtx_color_interp_t;

// ---------------------------------------------------------------------
// Import + lifetime.
// ---------------------------------------------------------------------

// Open the USD stage at `uri` and convert it into a flat node tree. `uri` may
// be a filesystem path or any scheme the registered asset-IO resolver handles
// (res://, http — Phase 2). Returns NULL on failure (NULL uri, open failed, no
// usable root prim). Caller owns the result and frees with scene_destroy.
IDTX_CORE_API idtx_scene_t* idtx_core_import_scene_from_usd(const char* uri);

IDTX_CORE_API void idtx_core_scene_destroy(idtx_scene_t* scene);

// ---------------------------------------------------------------------
// Stage metadata — needed for the host's coordinate-system fix-up
// (ConvertStagePostProcess applied this on the converted root).
// ---------------------------------------------------------------------

IDTX_CORE_API idtx_axis_t idtx_scene_get_up_axis(const idtx_scene_t* scene);          // default Y
IDTX_CORE_API double      idtx_scene_get_meters_per_unit(const idtx_scene_t* scene);  // default 0.01

// ---------------------------------------------------------------------
// Tree access. Nodes are a flat array in depth-first order (parents before
// children, matching the converter's traversal); the tree is reconstructed
// from each node's parent index.
// ---------------------------------------------------------------------

IDTX_CORE_API int32_t      idtx_scene_get_node_count(const idtx_scene_t* scene);
IDTX_CORE_API idtx_node_t* idtx_scene_get_node(const idtx_scene_t* scene, int32_t index);

IDTX_CORE_API int32_t          idtx_node_get_parent(const idtx_node_t* node);   // -1 = scene root
IDTX_CORE_API idtx_node_kind_t idtx_node_get_kind(const idtx_node_t* node);
IDTX_CORE_API const char*      idtx_node_get_name(const idtx_node_t* node);     // prim name
IDTX_CORE_API const char*      idtx_node_get_path(const idtx_node_t* node);     // full USD prim path
IDTX_CORE_API void             idtx_node_get_local_transform(const idtx_node_t* node, float out_matrix[16]);

// ---------------------------------------------------------------------
// Shared visual payload (display color/opacity + interpolation + material).
// Display colors are RGBA (rgb from displayColor, a from displayOpacity).
// ---------------------------------------------------------------------

IDTX_CORE_API int32_t             idtx_node_get_material_index(const idtx_node_t* node);   // scene material slot, -1 = none
IDTX_CORE_API int32_t             idtx_node_get_display_color_count(const idtx_node_t* node);
IDTX_CORE_API void                idtx_node_get_display_colors(const idtx_node_t* node, float* out_rgba); // count*4 floats
IDTX_CORE_API idtx_color_interp_t idtx_node_get_color_interpolation(const idtx_node_t* node);

// Scene-wide material table (mesh/primitive nodes index into this).
IDTX_CORE_API int32_t          idtx_scene_get_material_count(const idtx_scene_t* scene);
IDTX_CORE_API idtx_material_t* idtx_scene_get_material(const idtx_scene_t* scene, int32_t index);

// ---------------------------------------------------------------------
// Per-kind payload. Each getter is valid ONLY when idtx_node_get_kind() is the
// matching kind; behaviour is undefined otherwise.
// ---------------------------------------------------------------------

// IDTX_NODE_CUBE
IDTX_CORE_API double idtx_node_get_cube_size(const idtx_node_t* node);

// IDTX_NODE_CONE / IDTX_NODE_CYLINDER — radius, height, and the authored axis
// (the local transform already bakes the axis rotation; `axis` is informational).
IDTX_CORE_API void idtx_node_get_cone(const idtx_node_t* node, double* out_radius, double* out_height, idtx_axis_t* out_axis);
IDTX_CORE_API void idtx_node_get_cylinder(const idtx_node_t* node, double* out_radius, double* out_height, idtx_axis_t* out_axis);

// IDTX_NODE_SPHERE
IDTX_CORE_API double idtx_node_get_sphere_radius(const idtx_node_t* node);

// IDTX_NODE_MESH — borrowed mesh handle (reuse the existing idtx_mesh getters).
IDTX_CORE_API idtx_mesh_t* idtx_node_get_mesh(const idtx_node_t* node);

// IDTX_NODE_SKELETON — borrowed skeleton handle + its skinned mesh (or NULL).
IDTX_CORE_API idtx_skeleton_t* idtx_node_get_skeleton(const idtx_node_t* node);
IDTX_CORE_API idtx_mesh_t*     idtx_node_get_skinned_mesh(const idtx_node_t* node);

// IDTX_NODE_COLLISION — shape + authored axis/height/radius, plus the
// interaction-type token list (static/grab/etc.).
IDTX_CORE_API void    idtx_node_get_collision(const idtx_node_t* node, idtx_collision_shape_t* out_shape,
                                              idtx_axis_t* out_axis, double* out_height, double* out_radius);
IDTX_CORE_API int32_t     idtx_node_get_collision_type_count(const idtx_node_t* node);
IDTX_CORE_API const char* idtx_node_get_collision_type(const idtx_node_t* node, int32_t index);

// IDTX_NODE_COLLISION_ROOT — interaction metadata. `out_identifier` borrows.
IDTX_CORE_API void idtx_node_get_collision_root(const idtx_node_t* node, float out_highlight_color3[3],
                                                const char** out_identifier, int32_t* out_enabled, int32_t* out_highlightable);

#ifdef __cplusplus
}
#endif

#endif // IDTX_CORE_IDTX_SCENE_H
