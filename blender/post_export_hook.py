"""
V-Sekai post-export hook for Blender's USD exporter.

Copyright 2026 The openusd-fabric authors.
SPDX-License-Identifier: MIT

Blender's native USD exporter does not apply custom API schemas, so this
hook runs after the export to stamp the V-Sekai schemas onto the right
prims and write back the v_sekai:* attributes that downstream tools
(idtx-flow for Godot, the schema mapper for Unity) consume.

Two entry points:

* CLI form, headless and CI-friendly:
      blender --background --python blender/post_export_hook.py -- \
          --in path/to/exported.usda
  Arguments past `--` are read via sys.argv after Blender strips its own
  flags. The script edits the file in place by default; pass --out to
  write a separate stage.

* In-process form, called from a Python addon's `wm.usd_export`
  post-handler:
      from openusd_fabric_blender import post_export_hook
      post_export_hook.apply_v_sekai_schemas(stage)

Plugin discovery: USD looks at PXR_PLUGINPATH_NAME for codeless schemas.
This script auto-sets it to ../schema relative to its own location if the
variable is unset, so the standalone CLI form works without any wrapper.

Phase 1 scope (CHI-251):
* Apply VSekaiMToonAPI to material prims flagged as MToon.
* Apply VSekaiSpringBoneAPI to joints flagged as springbone roots.
* Apply VSekaiSpringBoneColliderAPI to joints flagged as colliders.

Detection of "flagged as ..." is currently a stub — the rules live with
the Blender side of the V-Sekai authoring pipeline and will fill in as
the asset conventions stabilise. The plumbing here is what gets reused;
the predicates are what change.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path
from typing import Iterable, Optional

try:
    from pxr import Sdf, Usd, UsdGeom, UsdShade, UsdSkel
except ImportError as exc:
    raise SystemExit(
        "pxr (OpenUSD Python bindings) is not importable. Run this script "
        "from inside Blender (blender --background --python ...) or from a "
        "Python environment where `pip install usd-core` succeeded."
    ) from exc


SCHEMA_DIR_DEFAULT = (Path(__file__).resolve().parent.parent / "schema")

# Markers and namespaces. The V-Sekai authoring convention is a tiny
# pre-export step (an operator or post-handler in the V-Sekai branch of
# vrm-addon-for-blender) that mirrors the addon's RNA-pointer-property
# state into Blender id_properties on the source datablock. Blender's USD
# exporter then surfaces those id_properties as USD attributes under the
# `userProperties:` namespace, which is what the hook reads here. The
# raw `vrm_addon_extension.mtoon1.enabled` PointerProperty does NOT
# survive USD export on its own.
USER_PROPERTIES_PREFIX = "userProperties:"
MTOON_MARKER_ATTR = "userProperties:v_sekai:mtoon"
SPRINGBONE_MARKER_ATTR = "userProperties:v_sekai:springBone"
COLLIDER_MARKER_ATTR = "userProperties:v_sekai:springBoneCollider"

# Side-channel: the pre-export mirror stamps the addon's full
# spring_bone1 table as a JSON id_property on the Armature OBJECT
# (UsdSkel joints aren't prims, so per-bone id_properties can't
# survive Blender's USD export). The hook reads this attribute,
# parses the JSON, and synthesises sibling Xform prims under the
# corresponding SkelRoot — the layout idtx_core would have emitted
# directly. See pre_export_mirror.py _stamp_spring_config_blob.
#
# Wire format on the synthesised prims (matches idtx_core's
# USD exporter so the importer round-trips cleanly):
#   v_sekai:springBone:joints      = int[]   (joint indices into
#                                              the Skeleton's `joints`)
#   v_sekai:springBone:colliders   = int[]   (collider walk-order
#                                              indices under SpringBones)
#   v_sekai:springBone:collider:attachedBone = int (joint index)
# Bone-name / collider-name strings ride alongside on custom
# `*Names` / `*Name` token attributes for human / debug consumption.
SPRINGBONE_CONFIG_ATTR = "userProperties:v_sekai:springBoneConfig"

# MToon factor mirroring: every Blender-side id_property named
# `v_sekai:mtoon:<field>` is copied onto the USD prim as the matching
# schema attribute. The hook does not invent values — if a factor is not
# in the source customData, the schema default applies. Texture asset
# attributes share the same mapping; the value lives in the asset
# resolver path that Blender writes.
_MTOON_SCHEMA_TYPES: dict[str, Sdf.ValueTypeName] = {
    "v_sekai:mtoon:shadeColorFactor":              Sdf.ValueTypeNames.Color3f,
    "v_sekai:mtoon:shadeMultiplyTexture":          Sdf.ValueTypeNames.Asset,
    "v_sekai:mtoon:shadingShiftFactor":            Sdf.ValueTypeNames.Float,
    "v_sekai:mtoon:shadingShiftTexture":           Sdf.ValueTypeNames.Asset,
    "v_sekai:mtoon:shadingShiftTextureScale":      Sdf.ValueTypeNames.Float,
    "v_sekai:mtoon:shadingToonyFactor":            Sdf.ValueTypeNames.Float,
    "v_sekai:mtoon:giEqualizationFactor":          Sdf.ValueTypeNames.Float,
    "v_sekai:mtoon:matcapFactor":                  Sdf.ValueTypeNames.Color3f,
    "v_sekai:mtoon:matcapTexture":                 Sdf.ValueTypeNames.Asset,
    "v_sekai:mtoon:rimMultiplyTexture":            Sdf.ValueTypeNames.Asset,
    "v_sekai:mtoon:rimLightingMixFactor":          Sdf.ValueTypeNames.Float,
    "v_sekai:mtoon:parametricRimColorFactor":      Sdf.ValueTypeNames.Color3f,
    "v_sekai:mtoon:parametricRimFresnelPowerFactor": Sdf.ValueTypeNames.Float,
    "v_sekai:mtoon:parametricRimLiftFactor":       Sdf.ValueTypeNames.Float,
    "v_sekai:mtoon:outlineWidthMode":              Sdf.ValueTypeNames.Token,
    "v_sekai:mtoon:outlineWidthFactor":            Sdf.ValueTypeNames.Float,
    "v_sekai:mtoon:outlineWidthMultiplyTexture":   Sdf.ValueTypeNames.Asset,
    "v_sekai:mtoon:outlineColorFactor":            Sdf.ValueTypeNames.Color3f,
    "v_sekai:mtoon:outlineLightingMixFactor":      Sdf.ValueTypeNames.Float,
    "v_sekai:mtoon:uvAnimationMaskTexture":        Sdf.ValueTypeNames.Asset,
    "v_sekai:mtoon:uvAnimationScrollXSpeedFactor": Sdf.ValueTypeNames.Float,
    "v_sekai:mtoon:uvAnimationScrollYSpeedFactor": Sdf.ValueTypeNames.Float,
    "v_sekai:mtoon:uvAnimationRotationSpeedFactor": Sdf.ValueTypeNames.Float,
    "v_sekai:mtoon:renderQueueOffsetNumber":       Sdf.ValueTypeNames.Int,
    "v_sekai:mtoon:transparentWithZWrite":         Sdf.ValueTypeNames.Bool,
}

_SPRINGBONE_SCHEMA_TYPES: dict[str, Sdf.ValueTypeName] = {
    "v_sekai:springBone:stiffness":     Sdf.ValueTypeNames.Float,
    "v_sekai:springBone:drag":          Sdf.ValueTypeNames.Float,
    "v_sekai:springBone:gravityPower":  Sdf.ValueTypeNames.Float,
    "v_sekai:springBone:gravityDir":    Sdf.ValueTypeNames.Vector3f,
    "v_sekai:springBone:hitRadius":     Sdf.ValueTypeNames.Float,
    # `center` and `colliders` are USD relationships, not attributes; they
    # are stamped via _stamp_springbone_relationships below because the
    # userProperties→attribute mirror does not understand relationships.
}

_COLLIDER_SCHEMA_TYPES: dict[str, Sdf.ValueTypeName] = {
    "v_sekai:springBone:collider:shape":  Sdf.ValueTypeNames.Token,
    "v_sekai:springBone:collider:radius": Sdf.ValueTypeNames.Float,
    "v_sekai:springBone:collider:offset": Sdf.ValueTypeNames.Vector3f,
    "v_sekai:springBone:collider:tail":   Sdf.ValueTypeNames.Vector3f,
    "v_sekai:springBone:collider:normal": Sdf.ValueTypeNames.Vector3f,
    "v_sekai:springBone:collider:inside": Sdf.ValueTypeNames.Bool,
}


def ensure_plugin_path(schema_dir: Path = SCHEMA_DIR_DEFAULT) -> None:
    """Prepend the V-Sekai schema directory to PXR_PLUGINPATH_NAME if absent.

    USD only consults PXR_PLUGINPATH_NAME at plugin-registry warmup, which
    happens on the first UsdStage::Open or Usd.Stage.Open call. This must
    run before any stage is opened in the process, hence module top of
    every callsite calling into apply_v_sekai_schemas().
    """
    schema_dir = schema_dir.resolve()
    if not (schema_dir / "plugInfo.json").exists():
        raise FileNotFoundError(
            f"V-Sekai plugInfo.json not found at {schema_dir}. "
            "Pass --schema-dir or set PXR_PLUGINPATH_NAME manually."
        )
    current = os.environ.get("PXR_PLUGINPATH_NAME", "")
    parts = [p for p in current.split(os.pathsep) if p]
    if str(schema_dir) in parts:
        return
    parts.insert(0, str(schema_dir))
    os.environ["PXR_PLUGINPATH_NAME"] = os.pathsep.join(parts)


def _iter_material_prims(stage: Usd.Stage) -> Iterable[Usd.Prim]:
    for prim in stage.Traverse():
        if prim.IsA(UsdShade.Material):
            yield prim


def _iter_marked_prims(stage: Usd.Stage, marker_attr: str) -> Iterable[Usd.Prim]:
    """Yield every prim whose `marker_attr` userProperty resolves truthy.

    The V-Sekai authoring convention models springbone roots and
    colliders as ordinary Xform prims (siblings of the Skeleton) rather
    than as Skeleton-internal joints, because USD joints are entries in
    a `joints` token array on the Skeleton prim and cannot carry API
    schemas directly. The pre-export step writes the joint-equivalent
    Xform plus the marker id_property; the hook only needs to find it.
    """
    for prim in stage.Traverse():
        if _truthy_user_property(prim, marker_attr):
            yield prim


def _truthy_user_property(prim: Usd.Prim, attr_name: str) -> bool:
    """Return True iff prim has attr_name (or its non-prefixed variant)
    with a non-zero / non-empty value.

    Blender's USD exporter historically wrote id_properties under
    `userProperties:` (e.g. `userProperties:v_sekai:mtoon`); Blender
    4.x writes them as raw attributes without the prefix
    (`v_sekai:mtoon`). The hook accepts both so the same id_property
    convention round-trips across Blender versions.
    """
    attr = prim.GetAttribute(attr_name)
    if (not attr or not attr.IsValid()) and attr_name.startswith(USER_PROPERTIES_PREFIX):
        # Fall back to the un-prefixed form for Blender 4.x output.
        attr = prim.GetAttribute(attr_name[len(USER_PROPERTIES_PREFIX):])
    if not attr or not attr.IsValid():
        return False
    value = attr.Get()
    if value is None:
        return False
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    if isinstance(value, str):
        return bool(value)
    return True


def _is_mtoon_material(prim: Usd.Prim) -> bool:
    """Predicate: should this material carry VSekaiMToonAPI?

    Looks for the V-Sekai authoring marker `userProperties:v_sekai:mtoon`,
    emitted by the V-Sekai pre-export step that mirrors
    `material.vrm_addon_extension.mtoon1.enabled` (from
    vrm-addon-for-blender) into a Blender id_property. The id_property
    survives Blender's USD export under the userProperties namespace;
    the RNA PointerProperty itself does not.
    """
    return _truthy_user_property(prim, MTOON_MARKER_ATTR)


def _is_springbone_root(prim: Usd.Prim) -> bool:
    """Predicate: should this prim carry VSekaiSpringBoneAPI?

    Looks for `userProperties:v_sekai:springBone`, the marker the V-Sekai
    pre-export step mirrors from `armature.data.vrm_addon_extension
    .spring_bone1.spring_bones[i].joints[0]` (the chain root) into a
    Blender id_property on the corresponding bone / empty.
    """
    return _truthy_user_property(prim, SPRINGBONE_MARKER_ATTR)


def _is_springbone_collider(prim: Usd.Prim) -> bool:
    """Predicate: should this prim carry VSekaiSpringBoneColliderAPI?

    Looks for `userProperties:v_sekai:springBoneCollider`, mirrored from
    `spring_bone1.colliders[i]` into a Blender id_property on the
    collider's representative bone / empty.
    """
    return _truthy_user_property(prim, COLLIDER_MARKER_ATTR)


def _apply_api(prim: Usd.Prim, api_name: str) -> None:
    """Apply a single-apply API schema to a prim if not already applied.

    Uses the generic ApplyAPI(TfType) path because the codeless schema
    has no generated C++ class to call ApplyAPI(VSekaiMToonAPI) on.
    """
    if prim.HasAPI(api_name):
        return
    prim.ApplyAPI(api_name)


def _copy_user_property_to_schema_attr(
    prim: Usd.Prim,
    schema_attr_name: str,
    schema_type: Sdf.ValueTypeName,
) -> bool:
    """Mirror a `userProperties:<schema_attr_name>` value onto schema_attr_name.

    Returns True if a value was copied (source attribute present), False
    otherwise. Texture-asset values are passed through unchanged — Blender
    writes them as Sdf.AssetPath instances with the resolved path baked
    in, which is exactly what idtx-flow and the Unity mapper expect.
    """
    source = prim.GetAttribute(USER_PROPERTIES_PREFIX + schema_attr_name)
    if not source or not source.IsValid():
        # Blender 4.x writes id_properties as raw attributes without
        # the userProperties: prefix; fall back to that form.
        source = prim.GetAttribute(schema_attr_name)
    if not source or not source.IsValid():
        return False
    value = source.Get()
    if value is None:
        return False
    target = prim.CreateAttribute(schema_attr_name, schema_type)
    target.Set(value)
    return True


def _stamp_mtoon_attrs(prim: Usd.Prim) -> int:
    n = 0
    for attr_name, attr_type in _MTOON_SCHEMA_TYPES.items():
        if _copy_user_property_to_schema_attr(prim, attr_name, attr_type):
            n += 1
    return n


def _stamp_springbone_attrs(prim: Usd.Prim) -> int:
    n = 0
    for attr_name, attr_type in _SPRINGBONE_SCHEMA_TYPES.items():
        if _copy_user_property_to_schema_attr(prim, attr_name, attr_type):
            n += 1
    return n


def _stamp_collider_attrs(prim: Usd.Prim) -> int:
    n = 0
    for attr_name, attr_type in _COLLIDER_SCHEMA_TYPES.items():
        if _copy_user_property_to_schema_attr(prim, attr_name, attr_type):
            n += 1
    return n


_SAFE_NAME_RE = re.compile(r"[^A-Za-z0-9_]")


def _sanitise_prim_name(name: str, fallback: str) -> str:
    """USD prim names must match Tf_IsValidIdentifier. Strip anything
    outside [A-Za-z0-9_] and prepend an underscore if the result
    starts with a digit. Empty names fall back to `fallback`."""
    if not name:
        return fallback
    s = _SAFE_NAME_RE.sub("_", name)
    if not s:
        return fallback
    if s[0].isdigit():
        s = "_" + s
    return s


def _find_armature_config_blobs(stage: Usd.Stage) -> Iterable[tuple[Usd.Prim, str]]:
    """Yield (armature_prim, json_blob) for every prim carrying the
    spring-bone config side-channel attribute. Both userProperties:-
    prefixed (Blender pre-4.x) and raw (Blender 4.x) forms accepted."""
    for prim in stage.Traverse():
        attr = prim.GetAttribute(SPRINGBONE_CONFIG_ATTR)
        if not attr or not attr.IsValid():
            attr = prim.GetAttribute(SPRINGBONE_CONFIG_ATTR[len(USER_PROPERTIES_PREFIX):])
        if not attr or not attr.IsValid():
            continue
        value = attr.Get()
        if not value or not isinstance(value, str):
            continue
        yield prim, value


def _find_skelroot_for(armature_prim: Usd.Prim) -> Optional[Usd.Prim]:
    """Find the SkelRoot most likely associated with the given Armature
    prim. Blender's USD export puts the SkelRoot as either an ancestor,
    sibling, or descendant of the Armature object's prim. Search order:
    self → descendants → ancestors → siblings."""
    if armature_prim.IsA(UsdSkel.Root):
        return armature_prim
    for desc in Usd.PrimRange(armature_prim):
        if desc != armature_prim and desc.IsA(UsdSkel.Root):
            return desc
    cur = armature_prim.GetParent()
    while cur and cur.IsValid() and not cur.IsPseudoRoot():
        if cur.IsA(UsdSkel.Root):
            return cur
        for sib in cur.GetChildren():
            if sib != armature_prim and sib.IsA(UsdSkel.Root):
                return sib
            for desc in Usd.PrimRange(sib):
                if desc.IsA(UsdSkel.Root):
                    return desc
        cur = cur.GetParent()
    return None


def _find_skeleton_for(armature_prim: Usd.Prim, skelroot: Usd.Prim) -> Optional[Usd.Prim]:
    """Find the Skeleton the meshes inside `skelroot` actually bind to
    via their `skel:skeleton` relationship. This is the well-specified
    UsdSkel lookup
    (https://openusd.org/dev/api/_usd_skel__schemas.html#UsdSkel_Skeleton)
    — preferred over `armature_prim`'s subtree because Blender's USD
    exporter scatters multiple Skeleton prims (one per Mesh as a
    skinning proxy, plus the authoritative armature). Only the
    majority-bound one is the VRM skeleton."""
    # 1. Skeleton directly under armature_prim (Blender ARMATURE-object
    #    Xform with a child Skeleton — this is the canonical layout
    #    when there's a single armature with no proxies).
    for desc in Usd.PrimRange(armature_prim):
        if desc != armature_prim and desc.IsA(UsdSkel.Skeleton):
            return desc
    # 2. UsdSkel binding lookup: ask the meshes which Skeleton they're
    #    actually skinned to. Majority wins.
    from collections import Counter
    counts: Counter = Counter()
    for desc in Usd.PrimRange(skelroot):
        if not desc.IsA(UsdGeom.Mesh): continue
        rel = desc.GetRelationship("skel:skeleton")
        if not rel: continue
        for t in rel.GetTargets():
            counts[str(t)] += 1
    if counts:
        winner_path, _ = counts.most_common(1)[0]
        sk = skelroot.GetStage().GetPrimAtPath(winner_path)
        if sk.IsValid() and sk.IsA(UsdSkel.Skeleton):
            return sk
    # 3. Fallback: first Skeleton in tree order (degenerate fixtures
    #    with no skinned meshes at all).
    for desc in Usd.PrimRange(skelroot):
        if desc.IsA(UsdSkel.Skeleton):
            return desc
    return None


def _sanitise_bone_name(name: str) -> str:
    """Blender's USD exporter converts dot-separated bone names
    (`Hair.Back.001`) to underscore (`Hair_Back_001`) when writing
    them into UsdSkelSkeleton.joints[]. The blob the pre-export
    mirror writes carries the original RNA names; match against
    Skeleton joints uses the underscore form."""
    return name.replace(".", "_")


def _synthesise_spring_prims_from_blob(
    stage: Usd.Stage,
    armature_prim: Usd.Prim,
    blob: str,
    counts: dict[str, int],
) -> None:
    """Parse a v_sekai:springBoneConfig JSON blob and emit one Xform
    prim per chain + per collider under <SkelRoot>/SpringBones/, with
    the V-Sekai API schemas applied. Idempotent — re-running overwrites
    any prims already at the target paths."""
    try:
        config = json.loads(blob)
    except json.JSONDecodeError:
        counts["springConfig_parse_errors"] = counts.get("springConfig_parse_errors", 0) + 1
        return

    skelroot = _find_skelroot_for(armature_prim)
    if skelroot is None:
        counts["springConfig_no_skelroot"] = counts.get("springConfig_no_skelroot", 0) + 1
        return

    # idtx_core's importer reads spring-chain joints + colliders as
    # int[] indices (matching how idtx_core's USD exporter writes
    # them — see idtx_export_usd.cpp:376–390). Resolve bone-name
    # → joint index via the Skeleton's `joints` attribute (leaf
    # name of each Sdf path), and resolve collider-name → flat
    # walk-order index via a name table built as we emit colliders.
    bone_to_joint_idx: dict[str, int] = {}
    skel_prim = _find_skeleton_for(armature_prim, skelroot)
    if skel_prim is not None:
        joints_attr = skel_prim.GetAttribute("joints")
        if joints_attr and joints_attr.IsValid():
            for i, jp in enumerate(joints_attr.Get() or []):
                leaf = str(jp).rsplit("/", 1)[-1]
                bone_to_joint_idx[leaf] = i
                bone_to_joint_idx[str(jp)] = i  # also full-path form

    # Idempotency: if a prior hook invocation already populated the
    # SpringBones scope, wipe it first. Otherwise a second run would
    # stack synthesised prims on top of the old ones (USD's DefinePrim
    # is upsert-like for the parent but child prims accrete).
    container_path = skelroot.GetPath().AppendChild("SpringBones")
    existing = stage.GetPrimAtPath(container_path)
    if existing and existing.IsValid():
        for child in list(existing.GetChildren()):
            stage.RemovePrim(child.GetPath())
    container = stage.DefinePrim(container_path, "Scope")

    # Build a (group name → [collider name]) lookup so chains that
    # reference colliderGroups can be flattened to a per-chain
    # collider-name list, then mapped through the name→index table
    # we accumulate when emitting collider prims.
    group_lookup: dict[str, list[str]] = {}
    for grp in config.get("colliderGroups", []):
        gname = grp.get("name", "")
        if gname:
            group_lookup[gname] = [str(c) for c in grp.get("colliders", [])]

    collider_name_to_idx: dict[str, int] = {}

    used_names: set[str] = set()
    def _unique(base: str) -> str:
        # USD requires sibling-unique names; suffix on collision.
        candidate = base
        i = 1
        while candidate in used_names or container.GetChild(candidate):
            i += 1
            candidate = f"{base}_{i}"
        used_names.add(candidate)
        return candidate

    # Colliders FIRST: idtx_core's importer walks colliders before
    # chains (idtx_import_usd.cpp:553–561) so chains can reference
    # them by their walk-order index. We must do the same so the
    # name→index table we build matches the importer's view.
    for col in config.get("colliders", []):
        bone = col.get("attachedBone", "")
        if not bone: continue
        col_name_src = col.get("name") or bone
        name = _unique(_sanitise_prim_name(
            "Collider_" + col_name_src, "Collider"))
        prim = stage.DefinePrim(container_path.AppendChild(name), "Xform")
        _apply_api(prim, "VSekaiSpringBoneColliderAPI")
        counts["VSekaiSpringBoneColliderAPI"] += 1
        counts["springColliders_synthesised"] = counts.get("springColliders_synthesised", 0) + 1
        collider_name_to_idx[col_name_src] = len(collider_name_to_idx)
        # attachedBone is an int index into the Skeleton's `joints`.
        # Try the raw name first (case where Blender didn't mangle),
        # then the underscore-sanitised form (Blender's USD-export
        # convention).
        attached_idx = bone_to_joint_idx.get(str(bone),
                       bone_to_joint_idx.get(_sanitise_bone_name(str(bone)), -1))
        prim.CreateAttribute("v_sekai:springBone:collider:attachedBone",
            Sdf.ValueTypeNames.Int).Set(attached_idx)
        shape = str(col.get("shape", "sphere"))
        prim.CreateAttribute("v_sekai:springBone:collider:shape", Sdf.ValueTypeNames.Token
            ).Set(shape)
        prim.CreateAttribute("v_sekai:springBone:collider:radius", Sdf.ValueTypeNames.Float
            ).Set(float(col.get("radius", 0.05)))
        off = col.get("offset", [0.0, 0.0, 0.0])
        prim.CreateAttribute("v_sekai:springBone:collider:offset", Sdf.ValueTypeNames.Vector3f
            ).Set((float(off[0]), float(off[1]), float(off[2])))
        if shape == "capsule" and "tail" in col:
            tail = col["tail"]
            prim.CreateAttribute("v_sekai:springBone:collider:tail", Sdf.ValueTypeNames.Vector3f
                ).Set((float(tail[0]), float(tail[1]), float(tail[2])))
        # Provenance: keep the original bone name + collider name as
        # custom strings for human inspection / round-trip transparency.
        prim.CreateAttribute("v_sekai:springBone:collider:attachedBoneName",
            Sdf.ValueTypeNames.Token, custom=True).Set(str(bone))
        prim.CreateAttribute("v_sekai:springBone:collider:vrmName",
            Sdf.ValueTypeNames.Token, custom=True).Set(col_name_src)

    for chain in config.get("chains", []):
        joints = chain.get("joints", [])
        if not joints: continue
        name = _unique(_sanitise_prim_name("Chain_" + chain.get("name", ""), "Chain"))
        prim = stage.DefinePrim(container_path.AppendChild(name), "Xform")
        _apply_api(prim, "VSekaiSpringBoneAPI")
        counts["VSekaiSpringBoneAPI"] += 1
        counts["springChains_synthesised"] = counts.get("springChains_synthesised", 0) + 1
        # The chain's dynamics are joint-level in the addon. We use the
        # FIRST joint as the chain-level representative — matching how
        # VRM 1.0 / VRMC_springBone treats per-joint stiffness uniformly
        # across a chain in the common authoring case. Per-joint
        # divergence is captured in the parallel arrays below.
        first = joints[0]
        prim.CreateAttribute("v_sekai:springBone:stiffness", Sdf.ValueTypeNames.Float
            ).Set(float(first.get("stiffness", 1.0)))
        prim.CreateAttribute("v_sekai:springBone:drag", Sdf.ValueTypeNames.Float
            ).Set(float(first.get("drag", 0.4)))
        prim.CreateAttribute("v_sekai:springBone:gravityPower", Sdf.ValueTypeNames.Float
            ).Set(float(first.get("gravityPower", 0.0)))
        gd = first.get("gravityDir", [0.0, -1.0, 0.0])
        prim.CreateAttribute("v_sekai:springBone:gravityDir", Sdf.ValueTypeNames.Vector3f
            ).Set((float(gd[0]), float(gd[1]), float(gd[2])))
        prim.CreateAttribute("v_sekai:springBone:hitRadius", Sdf.ValueTypeNames.Float
            ).Set(float(first.get("hitRadius", 0.02)))
        # joints: int[] indices into the Skeleton's `joints` array.
        # Bones the resolver can't match drop out — better than a
        # poisoned -1 sentinel that would index out-of-bounds later.
        # Try raw + underscore-sanitised forms.
        joint_indices: list[int] = []
        joint_names_kept: list[str] = []
        for j in joints:
            bone_name = str(j.get("bone", ""))
            idx = bone_to_joint_idx.get(bone_name,
                  bone_to_joint_idx.get(_sanitise_bone_name(bone_name), -1))
            if idx >= 0:
                joint_indices.append(idx)
                joint_names_kept.append(bone_name)
        prim.CreateAttribute("v_sekai:springBone:joints", Sdf.ValueTypeNames.IntArray
            ).Set(joint_indices)
        # Provenance: keep the bone-name list as a custom token[]
        # so debugging tools can recover the authoring intent without
        # having to dereference the Skeleton joints array.
        prim.CreateAttribute("v_sekai:springBone:jointNames",
            Sdf.ValueTypeNames.TokenArray, custom=True).Set(joint_names_kept)
        # Per-joint divergence (matches the surviving joint set).
        # Same sanitise-then-lookup logic as the joint_indices loop above.
        def _idx_of(j_dict: dict) -> int:
            bn = str(j_dict.get("bone", ""))
            return bone_to_joint_idx.get(bn,
                   bone_to_joint_idx.get(_sanitise_bone_name(bn), -1))
        prim.CreateAttribute("v_sekai:springBone:perJointStiffness", Sdf.ValueTypeNames.FloatArray
            ).Set([float(j.get("stiffness",     first.get("stiffness",     1.0)))
                   for j in joints if _idx_of(j) >= 0])
        prim.CreateAttribute("v_sekai:springBone:perJointDrag", Sdf.ValueTypeNames.FloatArray
            ).Set([float(j.get("drag",          first.get("drag",          0.4)))
                   for j in joints if _idx_of(j) >= 0])
        prim.CreateAttribute("v_sekai:springBone:perJointHitRadius", Sdf.ValueTypeNames.FloatArray
            ).Set([float(j.get("hitRadius",     first.get("hitRadius",     0.02)))
                   for j in joints if _idx_of(j) >= 0])
        groups = chain.get("colliderGroups", [])
        if groups:
            prim.CreateAttribute("v_sekai:springBone:colliderGroups", Sdf.ValueTypeNames.TokenArray
                ).Set([str(g) for g in groups])
            # Flatten group refs → list of collider names → list of
            # int indices (de-duplicated, walk-order).
            seen: set[int] = set()
            col_indices: list[int] = []
            col_names_kept: list[str] = []
            for g in groups:
                for cname in group_lookup.get(str(g), []):
                    cidx = collider_name_to_idx.get(str(cname), -1)
                    if cidx >= 0 and cidx not in seen:
                        col_indices.append(cidx)
                        col_names_kept.append(str(cname))
                        seen.add(cidx)
            if col_indices:
                # The schema declares `v_sekai:springBone:colliders`
                # as a `rel` (relationship — for engine-side auto-
                # resolution to collider PRIMS). USD refuses to also
                # let us author an int[] attribute with the same
                # name, even with custom=True (the schema slot is
                # already typed). Use a sibling attribute name
                # `v_sekai:springBone:colliderIndices` for the wire
                # format the C ABI consumes (int indices into the
                # avatar's flat collider table). idtx_import_usd.cpp
                # reads this attribute name back.
                prim.CreateAttribute("v_sekai:springBone:colliderIndices",
                    Sdf.ValueTypeNames.IntArray, custom=True).Set(col_indices)
                prim.CreateAttribute("v_sekai:springBone:colliderNames",
                    Sdf.ValueTypeNames.TokenArray, custom=True).Set(col_names_kept)

    # Collider-groups: a list of named groups, each holding a list of
    # collider names. Emit as a single token[] array attribute keyed by
    # group name on the SpringBones scope itself.
    for grp in config.get("colliderGroups", []):
        gname = grp.get("name", "")
        members = grp.get("colliders", [])
        if not gname: continue
        safe = _sanitise_prim_name(gname, "Group")
        container.CreateAttribute(
            f"v_sekai:springBone:colliderGroup:{safe}",
            Sdf.ValueTypeNames.TokenArray).Set([str(m) for m in members])


def apply_v_sekai_schemas(stage: Usd.Stage) -> dict[str, int]:
    """Apply V-Sekai API schemas across the stage. Returns counts per API."""
    counts = {
        "VSekaiMToonAPI": 0,
        "VSekaiSpringBoneAPI": 0,
        "VSekaiSpringBoneColliderAPI": 0,
        "mtoon_attrs_stamped": 0,
    }

    for prim in _iter_material_prims(stage):
        if _is_mtoon_material(prim):
            _apply_api(prim, "VSekaiMToonAPI")
            counts["VSekaiMToonAPI"] += 1
            counts["mtoon_attrs_stamped"] += _stamp_mtoon_attrs(prim)

    # Side-channel synthesis FIRST: the JSON blob on the Armature prim
    # gets unpacked into sibling Xform prims under the SkelRoot. The
    # marker-attribute pass below then picks up any direct bone-prim
    # markers (which Blender pre-4.x produced for Empty-based rigs).
    for armature_prim, blob in _find_armature_config_blobs(stage):
        _synthesise_spring_prims_from_blob(stage, armature_prim, blob, counts)

    for prim in _iter_marked_prims(stage, SPRINGBONE_MARKER_ATTR):
        _apply_api(prim, "VSekaiSpringBoneAPI")
        counts["VSekaiSpringBoneAPI"] += 1
        _stamp_springbone_attrs(prim)

    for prim in _iter_marked_prims(stage, COLLIDER_MARKER_ATTR):
        _apply_api(prim, "VSekaiSpringBoneColliderAPI")
        counts["VSekaiSpringBoneColliderAPI"] += 1
        _stamp_collider_attrs(prim)

    return counts


def _user_args() -> list[str]:
    """Return CLI args trailing Blender's own flag block.

    Blender forwards anything after a literal `--` to the script. Inside a
    plain `python` invocation there is no `--` separator, so we fall back
    to sys.argv[1:].
    """
    if "--" in sys.argv:
        return sys.argv[sys.argv.index("--") + 1:]
    return sys.argv[1:]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("--in", dest="input_path", required=True,
                        help="Path to the .usda stage exported by Blender.")
    parser.add_argument("--out", dest="output_path", default=None,
                        help="Where to write the stamped stage. "
                             "Defaults to overwriting the input.")
    parser.add_argument("--schema-dir", dest="schema_dir", default=None,
                        help="Directory containing plugInfo.json. "
                             "Defaults to ../schema next to this script.")
    args = parser.parse_args(_user_args())

    schema_dir = Path(args.schema_dir) if args.schema_dir else SCHEMA_DIR_DEFAULT
    ensure_plugin_path(schema_dir)

    stage = Usd.Stage.Open(args.input_path)
    if stage is None:
        print(f"error: could not open {args.input_path}", file=sys.stderr)
        return 1

    counts = apply_v_sekai_schemas(stage)

    out_path = args.output_path or args.input_path
    if out_path == args.input_path:
        stage.GetRootLayer().Save()
    else:
        stage.GetRootLayer().Export(out_path)

    summary = ", ".join(f"{name}={n}" for name, n in counts.items())
    print(f"openusd-fabric: applied V-Sekai schemas → {summary} → {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
