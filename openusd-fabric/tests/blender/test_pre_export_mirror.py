# Copyright 2026 The openusd-fabric authors.
# SPDX-License-Identifier: MIT
#
# Tests for blender/pre_export_mirror.py via a synthetic bpy mock.
# We don't need a real Blender install — the mirror only walks
# bpy.data.materials, bpy.data.objects (armatures), and id_property
# bags, so a tiny dict-backed shim covers it.
#
# The contract under test:
#   * MToon-enabled materials get `v_sekai:mtoon` set + a fan of
#     `v_sekai:mtoon:<field>` from the RNA tree.
#   * Spring-bone chains stamp `v_sekai:springBone` on referenced bones
#     with dynamics mirrored.
#   * Colliders stamp `v_sekai:springBoneCollider` with shape fields.
#   * The mirror is idempotent (running it twice produces the same
#     state) and clears markers when the RNA source disables itself.

from __future__ import annotations

import sys
import types
from pathlib import Path
from typing import Any

import pytest

# Make blender/pre_export_mirror.py importable. We register a synthetic
# `bpy` module BEFORE the import so the script's `import bpy` resolves
# against the shim rather than failing the way it does outside Blender.
_REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(_REPO / "blender"))


class _IdPropBag(dict):
    """dict subclass that mimics Blender's id_property API enough for
    the mirror's `datablock[key] = value` + `key in datablock.keys()`
    + `del datablock[key]` calls."""
    def keys(self):
        return list(super().keys())


class _Datablock:
    """A namespace + id_property bag rolled together."""
    def __init__(self, name: str = "") -> None:
        self.name = name
        self._props = _IdPropBag()

    def __getitem__(self, k):       return self._props[k]
    def __setitem__(self, k, v):    self._props[k] = v
    def __delitem__(self, k):       del self._props[k]
    def __contains__(self, k):      return k in self._props
    def keys(self):                 return self._props.keys()


class _Bone(_Datablock):
    pass


class _BoneDict:
    """bpy_prop_collection-like that resolves bones by name."""
    def __init__(self) -> None: self._d: dict[str, _Bone] = {}
    def add(self, name: str) -> _Bone:
        b = _Bone(name)
        self._d[name] = b
        return b
    def get(self, name): return self._d.get(name)


class _Armature(_Datablock):
    def __init__(self, name: str = "Armature") -> None:
        super().__init__(name)
        self.bones = _BoneDict()
        # vrm_addon_extension lives on the armature DATA, not the object.
        self.vrm_addon_extension = types.SimpleNamespace(spring_bone1=None)


class _ArmatureObject(_Datablock):
    """Blender's Armature OBJECT (not the underlying data). The pre-
    export mirror stamps the side-channel spring config blob on THIS,
    not the armature data — so it needs to be a dict-like id_property
    bag the same way materials/bones are."""
    def __init__(self, armature: _Armature) -> None:
        super().__init__(armature.name)
        self.type = "ARMATURE"
        self.data = armature


class _MockBpy(types.ModuleType):
    def __init__(self) -> None:
        super().__init__("bpy")
        self.data = types.SimpleNamespace(
            materials=[],
            objects=[],
            is_saved=False,  # avoid save_mainfile call
        )
        self.ops = types.SimpleNamespace(
            wm=types.SimpleNamespace(save_mainfile=lambda: None))
        self.types = types.SimpleNamespace()


@pytest.fixture
def bpy_mock(monkeypatch):
    mock = _MockBpy()
    monkeypatch.setitem(sys.modules, "bpy", mock)
    # Force a reload so pre_export_mirror picks up the mock if cached.
    if "pre_export_mirror" in sys.modules:
        del sys.modules["pre_export_mirror"]
    return mock


def test_mtoon_material_marked(bpy_mock):
    import pre_export_mirror

    mat = _Datablock("Face_MToon")
    mtoon = types.SimpleNamespace(
        enabled=True,
        shade_color_factor=(0.5, 0.5, 0.5),
        shading_toony_factor=0.9,
        outline_width_mode="worldCoordinates",
        outline_width_factor=0.02,
    )
    # All other fields read as None via getattr default.
    mat.vrm_addon_extension = types.SimpleNamespace(
        mtoon1=mtoon)
    bpy_mock.data.materials.append(mat)

    counts = pre_export_mirror.mirror_all()
    assert counts["materials_marked"] == 1
    assert mat["v_sekai:mtoon"] == 1
    # Color tuple stored as floats
    assert mat["v_sekai:mtoon:shadeColorFactor"] == (0.5, 0.5, 0.5)
    assert mat["v_sekai:mtoon:shadingToonyFactor"] == 0.9
    assert mat["v_sekai:mtoon:outlineWidthMode"] == "worldCoordinates"
    assert mat["v_sekai:mtoon:outlineWidthFactor"] == 0.02


def test_disabled_mtoon_clears_stale_markers(bpy_mock):
    import pre_export_mirror

    mat = _Datablock("Body_PBR")
    # Pretend a previous run set the markers.
    mat["v_sekai:mtoon"] = 1
    mat["v_sekai:mtoon:shadeColorFactor"] = (0.5, 0.5, 0.5)
    # Current RNA state: NOT MToon (enabled=False).
    mat.vrm_addon_extension = types.SimpleNamespace(
        mtoon1=types.SimpleNamespace(enabled=False))
    bpy_mock.data.materials.append(mat)

    counts = pre_export_mirror.mirror_all()
    assert counts["materials_marked"] == 0
    assert "v_sekai:mtoon" not in mat
    assert "v_sekai:mtoon:shadeColorFactor" not in mat


def test_springbone_chain_stamps_bones(bpy_mock):
    import pre_export_mirror

    arm = _Armature()
    bone_a = arm.bones.add("Hair_L_0")
    bone_b = arm.bones.add("Hair_L_1")

    chain = types.SimpleNamespace(
        stiffness=1.0, drag_force=0.4, gravity_power=0.0,
        gravity_dir=(0.0, -1.0, 0.0), hit_radius=0.02,
        joints=[
            types.SimpleNamespace(node="Hair_L_0"),
            types.SimpleNamespace(node="Hair_L_1"),
        ])
    arm.vrm_addon_extension.spring_bone1 = types.SimpleNamespace(
        spring_bones=[chain], colliders=[])
    bpy_mock.data.objects.append(_ArmatureObject(arm))

    counts = pre_export_mirror.mirror_all()
    assert counts["chains_marked"] == 1
    assert counts["joints_marked"] == 2
    for bone in (bone_a, bone_b):
        assert bone["v_sekai:springBone"] == 1
        assert bone["v_sekai:springBone:stiffness"] == 1.0
        assert bone["v_sekai:springBone:drag"] == 0.4
        assert bone["v_sekai:springBone:hitRadius"] == 0.02
        assert bone["v_sekai:springBone:gravityDir"] == (0.0, -1.0, 0.0)


def test_collider_stamped(bpy_mock):
    import pre_export_mirror

    arm = _Armature()
    head = arm.bones.add("Head")
    collider = types.SimpleNamespace(
        node="Head",
        shape=types.SimpleNamespace(
            shape="sphere", radius=0.05, offset=(0.0, 0.05, 0.0),
            is_capsule=False,
        ))
    arm.vrm_addon_extension.spring_bone1 = types.SimpleNamespace(
        spring_bones=[], colliders=[collider])
    bpy_mock.data.objects.append(_ArmatureObject(arm))

    counts = pre_export_mirror.mirror_all()
    assert counts["colliders_marked"] == 1
    assert head["v_sekai:springBoneCollider"] == 1
    assert head["v_sekai:springBone:collider:shape"] == "sphere"
    assert head["v_sekai:springBone:collider:radius"] == 0.05
    assert head["v_sekai:springBone:collider:offset"] == (0.0, 0.05, 0.0)
    # tail is capsule-only, must not appear on a sphere collider.
    assert "v_sekai:springBone:collider:tail" not in head


def test_idempotent(bpy_mock):
    import pre_export_mirror

    mat = _Datablock("Face_MToon")
    mat.vrm_addon_extension = types.SimpleNamespace(
        mtoon1=types.SimpleNamespace(
            enabled=True, shade_color_factor=(0.1, 0.2, 0.3)))
    bpy_mock.data.materials.append(mat)

    c1 = pre_export_mirror.mirror_all()
    snapshot = dict(mat._props)
    c2 = pre_export_mirror.mirror_all()
    assert c1 == c2
    assert dict(mat._props) == snapshot


def test_addon_not_installed_is_no_op(bpy_mock):
    import pre_export_mirror
    mat = _Datablock("Material")
    bpy_mock.data.materials.append(mat)
    counts = pre_export_mirror.mirror_all()
    assert counts["materials_marked"] == 0
    assert list(mat.keys()) == []


# ---- Side-channel spring config blob stamping -----------------------
#
# UsdSkel joints are tokens in an array, not prims — Blender's USD
# export can't carry per-bone id_properties through. The mirror's
# workaround is to serialise the addon's spring_bone1 state into a
# JSON id_property on the Armature OBJECT, which DOES surface as a
# userProperties:* attribute. The post-export hook then parses it and
# synthesises sibling Xform prims.

def _make_armature_with_springs(arm_name="Armature"):
    """Build an Armature object whose RNA state has one chain (2 joints)
    + one sphere collider + one collider group referencing both."""
    arm = _Armature(arm_name)
    arm.bones.add("Hair_L_0")
    arm.bones.add("Hair_L_1")
    arm.bones.add("Head")
    arm.bones.add("Chest")

    def _node_ref(bone_name: str):
        # vrm-addon-for-blender exposes the joint's bone reference as
        # a struct with a .bone_name field. The blob writer reads that.
        return types.SimpleNamespace(bone_name=bone_name)

    chain = types.SimpleNamespace(
        vrm_name="Hair.L",
        joints=[
            types.SimpleNamespace(
                node=_node_ref("Hair_L_0"),
                stiffness=1.0, drag_force=0.4, gravity_power=0.0,
                gravity_dir=(0.0, -1.0, 0.0), hit_radius=0.02),
            types.SimpleNamespace(
                node=_node_ref("Hair_L_1"),
                stiffness=0.8, drag_force=0.5, gravity_power=0.1,
                gravity_dir=(0.0, -1.0, 0.0), hit_radius=0.03),
        ],
        collider_groups=[
            types.SimpleNamespace(collider_group_name="HeadGroup")])

    head_collider = types.SimpleNamespace(
        vrm_name="Head",
        node=_node_ref("Head"),
        shape=types.SimpleNamespace(
            sphere=types.SimpleNamespace(
                radius=0.10, offset=(0.0, 0.05, 0.0)),
            capsule=types.SimpleNamespace(tail=None)))

    grp = types.SimpleNamespace(
        vrm_name="HeadGroup",
        colliders=[types.SimpleNamespace(collider_name="Head")])

    # The full spring_bone1 namespace (both `springs` and `spring_bones`
    # accessors work — the mirror tries `springs` first).
    arm.vrm_addon_extension.spring_bone1 = types.SimpleNamespace(
        springs=[chain], spring_bones=[chain],
        colliders=[head_collider], collider_groups=[grp])
    return arm


def test_spring_config_blob_stamped(bpy_mock):
    import json
    import pre_export_mirror

    arm = _make_armature_with_springs()
    arm_obj = _ArmatureObject(arm)
    bpy_mock.data.objects.append(arm_obj)

    pre_export_mirror.mirror_all()
    blob = arm_obj["v_sekai:springBoneConfig"]
    config = json.loads(blob)

    assert len(config["chains"]) == 1
    chain = config["chains"][0]
    assert chain["name"] == "Hair.L"
    assert [j["bone"] for j in chain["joints"]] == ["Hair_L_0", "Hair_L_1"]
    assert chain["joints"][0]["stiffness"] == 1.0
    assert chain["joints"][1]["drag"] == 0.5
    assert chain["colliderGroups"] == ["HeadGroup"]

    assert len(config["colliders"]) == 1
    col = config["colliders"][0]
    assert col["attachedBone"] == "Head"
    assert col["shape"] == "sphere"
    assert col["radius"] == 0.10
    assert col["offset"] == [0.0, 0.05, 0.0]

    assert len(config["colliderGroups"]) == 1
    assert config["colliderGroups"][0]["colliders"] == ["Head"]


def test_spring_config_blob_cleared_when_empty(bpy_mock):
    import pre_export_mirror

    arm = _Armature()
    # Pretend a previous run left a blob behind.
    arm_obj = _ArmatureObject(arm)
    arm_obj["v_sekai:springBoneConfig"] = '{"chains":[],"colliders":[]}'
    # Current RNA state: addon present but no chains / colliders.
    arm.vrm_addon_extension.spring_bone1 = types.SimpleNamespace(
        springs=[], colliders=[], collider_groups=[])
    bpy_mock.data.objects.append(arm_obj)

    pre_export_mirror.mirror_all()
    assert "v_sekai:springBoneConfig" not in arm_obj
