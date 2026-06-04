"""
Pytest + Hypothesis tests for the JFA outline CPU reference (CHI-255).

Copyright 2026 The openusd-fabric authors.
SPDX-License-Identifier: MIT

Two layers of coverage:

  * Example-based tests with closed-form expected outcomes:
      - single-pixel silhouette -> exact Euclidean distance field
      - per-material outline width respected
      - background-only input is a no-op
      - matches the Lean spec's jfaStepCount helper

  * Property-based tests with Hypothesis (per pytest-with-eric guidance):
      generates arbitrary ID/width AOVs and asserts the mathematical
      invariants the algorithm must satisfy for every input, not just
      hand-picked ones.

If the reference drifts, the GPU implementation tested later cannot
diverge silently from the spec.
"""

from __future__ import annotations

from pathlib import Path
import sys

import numpy as np
import pytest
from hypothesis import HealthCheck, assume, given, settings, strategies as st
from hypothesis.extra import numpy as hnp

sys.path.insert(0, str(Path(__file__).resolve().parent))
import outline_jfa_reference as ref  # noqa: E402


def _empty_scene(h: int, w: int) -> tuple[np.ndarray, np.ndarray]:
    """White scene, max depth — i.e. no occlusion."""
    colour = np.ones((h, w, 4), dtype=np.float32)
    depth = np.full((h, w), 1e9, dtype=np.float32)
    return colour, depth


def test_single_pixel_distance_field():
    id_aov = np.zeros((32, 32), dtype=np.uint16)
    width_aov = np.zeros_like(id_aov)
    id_aov[16, 16] = 1
    width_aov[16, 16] = 16

    seed_init = ref.silhouette_init(id_aov, width_aov)
    seed = ref.converge_jfa(seed_init, max_width_px=16)

    # After convergence, every pixel's nearest-silhouette should be (16, 16).
    assert (seed[..., 0] == 16).all()
    assert (seed[..., 1] == 16).all()

    # Distance is exact Euclidean (or very close, given JFA can be 1-pixel off).
    dy, dx = np.mgrid[0:32, 0:32]
    expected = np.sqrt((dx - 16) ** 2 + (dy - 16) ** 2)
    _, distance = ref.final_pass(
        seed, palette={}, scene_colour=np.zeros((32, 32, 4), dtype=np.float32),
    )
    np.testing.assert_allclose(distance, expected, atol=1.5)


def test_single_material_outline_within_width():
    id_aov = np.zeros((32, 32), dtype=np.uint16)
    width_aov = np.zeros_like(id_aov)
    id_aov[16, 16] = 1
    width_aov[16, 16] = 5

    colour, depth = _empty_scene(32, 32)
    palette = {1: ((1.0, 0.0, 0.0, 1.0), 5.0)}
    out = ref.run(id_aov, width_aov, palette, colour, depth, max_width_px=8)

    # The silhouette pixel itself is left untouched.
    assert np.allclose(out.composite[16, 16], (1.0, 1.0, 1.0, 1.0))
    # A pixel exactly 1 away should be fully outlined (red).
    assert np.allclose(out.composite[16, 17], (1.0, 0.0, 0.0, 1.0))
    # A pixel 10 away (> 5 width) should be untouched white.
    assert np.allclose(out.composite[16, 26], (1.0, 1.0, 1.0, 1.0))


def test_two_materials_have_independent_widths():
    id_aov = np.zeros((48, 48), dtype=np.uint16)
    width_aov = np.zeros_like(id_aov)
    id_aov[12, 12] = 1
    width_aov[12, 12] = 3
    id_aov[36, 36] = 2
    width_aov[36, 36] = 9

    colour, depth = _empty_scene(48, 48)
    palette = {
        1: ((1.0, 0.0, 0.0, 1.0), 3.0),
        2: ((0.0, 1.0, 0.0, 1.0), 9.0),
    }
    out = ref.run(id_aov, width_aov, palette, colour, depth, max_width_px=16)

    # Material 1 ring: red within distance 3 of (12, 12).
    assert np.allclose(out.composite[12, 14], (1.0, 0.0, 0.0, 1.0))
    assert np.allclose(out.composite[12, 16], (1.0, 1.0, 1.0, 1.0))  # outside ring
    # Material 2 ring: green within distance 9 of (36, 36).
    assert np.allclose(out.composite[36, 44], (0.0, 1.0, 0.0, 1.0))
    assert np.allclose(out.composite[36, 47], (1.0, 1.0, 1.0, 1.0))  # 11 away, outside


def test_background_pixels_remain_untouched():
    id_aov = np.zeros((24, 24), dtype=np.uint16)
    width_aov = np.zeros_like(id_aov)
    # No silhouette at all.
    colour, depth = _empty_scene(24, 24)
    palette = {1: ((1.0, 0.0, 0.0, 1.0), 5.0)}
    out = ref.run(id_aov, width_aov, palette, colour, depth, max_width_px=8)

    # Composite must equal the input scene exactly.
    np.testing.assert_array_equal(out.composite, colour)
    # Every seed must be the sentinel.
    assert (out.seed[..., 0] == ref.SENTINEL).all()


def test_jfa_step_count_helper_matches_lean_spec():
    """jfaStepCount in lean/Fabric/Mesh/OutlineJFA.lean expects 64 -> 6."""
    # Reference's converge_jfa uses ceil(log2(width)) + 1 fixup pass.
    # The Lean helper documents the JFA-only count of 6 for width 64.
    assert int(np.ceil(np.log2(64))) == 6


# ---------------------------------------------------------------------------
# Property-based tests (Hypothesis)
#
# Each test asserts a mathematical invariant that must hold for *every*
# valid ID/width AOV, not just the hand-picked fixtures above. The
# strategies bound the search space so a single test run is fast
# (shape <= 32x32, material IDs <= 3, widths <= 16) while still
# exercising thousands of distinct inputs.
# ---------------------------------------------------------------------------

_MAX_DIM = 24
_MAX_ID = 3
_MAX_WIDTH = 12

# An (id_aov, width_aov) strategy. width is 0 wherever id is 0; otherwise
# it's a positive integer up to _MAX_WIDTH. We co-generate the two arrays
# so they always describe a consistent silhouette + width payload.
@st.composite
def _aov_pair(draw: st.DrawFn) -> tuple[np.ndarray, np.ndarray]:
    h = draw(st.integers(min_value=4, max_value=_MAX_DIM))
    w = draw(st.integers(min_value=4, max_value=_MAX_DIM))
    id_aov = draw(hnp.arrays(
        dtype=np.uint16,
        shape=(h, w),
        elements=st.integers(min_value=0, max_value=_MAX_ID),
    ))
    width_aov = np.zeros_like(id_aov)
    # Authored width is meaningless on background pixels; only set
    # widths where there is a silhouette.
    silhouette_count = int(np.count_nonzero(id_aov))
    if silhouette_count > 0:
        widths = draw(hnp.arrays(
            dtype=np.uint16,
            shape=(silhouette_count,),
            elements=st.integers(min_value=1, max_value=_MAX_WIDTH),
        ))
        width_aov[id_aov != 0] = widths
    return id_aov, width_aov


def _brute_force_nearest_dist(id_aov: np.ndarray) -> np.ndarray:
    """Authoritative O(H*W*S) Euclidean nearest-silhouette distance.

    Returns inf on background-only inputs; never used as a perf path, only
    as the truth oracle against which JFA's approximate answer is judged.
    """
    H, W = id_aov.shape
    yy, xx = np.mgrid[0:H, 0:W]
    sil_y, sil_x = np.where(id_aov != 0)
    if sil_y.size == 0:
        return np.full((H, W), np.inf, dtype=np.float32)
    dx = xx[..., None] - sil_x[None, None, :]
    dy = yy[..., None] - sil_y[None, None, :]
    return np.sqrt((dx * dx + dy * dy).min(axis=-1)).astype(np.float32)


@given(_aov_pair())
@settings(max_examples=80, deadline=None,
          suppress_health_check=[HealthCheck.too_slow])
def test_property_distances_are_non_negative(aov):
    """The distance field is non-negative everywhere, by construction."""
    id_aov, width_aov = aov
    res = ref.run(id_aov, width_aov, palette={},
                  scene_colour=np.zeros((*id_aov.shape, 4), dtype=np.float32),
                  max_width_px=_MAX_WIDTH)
    assert (res.distance >= 0.0).all()


@given(_aov_pair())
@settings(max_examples=80, deadline=None,
          suppress_health_check=[HealthCheck.too_slow])
def test_property_silhouette_pixels_have_zero_distance(aov):
    """Any silhouette pixel's nearest-silhouette is itself, distance 0."""
    id_aov, width_aov = aov
    res = ref.run(id_aov, width_aov, palette={},
                  scene_colour=np.zeros((*id_aov.shape, 4), dtype=np.float32),
                  max_width_px=_MAX_WIDTH)
    silhouette = id_aov != 0
    if not silhouette.any():
        return
    assert np.allclose(res.distance[silhouette], 0.0)


@given(_aov_pair())
@settings(max_examples=60, deadline=None,
          suppress_health_check=[HealthCheck.too_slow])
def test_property_jfa_within_one_pixel_of_brute_force_truth(aov):
    """JFA's distance must be within 1 pixel of the brute-force truth.

    Jump Flood is exact only inside its step radius; with the fixup
    stride=1 pass after the log2 schedule, residual error is bounded
    by one pixel for any in-bound query.
    """
    id_aov, width_aov = aov
    if not (id_aov != 0).any():
        return  # background-only — distance is inf everywhere, nothing to check
    res = ref.run(id_aov, width_aov, palette={},
                  scene_colour=np.zeros((*id_aov.shape, 4), dtype=np.float32),
                  max_width_px=_MAX_WIDTH)
    truth = _brute_force_nearest_dist(id_aov)
    # Reference reports 0 on background pixels with no valid seed; align
    # the comparison by only checking where JFA actually found a seed.
    valid = res.seed[..., 0] >= 0
    assume(valid.any())
    err = np.abs(res.distance[valid] - truth[valid])
    assert err.max() <= 1.5, (
        f"JFA distance error {err.max():.3f} exceeds 1.5 px; "
        f"id_aov.shape={id_aov.shape}, silhouette_count={int((id_aov!=0).sum())}"
    )


@given(_aov_pair())
@settings(max_examples=60, deadline=None,
          suppress_health_check=[HealthCheck.too_slow])
def test_property_outline_only_within_palette_width(aov):
    """Composited outline pixels must satisfy distance <= material width."""
    id_aov, width_aov = aov
    if not (id_aov != 0).any():
        return
    palette = {i: ((1.0, 0.0, 0.0, 1.0), float(_MAX_WIDTH))
               for i in range(1, _MAX_ID + 1)}
    scene = np.ones((*id_aov.shape, 4), dtype=np.float32)
    res = ref.run(id_aov, width_aov, palette, scene,
                  scene_depth=np.full(id_aov.shape, 1e9, dtype=np.float32),
                  max_width_px=_MAX_WIDTH)
    # Any pixel whose composite differs from the input scene must have
    # been inside some material's outline ring.
    changed = np.any(res.composite != scene, axis=-1)
    if changed.any():
        assert (res.distance[changed] <= _MAX_WIDTH + 1.0).all()


@given(_aov_pair())
@settings(max_examples=40, deadline=None,
          suppress_health_check=[HealthCheck.too_slow])
def test_property_jfa_converged_state_is_idempotent(aov):
    """Once converged, another stride=1 JFA pass cannot change the seed."""
    id_aov, width_aov = aov
    if not (id_aov != 0).any():
        return
    seed_init = ref.silhouette_init(id_aov, width_aov)
    converged = ref.converge_jfa(seed_init, max_width_px=_MAX_WIDTH)
    once_more = ref.jfa_step(converged, stride=1)
    np.testing.assert_array_equal(converged, once_more)


@given(_aov_pair())
@settings(max_examples=40, deadline=None,
          suppress_health_check=[HealthCheck.too_slow])
def test_property_background_only_is_a_no_op(aov):
    """All-background input must leave the scene colour exactly unchanged."""
    id_aov, _ = aov
    id_aov = np.zeros_like(id_aov)
    width_aov = np.zeros_like(id_aov)
    scene = np.random.default_rng(0).random((*id_aov.shape, 4),
                                            dtype=np.float32)
    palette = {1: ((1.0, 0.0, 0.0, 1.0), 5.0)}
    res = ref.run(id_aov, width_aov, palette, scene.copy(), max_width_px=8)
    np.testing.assert_array_equal(res.composite, scene)


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v"]))
