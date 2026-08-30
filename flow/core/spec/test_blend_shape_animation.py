"""
Spec test for blend-shape weight animation tracks (IDTX_ANIM_TRACK_BLEND_WEIGHT).

Copyright 2026 V-Sekai contributors.
SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

Authors a minimal UsdSkel stage — one skinned quad, one blend shape "lean",
weights keyed 0 -> 1 -> 0 over 48 timecodes at 24tps — imports it through the
C ABI, and asserts the skeleton node's animation carries one BLEND_WEIGHT track
whose keys reproduce those values in seconds. The negative control strips the
weight samples and must yield only rest keys of 0; a shapeless rig must yield
no blend tracks at all; and the comparator itself is proven able to fail on a
planted wrong key.
"""

from __future__ import annotations

import ctypes
import os
import sys
from ctypes import POINTER, c_char_p, c_double, c_float, c_int32, c_void_p
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[3]
DLL_PATH = REPO / "build" / "idtx_core" / "libidtx_core.windows.x86_64.dll"

if not DLL_PATH.exists():
    pytest.skip(f"idtx_core DLL not built at {DLL_PATH}",
                allow_module_level=True)

if sys.platform == "win32":
    dep_dirs = [REPO / "flow" / "core" / "usd" / "libs" / "windows",
                REPO / "build" / "idtx_core"]
    for usd in sorted((REPO / "thirdparty").glob("openusd-*")):
        dep_dirs += [usd / "lib", usd / "bin"]
    for dep_dir in dep_dirs:
        if dep_dir.exists():
            os.add_dll_directory(str(dep_dir))

IDTX_NODE_SKELETON = 6  # keep in sync with idtx_node_kind_t
IDTX_ANIM_TRACK_BLEND_WEIGHT = 3


def write_stage(path: Path, animated: bool, shapes: bool = True) -> None:
    weights = ("float[] blendShapeWeights.timeSamples = {\n"
               "                0: [0],\n"
               "                24: [1],\n"
               "                48: [0],\n"
               "            }" if animated else
               "float[] blendShapeWeights = [0]")
    anim_shapes = "" if not shapes else (
        'uniform token[] blendShapes = ["lean"]\n            ' + weights + "\n            ")
    shape_block = "" if not shapes else """
        uniform token[] skel:blendShapes = ["lean"]
        rel skel:blendShapeTargets = [</Char/Quad/lean>]

        def BlendShape "lean"
        {
            uniform vector3f[] offsets = [(0,0,0), (0,0,0), (0,0,1), (0,0,1)]
            uniform int[] pointIndices = [0, 1, 2, 3]
        }"""
    path.write_text(f"""#usda 1.0
(
    endTimeCode = 48
    startTimeCode = 0
    timeCodesPerSecond = 24
    upAxis = "Y"
)

def SkelRoot "Char"
{{
    def Skeleton "Skel" (
        prepend apiSchemas = ["SkelBindingAPI"]
    )
    {{
        uniform matrix4d[] bindTransforms = [( (1,0,0,0), (0,1,0,0), (0,0,1,0), (0,0,0,1) )]
        uniform token[] joints = ["root"]
        uniform matrix4d[] restTransforms = [( (1,0,0,0), (0,1,0,0), (0,0,1,0), (0,0,0,1) )]
        rel skel:animationSource = </Char/Skel/Anim>

        def SkelAnimation "Anim"
        {{
            {anim_shapes}uniform token[] joints = ["root"]
            quatf[] rotations.timeSamples = {{ 0: [(1,0,0,0)], 48: [(1,0,0,0)] }}
            half3[] scales.timeSamples = {{ 0: [(1,1,1)], 48: [(1,1,1)] }}
            float3[] translations.timeSamples = {{ 0: [(0,0,0)], 48: [(0,0,0)] }}
        }}
    }}

    def Mesh "Quad" (
        prepend apiSchemas = ["SkelBindingAPI"]
    )
    {{
        point3f[] points = [(-1,0,0), (1,0,0), (1,2,0), (-1,2,0)]
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        rel skel:skeleton = </Char/Skel>
        int[] primvars:skel:jointIndices = [0, 0, 0, 0] (
            elementSize = 1
            interpolation = "vertex"
        )
        float[] primvars:skel:jointWeights = [1, 1, 1, 1] (
            elementSize = 1
            interpolation = "vertex"
        )
{shape_block}
    }}
}}
""", encoding="utf-8")


@pytest.fixture(scope="module")
def core():
    # The default loader ignores PATH for dependencies; preloading them by full
    # path makes the core resolvable regardless of environment.
    for dep in ("tbb12.dll", "usd_ms.dll", "libidtx_usd.dll"):
        for d in dep_dirs:
            cand = d / dep
            if cand.exists():
                ctypes.CDLL(str(cand))
                break
    lib = ctypes.CDLL(str(DLL_PATH))
    lib.idtx_core_import_scene_from_usd.restype = c_void_p
    lib.idtx_core_import_scene_from_usd.argtypes = [c_char_p]
    lib.idtx_core_scene_destroy.argtypes = [c_void_p]
    lib.idtx_scene_get_node_count.restype = c_int32
    lib.idtx_scene_get_node_count.argtypes = [c_void_p]
    lib.idtx_scene_get_node.restype = c_void_p
    lib.idtx_scene_get_node.argtypes = [c_void_p, c_int32]
    lib.idtx_node_get_kind.restype = c_int32
    lib.idtx_node_get_kind.argtypes = [c_void_p]
    lib.idtx_node_get_animation.restype = c_void_p
    lib.idtx_node_get_animation.argtypes = [c_void_p]
    lib.idtx_anim_get_track_count.restype = c_int32
    lib.idtx_anim_get_track_count.argtypes = [c_void_p]
    lib.idtx_anim_track_get_type.restype = c_int32
    lib.idtx_anim_track_get_type.argtypes = [c_void_p, c_int32]
    lib.idtx_anim_track_get_bone_name.restype = c_char_p
    lib.idtx_anim_track_get_bone_name.argtypes = [c_void_p, c_int32]
    lib.idtx_anim_track_get_key_count.restype = c_int32
    lib.idtx_anim_track_get_key_count.argtypes = [c_void_p, c_int32]
    lib.idtx_anim_track_get_key_time.restype = c_double
    lib.idtx_anim_track_get_key_time.argtypes = [c_void_p, c_int32, c_int32]
    lib.idtx_anim_track_get_key_float.restype = c_float
    lib.idtx_anim_track_get_key_float.argtypes = [c_void_p, c_int32, c_int32]
    return lib


def blend_tracks(core, scene):
    out = []
    for i in range(core.idtx_scene_get_node_count(scene)):
        node = core.idtx_scene_get_node(scene, i)
        if core.idtx_node_get_kind(node) != IDTX_NODE_SKELETON:
            continue
        anim = core.idtx_node_get_animation(node)
        if not anim:
            continue
        for t in range(core.idtx_anim_get_track_count(anim)):
            if core.idtx_anim_track_get_type(anim, t) == IDTX_ANIM_TRACK_BLEND_WEIGHT:
                out.append((anim, t))
    return out


def test_animated_weights_become_a_moving_track(core, tmp_path):
    stage = tmp_path / "anim.usda"
    write_stage(stage, animated=True)
    scene = core.idtx_core_import_scene_from_usd(str(stage).encode())
    assert scene, "scene import failed"
    try:
        tracks = blend_tracks(core, scene)
        assert len(tracks) == 1, f"expected 1 blend track, got {len(tracks)}"
        anim, t = tracks[0]
        assert core.idtx_anim_track_get_bone_name(anim, t) == b"lean"
        kc = core.idtx_anim_track_get_key_count(anim, t)
        assert kc == 3, f"expected 3 keys, got {kc}"
        keys = [(core.idtx_anim_track_get_key_time(anim, t, k),
                 core.idtx_anim_track_get_key_float(anim, t, k)) for k in range(kc)]
        expect = [(0.0, 0.0), (1.0, 1.0), (2.0, 0.0)]  # 24tps -> seconds
        for (gt, gv), (et, ev) in zip(keys, expect):
            assert abs(gt - et) < 1e-6 and abs(gv - ev) < 1e-6, f"{keys} != {expect}"
    finally:
        core.idtx_core_scene_destroy(scene)


def skeleton_nodes(core, scene):
    return [i for i in range(core.idtx_scene_get_node_count(scene))
            if core.idtx_node_get_kind(core.idtx_scene_get_node(scene, i))
            == IDTX_NODE_SKELETON]


def test_static_weights_do_not_move(core, tmp_path):
    stage = tmp_path / "rest.usda"
    write_stage(stage, animated=False)
    scene = core.idtx_core_import_scene_from_usd(str(stage).encode())
    assert scene, "scene import failed"
    try:
        # An empty scan would pass every assertion below by never running it, so
        # the population is asserted before the property (a silent skip is a FAIL).
        assert skeleton_nodes(core, scene), "no skeleton imported; nothing was checked"
        tracks = blend_tracks(core, scene)
        assert tracks, "no blend track found; the rest-key path was not exercised"
        for anim, t in tracks:
            kc = core.idtx_anim_track_get_key_count(anim, t)
            values = {core.idtx_anim_track_get_key_float(anim, t, k) for k in range(kc)}
            assert values == {0.0}, f"static weights produced motion: {values}"
    finally:
        core.idtx_core_scene_destroy(scene)


def test_no_blendshapes_yields_no_blend_tracks(core, tmp_path):
    stage = tmp_path / "bare.usda"
    write_stage(stage, animated=False, shapes=False)
    scene = core.idtx_core_import_scene_from_usd(str(stage).encode())
    assert scene, "scene import failed"
    try:
        assert skeleton_nodes(core, scene), "no skeleton imported; nothing was checked"
        tracks = blend_tracks(core, scene)
        assert not tracks, f"phantom blend tracks on a shapeless rig: {len(tracks)}"
    finally:
        core.idtx_core_scene_destroy(scene)


def test_the_comparator_rejects_a_planted_wrong_key(core, tmp_path):
    # Control for the control: the same extraction compared against deliberately
    # wrong values must FAIL, or the equality checks above are decoration.
    stage = tmp_path / "planted.usda"
    write_stage(stage, animated=True)
    scene = core.idtx_core_import_scene_from_usd(str(stage).encode())
    assert scene, "scene import failed"
    try:
        (anim, t), = blend_tracks(core, scene)
        got = [core.idtx_anim_track_get_key_float(anim, t, k)
               for k in range(core.idtx_anim_track_get_key_count(anim, t))]
        planted_wrong = [0.0, 0.5, 0.0]
        assert got != planted_wrong, "comparator cannot tell 1.0 from 0.5"
    finally:
        core.idtx_core_scene_destroy(scene)
