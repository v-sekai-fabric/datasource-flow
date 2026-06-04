"""
V-Sekai pre-export RNA → id_property mirror for Blender's USD exporter.

Copyright 2026 The openusd-fabric authors.
SPDX-License-Identifier: MIT

The companion to blender/post_export_hook.py. Blender's USD exporter
surfaces id_properties as USD attributes under the `userProperties:`
namespace but does NOT surface RNA PointerProperty trees from third-
party addons (vrm-addon-for-blender's `mtoon1`, `spring_bone1`, etc).
This script bridges that gap: walks the VRM addon's RNA state and
mirrors the parts the V-Sekai schema cares about into id_properties
on the same datablocks. Blender's exporter then carries them through
USD; the post-export hook reads them back and applies the schema.

Two entry points:

* CLI form, headless and CI-friendly:
      blender --background <project.blend> --python blender/pre_export_mirror.py
  No script arguments needed — operates on the open file.

* In-process form, called from an export workflow:
      from openusd_fabric_blender import pre_export_mirror
      counts = pre_export_mirror.mirror_all()
      bpy.ops.wm.usd_export(filepath="/tmp/out.usda")

The mirror is idempotent: id_properties are overwritten each call, so
running it twice produces the same .blend state. Deleting the RNA-side
source clears the id_property on the next run.

Detection contract (what's mirrored, source → id_property):

  material.vrm_addon_extension.mtoon1.enabled = True
    → material["v_sekai:mtoon"] = 1
    + material["v_sekai:mtoon:<field>"] = <value> for every field the
      schema knows (shadeColorFactor, shadingShiftFactor, etc).

  armature.data.vrm_addon_extension.spring_bone1.spring_bones[i]
    → bone-empty (or armature bone) ["v_sekai:springBone"] = 1
    + dynamics fields mirrored as v_sekai:springBone:<field>.

  armature.data.vrm_addon_extension.spring_bone1.colliders[i]
    → bone-empty["v_sekai:springBoneCollider"] = 1
    + shape fields mirrored as v_sekai:springBone:collider:<field>.

The RNA paths track vrm-addon-for-blender's master branch as of
2026. If upstream renames a property, the mirror silently skips it
(via getattr with a default); the post-export hook then sees an
unmarked prim and falls back to schema defaults.
"""

from __future__ import annotations

import sys
from typing import Any, Iterable, Optional

try:
    import bpy
except ImportError as exc:
    raise SystemExit(
        "bpy not importable. Run this script inside Blender "
        "(blender --background ... --python pre_export_mirror.py)."
    ) from exc


# Property names live on the addon's RNA structs. Keeping them in lists
# rather than hardcoded per-call lets future upstream renames be a
# one-line patch — and the post-export hook's schema-type map drives
# what eventually lands on USD.
_MTOON_RNA_FIELDS: list[str] = [
    "shade_color_factor",
    "shade_multiply_texture",
    "shading_shift_factor",
    "shading_shift_texture",
    "shading_shift_texture_scale",
    "shading_toony_factor",
    "gi_equalization_factor",
    "matcap_factor",
    "matcap_texture",
    "rim_multiply_texture",
    "rim_lighting_mix_factor",
    "parametric_rim_color_factor",
    "parametric_rim_fresnel_power_factor",
    "parametric_rim_lift_factor",
    "outline_width_mode",
    "outline_width_factor",
    "outline_width_multiply_texture",
    "outline_color_factor",
    "outline_lighting_mix_factor",
    "uv_animation_mask_texture",
    "uv_animation_scroll_x_speed_factor",
    "uv_animation_scroll_y_speed_factor",
    "uv_animation_rotation_speed_factor",
    "render_queue_offset_number",
    "transparent_with_z_write",
]

# Mapping from snake_case (Blender RNA) to camelCase (V-Sekai schema /
# VRMC_materials_mtoon). The post-export hook reads the camelCase form.
_SNAKE_TO_CAMEL_MTOON: dict[str, str] = {
    "shade_color_factor":              "shadeColorFactor",
    "shade_multiply_texture":          "shadeMultiplyTexture",
    "shading_shift_factor":            "shadingShiftFactor",
    "shading_shift_texture":           "shadingShiftTexture",
    "shading_shift_texture_scale":     "shadingShiftTextureScale",
    "shading_toony_factor":            "shadingToonyFactor",
    "gi_equalization_factor":          "giEqualizationFactor",
    "matcap_factor":                   "matcapFactor",
    "matcap_texture":                  "matcapTexture",
    "rim_multiply_texture":            "rimMultiplyTexture",
    "rim_lighting_mix_factor":         "rimLightingMixFactor",
    "parametric_rim_color_factor":     "parametricRimColorFactor",
    "parametric_rim_fresnel_power_factor": "parametricRimFresnelPowerFactor",
    "parametric_rim_lift_factor":      "parametricRimLiftFactor",
    "outline_width_mode":              "outlineWidthMode",
    "outline_width_factor":            "outlineWidthFactor",
    "outline_width_multiply_texture":  "outlineWidthMultiplyTexture",
    "outline_color_factor":            "outlineColorFactor",
    "outline_lighting_mix_factor":     "outlineLightingMixFactor",
    "uv_animation_mask_texture":       "uvAnimationMaskTexture",
    "uv_animation_scroll_x_speed_factor": "uvAnimationScrollXSpeedFactor",
    "uv_animation_scroll_y_speed_factor": "uvAnimationScrollYSpeedFactor",
    "uv_animation_rotation_speed_factor": "uvAnimationRotationSpeedFactor",
    "render_queue_offset_number":      "renderQueueOffsetNumber",
    "transparent_with_z_write":        "transparentWithZWrite",
}

_SPRING_DYNAMICS_FIELDS: dict[str, str] = {
    # snake → schema attribute suffix (after "v_sekai:springBone:")
    "stiffness":     "stiffness",
    "drag_force":    "drag",
    "gravity_power": "gravityPower",
    "gravity_dir":   "gravityDir",
    "hit_radius":    "hitRadius",
}

_COLLIDER_FIELDS: dict[str, tuple[str, str]] = {
    # snake → (schema attribute, shape-context "sphere"|"capsule"|"any")
    "shape":   ("shape",  "any"),
    "radius":  ("radius", "any"),
    "offset":  ("offset", "any"),
    "tail":    ("tail",   "capsule"),
    "normal":  ("normal", "any"),
    "inside":  ("inside", "any"),
}


def _set_id_property(datablock: Any, key: str, value: Any) -> None:
    """Set datablock[key] = value, handling Blender's RNA quirks.

    * None / missing source: clear the id_property if present, no-op
      otherwise. Keeps idempotency clean — a False marker round trip
      doesn't leave stale data.
    * Vector / Color: stored as a Python tuple of floats so Blender
      serialises it as a fixed-length array surfacing as VEC3/VEC4
      on the USD side.
    * Image / asset references: stored as the resolved filepath
      string. The USD exporter then surfaces it as an Sdf.AssetPath
      with the resolver baking in.
    """
    if value is None:
        if key in datablock.keys():
            del datablock[key]
        return
    if hasattr(value, "filepath"):  # bpy.types.Image / similar
        datablock[key] = value.filepath
        return
    if hasattr(value, "__len__") and not isinstance(value, (str, bytes)):
        datablock[key] = tuple(float(c) for c in value)
        return
    if isinstance(value, bool):
        datablock[key] = 1 if value else 0
        return
    datablock[key] = value


def _vrm_addon_ext(datablock: Any) -> Optional[Any]:
    """Return the vrm-addon-for-blender extension namespace on the
    datablock, or None if the addon isn't installed / hasn't touched
    this datablock. Tolerant of both the master-branch and earlier
    naming conventions."""
    return getattr(datablock, "vrm_addon_extension", None)


def _mirror_mtoon_material(mat: Any) -> bool:
    """Mirror RNA MToon state to id_properties on `mat`. Returns True
    when the material was actually flagged as MToon-enabled, False
    when it isn't (so caller can count for the summary print)."""
    ext = _vrm_addon_ext(mat)
    if ext is None:
        return False
    mtoon = getattr(ext, "mtoon1", None)
    if mtoon is None or not getattr(mtoon, "enabled", False):
        # Not MToon — clear any stale marker from a previous run so
        # the post-export hook doesn't latch onto an old flag.
        for key in list(mat.keys()):
            if key.startswith("v_sekai:mtoon"):
                del mat[key]
        return False

    mat["v_sekai:mtoon"] = 1
    extensions = getattr(mtoon, "extensions", None)
    inner = getattr(extensions, "vrmc_materials_mtoon", None) if extensions else None
    src = inner if inner is not None else mtoon
    for snake in _MTOON_RNA_FIELDS:
        camel = _SNAKE_TO_CAMEL_MTOON[snake]
        value = getattr(src, snake, None)
        _set_id_property(mat, "v_sekai:mtoon:" + camel, value)
    return True


def _mirror_springbone_chain(armature: Any, chain: Any) -> tuple[int, int]:
    """Stamp v_sekai:springBone markers on the bones the chain's joints
    point at. Returns (joint_count, marker_count).

    Each VRM 0.x / 1.0 spring chain has a list of `joints` referencing
    Blender bones by name. The pre-export step mirrors the chain's
    dynamics onto every referenced bone's pose / id-property bag.
    Blender's USD exporter then surfaces those id_properties on the
    UsdSkel joint or the equivalent Xform sibling.
    """
    joints = getattr(chain, "joints", [])
    if not joints:
        return (0, 0)
    n_marked = 0
    for joint in joints:
        bone_name = getattr(joint, "node", None)
        if bone_name is None:
            # vrm-addon-for-blender exposes the bone reference as a
            # bpy_prop_bone_name struct — try `.bone_name` next.
            bone_name = getattr(joint, "bone_name", None)
        if bone_name is None:
            continue
        bone = armature.data.bones.get(str(bone_name))
        if bone is None:
            continue
        bone["v_sekai:springBone"] = 1
        for snake, schema_suffix in _SPRING_DYNAMICS_FIELDS.items():
            value = getattr(chain, snake, None)
            if value is None:
                value = getattr(joint, snake, None)
            _set_id_property(bone, "v_sekai:springBone:" + schema_suffix, value)
        n_marked += 1
    return (len(joints), n_marked)


def _mirror_collider(armature: Any, collider: Any) -> bool:
    """Stamp v_sekai:springBoneCollider on the bone the collider
    attaches to. Returns True when a bone was found and marked."""
    bone_name = getattr(collider, "node", None)
    if bone_name is None:
        bone_name = getattr(collider, "bone_name", None)
    if bone_name is None:
        return False
    bone = armature.data.bones.get(str(bone_name))
    if bone is None:
        return False
    bone["v_sekai:springBoneCollider"] = 1
    shape_obj = getattr(collider, "shape", collider)
    for snake, (suffix, ctx) in _COLLIDER_FIELDS.items():
        if ctx == "capsule" and not getattr(shape_obj, "is_capsule", False):
            continue
        value = getattr(shape_obj, snake, None)
        if value is None:
            value = getattr(collider, snake, None)
        _set_id_property(bone, "v_sekai:springBone:collider:" + suffix, value)
    return True


def _iter_armatures() -> Iterable[Any]:
    for obj in bpy.data.objects:
        if obj.type == "ARMATURE":
            yield obj


def mirror_all() -> dict[str, int]:
    """Run the full mirror across the open .blend. Returns counts
    grouped by mirror category — useful for CI assertions and the
    CLI summary line."""
    counts = {
        "materials_marked": 0,
        "chains_marked":    0,
        "joints_marked":    0,
        "colliders_marked": 0,
    }

    for mat in bpy.data.materials:
        if _mirror_mtoon_material(mat):
            counts["materials_marked"] += 1

    for armature_obj in _iter_armatures():
        ext = _vrm_addon_ext(armature_obj.data)
        if ext is None:
            continue
        sb = getattr(ext, "spring_bone1", None)
        if sb is None:
            continue
        for chain in getattr(sb, "springs", getattr(sb, "spring_bones", [])):
            n_joints, n_marked = _mirror_springbone_chain(armature_obj, chain)
            if n_marked > 0:
                counts["chains_marked"] += 1
                counts["joints_marked"] += n_marked
        for collider in getattr(sb, "colliders", []):
            if _mirror_collider(armature_obj, collider):
                counts["colliders_marked"] += 1

        # Side-channel blob: bone id_properties don't survive Blender's
        # USD export (joints are tokens in an array, not separate prims
        # — UsdSkel design, see openusd.org/dev/api/_usd_skel__schemas
        # .html). Workaround: serialise the chain + collider table as
        # JSON onto the Armature OBJECT's id_properties, which Blender
        # DOES surface as userProperties:* on the corresponding USD
        # Xform. The post-export hook reads it and synthesises sibling
        # Xform prims under the SkelRoot the way
        # idtx_core_export_avatar_to_usd would.
        _stamp_spring_config_blob(armature_obj, sb)
        if (counts["chains_marked"] > 0 or counts["colliders_marked"] > 0):
            counts.setdefault("blob_stamped", 0)
            counts["blob_stamped"] += 1

    return counts


def _stamp_spring_config_blob(armature_obj: Any, sb: Any) -> None:
    """Serialise the addon's spring_bone1 state into a JSON id_property
    on the Armature OBJECT. The post-export hook reads
    userProperties:v_sekai:springBoneConfig (Blender surfaces id_
    properties on Xform-equivalent objects directly) and synthesises
    the missing per-bone Xform structure on the USD side.
    """
    import json

    def _from_joint_or_chain(j: Any, chain: Any, attr: str, default: Any) -> Any:
        # Match _mirror_springbone_chain: prefer chain-level value when
        # present (older vrm-addon shape), fall back to joint (newer
        # shape). This way bone markers and the blob agree.
        v = getattr(chain, attr, None)
        if v is None: v = getattr(j, attr, None)
        return default if v is None else v

    # vrm-addon stores collider / collider_group cross-references as
    # UUIDs (not names) on the addon-internal list entries:
    #   group.colliders[i].collider_uuid       -> sb.colliders[*].uuid
    #   chain.collider_groups[i].collider_group_uuid
    #                                          -> sb.collider_groups[*].uuid
    # Build lookup tables so we can resolve those back to display names
    # before writing the side-channel blob. The user-facing name lives
    # in a DIFFERENT attribute per type — `display_name` on collider,
    # `vrm_name` on collider_group. Older addon versions used `name` /
    # `vrm_name` consistently; we read all three with fallbacks.
    def _collider_name(c) -> str:
        for attr in ("vrm_name", "display_name", "name"):
            v = getattr(c, attr, None)
            if v: return str(v)
        return ""
    def _group_name(g) -> str:
        for attr in ("vrm_name", "display_name", "name"):
            v = getattr(g, attr, None)
            if v: return str(v)
        return ""

    collider_uuid_to_name: dict[str, str] = {}
    for col in getattr(sb, "colliders", []):
        u = getattr(col, "uuid", None)
        if u: collider_uuid_to_name[str(u)] = _collider_name(col)

    group_uuid_to_name: dict[str, str] = {}
    for grp in getattr(sb, "collider_groups", []):
        u = getattr(grp, "uuid", None)
        if u: group_uuid_to_name[str(u)] = _group_name(grp)

    chains: list[dict] = []
    for chain in getattr(sb, "springs", getattr(sb, "spring_bones", [])):
        joints_data: list[dict] = []
        for j in getattr(chain, "joints", []):
            ref = getattr(j, "node", None)
            bone_name = getattr(ref, "bone_name", None) if not isinstance(ref, str) else ref
            if bone_name is None: continue
            joints_data.append({
                "bone":         str(bone_name),
                "stiffness":    float(_from_joint_or_chain(j, chain, "stiffness",     1.0)),
                "drag":         float(_from_joint_or_chain(j, chain, "drag_force",    0.4)),
                "gravityPower": float(_from_joint_or_chain(j, chain, "gravity_power", 0.0)),
                "gravityDir":   _vec3_or_default(
                    _from_joint_or_chain(j, chain, "gravity_dir", None), (0.0, -1.0, 0.0)),
                "hitRadius":    float(_from_joint_or_chain(j, chain, "hit_radius",    0.02)),
            })
        if not joints_data: continue
        # Collider-group references — resolve UUID -> vrm_name, fall
        # back to the legacy `collider_group_name` attribute on older
        # addon versions.
        group_refs: list[str] = []
        for gref in getattr(chain, "collider_groups", []):
            uuid = getattr(gref, "collider_group_uuid", None)
            if uuid and str(uuid) in group_uuid_to_name:
                group_refs.append(group_uuid_to_name[str(uuid)])
                continue
            legacy = getattr(gref, "collider_group_name", None)
            if legacy: group_refs.append(str(legacy))
        chains.append({
            "name":          _group_name(chain),
            "joints":        joints_data,
            "colliderGroups": group_refs,
        })

    colliders_data: list[dict] = []
    for col in getattr(sb, "colliders", []):
        ref = getattr(col, "node", None)
        bone_name = getattr(ref, "bone_name", None) if not isinstance(ref, str) else ref
        if bone_name is None: continue
        shape = getattr(col, "shape", None)
        sphere = getattr(shape, "sphere", None) if shape else None
        capsule = getattr(shape, "capsule", None) if shape else None
        entry: dict = {
            "name":         _collider_name(col),
            "attachedBone": str(bone_name),
        }
        # vrm-addon's actual discriminator: `col.shape_type` is an
        # EnumProperty with values "Sphere" / "Capsule". Both
        # `shape.sphere` AND `shape.capsule` sub-objects ALWAYS exist
        # (default-constructed) regardless of which the author picked,
        # so a `capsule.tail is not None` check misclassifies every
        # sphere as a capsule.
        is_capsule = str(getattr(col, "shape_type", "Sphere")).lower() == "capsule"
        if is_capsule and capsule is not None:
            entry["shape"]  = "capsule"
            entry["radius"] = float(getattr(capsule, "radius", 0.05))
            entry["offset"] = _vec3_or_default(getattr(capsule, "offset", None), (0.0, 0.0, 0.0))
            entry["tail"]   = _vec3_or_default(getattr(capsule, "tail",   None), (0.0, 0.0, 0.0))
        else:
            entry["shape"]  = "sphere"
            entry["radius"] = float(getattr(sphere, "radius", 0.05) if sphere else 0.05)
            entry["offset"] = _vec3_or_default(
                getattr(sphere, "offset", None) if sphere else None, (0.0, 0.0, 0.0))
        colliders_data.append(entry)

    groups_data: list[dict] = []
    for group in getattr(sb, "collider_groups", []):
        members: list[str] = []
        for entry in getattr(group, "colliders", []):
            uuid = getattr(entry, "collider_uuid", None)
            if uuid and str(uuid) in collider_uuid_to_name:
                members.append(collider_uuid_to_name[str(uuid)])
                continue
            legacy = getattr(entry, "collider_name", None)
            if legacy: members.append(str(legacy))
        groups_data.append({
            "name":      _group_name(group),
            "colliders": members,
        })

    if not chains and not colliders_data:
        # Clear any stale blob from a prior run.
        if "v_sekai:springBoneConfig" in armature_obj.keys():
            del armature_obj["v_sekai:springBoneConfig"]
        return

    payload = json.dumps({
        "chains":         chains,
        "colliders":      colliders_data,
        "colliderGroups": groups_data,
    }, separators=(",", ":"))
    armature_obj["v_sekai:springBoneConfig"] = payload


def _vec3_or_default(v: Any, default: tuple[float, float, float]) -> list[float]:
    if v is None:
        return list(default)
    try:
        return [float(v[0]), float(v[1]), float(v[2])]
    except (TypeError, IndexError):
        return list(default)


def main() -> int:
    """CLI entry point. Idempotent: re-running just re-mirrors."""
    counts = mirror_all()
    summary = ", ".join(f"{name}={n}" for name, n in counts.items())
    print(f"openusd-fabric pre-export mirror: {summary}")
    # When invoked headlessly we usually want the .blend updated in
    # place so the subsequent wm.usd_export call sees the markers.
    if bpy.data.is_saved:
        bpy.ops.wm.save_mainfile()
    return 0


if __name__ == "__main__":
    sys.exit(main())
