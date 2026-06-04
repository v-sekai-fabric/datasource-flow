# Copyright 2026 The openusd-fabric authors.
# SPDX-License-Identifier: MIT
#
# Tests for blender/post_export_hook.py. We don't need a real Blender
# install — the hook reads userProperties:* attributes from a .usda
# and stamps V-Sekai API schemas + their attributes. The fixture below
# builds a synthetic stage that mimics what Blender would produce after
# the V-Sekai pre-export RNA-to-id_property mirror has run.

from __future__ import annotations

import sys
from pathlib import Path

import pytest

# Make blender/post_export_hook.py importable as a module.
_REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(_REPO / "blender"))

try:
    from pxr import Sdf, Usd, UsdGeom, UsdShade
except ImportError:
    pytest.skip("pxr (OpenUSD Python bindings) not available", allow_module_level=True)

import post_export_hook  # noqa: E402


@pytest.fixture(autouse=True, scope="module")
def _register_schema():
    """USD only consults PXR_PLUGINPATH_NAME at plugin-registry warmup,
    which happens on the first UsdStage::Open. Set it before any test
    in this module touches a stage.
    """
    post_export_hook.ensure_plugin_path()


def _make_stage(path: Path) -> Usd.Stage:
    stage = Usd.Stage.CreateNew(str(path))
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)
    UsdGeom.SetStageMetersPerUnit(stage, 1.0)

    # Avatar root.
    root = UsdGeom.Xform.Define(stage, "/Avatar")
    stage.SetDefaultPrim(root.GetPrim())

    # A material that the pre-export mirror has marked as MToon, with
    # representative MToon factor values stored under userProperties:*.
    mat_path = "/Avatar/Materials/Face_MToon"
    mat = UsdShade.Material.Define(stage, mat_path)
    mp = mat.GetPrim()
    mp.CreateAttribute("userProperties:v_sekai:mtoon", Sdf.ValueTypeNames.Bool).Set(True)
    mp.CreateAttribute("userProperties:v_sekai:mtoon:shadeColorFactor",
                       Sdf.ValueTypeNames.Color3f).Set((0.5, 0.5, 0.5))
    mp.CreateAttribute("userProperties:v_sekai:mtoon:shadingToonyFactor",
                       Sdf.ValueTypeNames.Float).Set(0.9)
    mp.CreateAttribute("userProperties:v_sekai:mtoon:outlineWidthMode",
                       Sdf.ValueTypeNames.Token).Set("worldCoordinates")

    # A material that is NOT marked — should be left alone.
    plain_mat = UsdShade.Material.Define(stage, "/Avatar/Materials/Body_PBR")

    # A springbone-root Xform sibling of the Skeleton.
    sb_root = UsdGeom.Xform.Define(stage, "/Avatar/SpringBones/Hair_L_0")
    sbp = sb_root.GetPrim()
    sbp.CreateAttribute("userProperties:v_sekai:springBone",
                        Sdf.ValueTypeNames.Bool).Set(True)
    sbp.CreateAttribute("userProperties:v_sekai:springBone:stiffness",
                        Sdf.ValueTypeNames.Float).Set(1.0)
    sbp.CreateAttribute("userProperties:v_sekai:springBone:drag",
                        Sdf.ValueTypeNames.Float).Set(0.4)
    sbp.CreateAttribute("userProperties:v_sekai:springBone:hitRadius",
                        Sdf.ValueTypeNames.Float).Set(0.02)

    # A collider Xform sibling.
    col = UsdGeom.Xform.Define(stage, "/Avatar/SpringBones/Head_Collider")
    cp = col.GetPrim()
    cp.CreateAttribute("userProperties:v_sekai:springBoneCollider",
                       Sdf.ValueTypeNames.Bool).Set(True)
    cp.CreateAttribute("userProperties:v_sekai:springBone:collider:shape",
                       Sdf.ValueTypeNames.Token).Set("sphere")
    cp.CreateAttribute("userProperties:v_sekai:springBone:collider:radius",
                       Sdf.ValueTypeNames.Float).Set(0.05)

    stage.GetRootLayer().Save()
    return stage


def test_mtoon_api_stamped(tmp_path: Path) -> None:
    p = tmp_path / "stage.usda"
    _make_stage(p)
    stage = Usd.Stage.Open(str(p))
    counts = post_export_hook.apply_v_sekai_schemas(stage)

    mat_prim = stage.GetPrimAtPath("/Avatar/Materials/Face_MToon")
    assert mat_prim.HasAPI("VSekaiMToonAPI"), "VSekaiMToonAPI not applied to marked material"
    plain_prim = stage.GetPrimAtPath("/Avatar/Materials/Body_PBR")
    assert not plain_prim.HasAPI("VSekaiMToonAPI"), "VSekaiMToonAPI applied to unmarked material"
    assert counts["VSekaiMToonAPI"] == 1, f"unexpected MToon count: {counts}"


def test_mtoon_attributes_stamped(tmp_path: Path) -> None:
    p = tmp_path / "stage.usda"
    _make_stage(p)
    stage = Usd.Stage.Open(str(p))
    post_export_hook.apply_v_sekai_schemas(stage)

    mat_prim = stage.GetPrimAtPath("/Avatar/Materials/Face_MToon")
    shade = mat_prim.GetAttribute("v_sekai:mtoon:shadeColorFactor")
    assert shade and shade.IsValid(), "v_sekai:mtoon:shadeColorFactor missing"
    sx, sy, sz = shade.Get()
    assert (sx, sy, sz) == (0.5, 0.5, 0.5), f"shadeColorFactor value drifted: {shade.Get()}"

    toony = mat_prim.GetAttribute("v_sekai:mtoon:shadingToonyFactor")
    assert toony and toony.Get() == pytest.approx(0.9)

    outline = mat_prim.GetAttribute("v_sekai:mtoon:outlineWidthMode")
    assert outline and str(outline.Get()) == "worldCoordinates"


def test_springbone_api_stamped(tmp_path: Path) -> None:
    p = tmp_path / "stage.usda"
    _make_stage(p)
    stage = Usd.Stage.Open(str(p))
    counts = post_export_hook.apply_v_sekai_schemas(stage)

    sb = stage.GetPrimAtPath("/Avatar/SpringBones/Hair_L_0")
    assert sb.HasAPI("VSekaiSpringBoneAPI")
    assert sb.GetAttribute("v_sekai:springBone:stiffness").Get() == pytest.approx(1.0)
    assert sb.GetAttribute("v_sekai:springBone:drag").Get() == pytest.approx(0.4)
    assert sb.GetAttribute("v_sekai:springBone:hitRadius").Get() == pytest.approx(0.02)
    assert counts["VSekaiSpringBoneAPI"] == 1


def test_collider_api_stamped(tmp_path: Path) -> None:
    p = tmp_path / "stage.usda"
    _make_stage(p)
    stage = Usd.Stage.Open(str(p))
    counts = post_export_hook.apply_v_sekai_schemas(stage)

    col = stage.GetPrimAtPath("/Avatar/SpringBones/Head_Collider")
    assert col.HasAPI("VSekaiSpringBoneColliderAPI")
    assert str(col.GetAttribute("v_sekai:springBone:collider:shape").Get()) == "sphere"
    assert col.GetAttribute("v_sekai:springBone:collider:radius").Get() == pytest.approx(0.05)
    assert counts["VSekaiSpringBoneColliderAPI"] == 1


def test_idempotent_reapply(tmp_path: Path) -> None:
    """Running the hook twice must not change the result."""
    p = tmp_path / "stage.usda"
    _make_stage(p)
    stage = Usd.Stage.Open(str(p))
    counts_a = post_export_hook.apply_v_sekai_schemas(stage)
    counts_b = post_export_hook.apply_v_sekai_schemas(stage)
    assert counts_a == counts_b, "hook is not idempotent"


def test_falsy_marker_skips(tmp_path: Path) -> None:
    """A marker attribute set to False / 0 / '' must NOT trigger application."""
    p = tmp_path / "stage.usda"
    _make_stage(p)
    stage = Usd.Stage.Open(str(p))
    # Mark the plain material with mtoon=False — must NOT pick it up.
    plain = stage.GetPrimAtPath("/Avatar/Materials/Body_PBR")
    plain.CreateAttribute("userProperties:v_sekai:mtoon",
                          Sdf.ValueTypeNames.Bool).Set(False)
    counts = post_export_hook.apply_v_sekai_schemas(stage)
    assert not plain.HasAPI("VSekaiMToonAPI")
    assert counts["VSekaiMToonAPI"] == 1, "False marker should not trigger application"


# ---- Side-channel spring config blob synthesis ----------------------
#
# The pre-export mirror writes a JSON blob to a v_sekai:springBoneConfig
# id_property on the Armature object — surfaces as a userProperties:*
# string attribute on the corresponding USD Xform prim. The hook parses
# it and synthesises sibling Xform prims under the SkelRoot, since
# UsdSkel joints are tokens in an array and can't carry API schemas
# directly. See post_export_hook._synthesise_spring_prims_from_blob.

def _make_stage_with_armature(path: Path, blob: str) -> Usd.Stage:
    """Minimal stage: Avatar / Armature (with the config blob) / SkelRoot
    with a Skeleton inside. The Skeleton's `joints` array carries the
    bone names the blob references — the synthesis resolves those to
    int indices."""
    stage = Usd.Stage.CreateNew(str(path))
    root = UsdGeom.Xform.Define(stage, "/Avatar")
    stage.SetDefaultPrim(root.GetPrim())
    arm = UsdGeom.Xform.Define(stage, "/Avatar/Armature")
    arm.GetPrim().CreateAttribute(
        "userProperties:v_sekai:springBoneConfig",
        Sdf.ValueTypeNames.String).Set(blob)
    from pxr import UsdSkel as _UsdSkel, Gf as _Gf
    sr = _UsdSkel.Root.Define(stage, "/Avatar/Skel_SkelRoot")
    skel = _UsdSkel.Skeleton.Define(stage, "/Avatar/Skel_SkelRoot/Skeleton")
    # Bones the synthesis blob will reference. Hair_Back_0/1 are the
    # spring chain, Head + Chest are the collider anchors. The order
    # determines the int indices the synthesis resolves to.
    skel.CreateJointsAttr(
        ["Hips", "Hips/Spine", "Hips/Spine/Chest", "Hips/Spine/Chest/Head",
         "Hair_Back_0", "Hair_Back_0/Hair_Back_1"])
    ident = _Gf.Matrix4d(1)
    skel.CreateBindTransformsAttr([ident] * 6)
    skel.CreateRestTransformsAttr([ident] * 6)
    stage.GetRootLayer().Save()
    return stage


_REPRESENTATIVE_BLOB = """{
  "chains":[
    {"name":"Hair.Back","joints":[
      {"bone":"Hair_Back_0","stiffness":1.0,"drag":0.4,"gravityPower":0.0,
       "gravityDir":[0.0,-1.0,0.0],"hitRadius":0.02},
      {"bone":"Hair_Back_1","stiffness":1.0,"drag":0.4,"gravityPower":0.0,
       "gravityDir":[0.0,-1.0,0.0],"hitRadius":0.02}
    ],"colliderGroups":["HeadGroup"]}
  ],
  "colliders":[
    {"name":"Head","attachedBone":"Head","shape":"sphere",
     "radius":0.10,"offset":[0.0,0.05,0.0]},
    {"name":"ChestCap","attachedBone":"Chest","shape":"capsule",
     "radius":0.08,"offset":[0.0,0.0,0.0],"tail":[0.0,0.2,0.0]}
  ],
  "colliderGroups":[
    {"name":"HeadGroup","colliders":["Head","ChestCap"]}
  ]
}"""


def test_spring_synthesis_fires_when_blob_present(tmp_path: Path) -> None:
    p = tmp_path / "stage.usda"
    _make_stage_with_armature(p, _REPRESENTATIVE_BLOB)
    stage = Usd.Stage.Open(str(p))
    counts = post_export_hook.apply_v_sekai_schemas(stage)

    assert counts.get("springChains_synthesised") == 1
    assert counts.get("springColliders_synthesised") == 2
    # The synthesised chain prim should exist + carry the API.
    chain = stage.GetPrimAtPath("/Avatar/Skel_SkelRoot/SpringBones/Chain_Hair_Back")
    assert chain.IsValid(), "chain prim was not synthesised at the expected path"
    assert chain.HasAPI("VSekaiSpringBoneAPI"), "VSekaiSpringBoneAPI not applied"
    # idtx_core's importer reads int[] joint indices — Hair_Back_0 is
    # joint #4 in our fixture skeleton, Hair_Back_1 is #5.
    joints = chain.GetAttribute("v_sekai:springBone:joints").Get()
    assert list(joints) == [4, 5]
    # Provenance: bone names preserved on a custom token[] for debug.
    names = chain.GetAttribute("v_sekai:springBone:jointNames").Get()
    assert list(names) == ["Hair_Back_0", "Hair_Back_1"]
    assert chain.GetAttribute("v_sekai:springBone:stiffness").Get() == pytest.approx(1.0)


def test_spring_synthesis_no_op_without_blob(tmp_path: Path) -> None:
    p = tmp_path / "stage.usda"
    # Create a stage WITHOUT the blob attribute.
    stage = Usd.Stage.CreateNew(str(p))
    UsdGeom.Xform.Define(stage, "/Avatar")
    from pxr import UsdSkel as _UsdSkel
    _UsdSkel.Root.Define(stage, "/Avatar/Skel_SkelRoot")
    stage.GetRootLayer().Save()
    stage = Usd.Stage.Open(str(p))
    counts = post_export_hook.apply_v_sekai_schemas(stage)
    assert counts.get("springChains_synthesised", 0) == 0
    assert counts.get("springColliders_synthesised", 0) == 0
    # No SpringBones scope should have been created.
    assert not stage.GetPrimAtPath("/Avatar/Skel_SkelRoot/SpringBones").IsValid()


def test_spring_synthesis_collider_attrs(tmp_path: Path) -> None:
    p = tmp_path / "stage.usda"
    _make_stage_with_armature(p, _REPRESENTATIVE_BLOB)
    stage = Usd.Stage.Open(str(p))
    post_export_hook.apply_v_sekai_schemas(stage)

    head = stage.GetPrimAtPath("/Avatar/Skel_SkelRoot/SpringBones/Collider_Head")
    assert head.IsValid()
    assert head.HasAPI("VSekaiSpringBoneColliderAPI")
    # attachedBone is an int index into the Skeleton's joints.
    # Head is joint #3 in our fixture skeleton.
    assert head.GetAttribute("v_sekai:springBone:collider:attachedBone").Get() == 3
    assert (str(head.GetAttribute("v_sekai:springBone:collider:attachedBoneName").Get())
            == "Head")
    assert str(head.GetAttribute("v_sekai:springBone:collider:shape").Get()) == "sphere"
    assert head.GetAttribute("v_sekai:springBone:collider:radius").Get() == pytest.approx(0.10)
    # Sphere — no tail value should be AUTHORED. The codeless-schema
    # registration declares the attr with a default, so .IsValid() is
    # True; HasAuthoredValue() distinguishes "set by us" from
    # "schema-default visible only".
    tail_attr = head.GetAttribute("v_sekai:springBone:collider:tail")
    assert not tail_attr.HasAuthoredValue(), (
        "sphere collider unexpectedly carries an authored tail")

    chest = stage.GetPrimAtPath("/Avatar/Skel_SkelRoot/SpringBones/Collider_ChestCap")
    assert chest.IsValid()
    assert str(chest.GetAttribute("v_sekai:springBone:collider:shape").Get()) == "capsule"
    tail = chest.GetAttribute("v_sekai:springBone:collider:tail").Get()
    # Vec3f is float32 — exact-equality against double literals would
    # fail on round-trip noise (0.2 → 0.20000000298023224 etc).
    assert tuple(tail) == pytest.approx((0.0, 0.2, 0.0), abs=1e-6)


def test_spring_synthesis_collider_groups(tmp_path: Path) -> None:
    p = tmp_path / "stage.usda"
    _make_stage_with_armature(p, _REPRESENTATIVE_BLOB)
    stage = Usd.Stage.Open(str(p))
    post_export_hook.apply_v_sekai_schemas(stage)

    container = stage.GetPrimAtPath("/Avatar/Skel_SkelRoot/SpringBones")
    grp = container.GetAttribute("v_sekai:springBone:colliderGroup:HeadGroup").Get()
    assert list(grp) == ["Head", "ChestCap"]

    chain = stage.GetPrimAtPath("/Avatar/Skel_SkelRoot/SpringBones/Chain_Hair_Back")
    chain_grps = chain.GetAttribute("v_sekai:springBone:colliderGroups").Get()
    assert list(chain_grps) == ["HeadGroup"]


def test_spring_synthesis_flattens_colliders(tmp_path: Path) -> None:
    """The C ABI has no collider-group concept — the importer reads
    a flat v_sekai:springBone:colliders token[] on each chain. The
    hook must expand the chain's colliderGroups references into that
    flat list while ALSO preserving the structured colliderGroups
    attribute for round-trip transparency."""
    p = tmp_path / "stage.usda"
    _make_stage_with_armature(p, _REPRESENTATIVE_BLOB)
    stage = Usd.Stage.Open(str(p))
    post_export_hook.apply_v_sekai_schemas(stage)

    chain = stage.GetPrimAtPath("/Avatar/Skel_SkelRoot/SpringBones/Chain_Hair_Back")
    # Indices into the collider walk-order. The fixture's HeadGroup
    # holds [Head, ChestCap] — emitted first/second, so [0, 1].
    flat = chain.GetAttribute("v_sekai:springBone:colliders").Get()
    assert list(flat) == [0, 1]
    # Provenance side-channel keeps the names for debugging.
    names = chain.GetAttribute("v_sekai:springBone:colliderNames").Get()
    assert list(names) == ["Head", "ChestCap"]


def test_spring_synthesis_bad_json_doesnt_crash(tmp_path: Path) -> None:
    p = tmp_path / "stage.usda"
    _make_stage_with_armature(p, "{ this is not json")
    stage = Usd.Stage.Open(str(p))
    counts = post_export_hook.apply_v_sekai_schemas(stage)
    assert counts.get("springConfig_parse_errors") == 1
    assert counts.get("springChains_synthesised", 0) == 0
