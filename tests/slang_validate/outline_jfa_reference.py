"""
CPU reference for the JFA outline shaders (CHI-255).

Copyright 2026 The openusd-fabric authors.
SPDX-License-Identifier: MIT

Mirrors the three Slang passes under shaders/outline_jfa/ in pure numpy
so the slang_validate harness can:

  * run the algorithm without a GPU
  * compare against a hand-authored expected outcome on a synthetic mask
  * later, compare bit-for-bit against the slangc-compiled output once
    the GPU validation step lands.

Implemented for clarity, not performance. A 256x256 input runs in well
under a second.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np


SENTINEL = -1


@dataclass
class JFAResult:
    """Final state after all three passes."""
    seed: np.ndarray         # shape (H, W, 4) int32 -- x, y, id, width_px
    distance: np.ndarray     # shape (H, W)    float32 -- pixel distance
    composite: np.ndarray    # shape (H, W, 4) float32 -- RGBA after final pass


def silhouette_init(id_aov: np.ndarray, width_aov: np.ndarray) -> np.ndarray:
    """Pass 1: every silhouette pixel stores its own coordinate; others store sentinel.

    id_aov    : (H, W) uint16, 0 = background
    width_aov : (H, W) uint16, width in pixels at the silhouette
    Returns   : (H, W, 4) int32 array; channels are x, y, id, width.
    """
    H, W = id_aov.shape
    seed = np.empty((H, W, 4), dtype=np.int32)
    yy, xx = np.mgrid[0:H, 0:W]
    is_silhouette = id_aov != 0
    seed[..., 0] = np.where(is_silhouette, xx, SENTINEL)
    seed[..., 1] = np.where(is_silhouette, yy, SENTINEL)
    seed[..., 2] = id_aov.astype(np.int32)
    seed[..., 3] = width_aov.astype(np.int32)
    return seed


def _sqr_dist(here_xy: np.ndarray, seed_xy: np.ndarray) -> np.ndarray:
    """Per-pixel squared Euclidean distance, with sentinel handling.

    here_xy : (H, W, 2) int32 of the pixel's own coords
    seed_xy : (H, W, 2) int32 of the candidate seed's coords
    Returns : (H, W) int32; SENTINEL (-1) seeds get int32 max.
    """
    valid = seed_xy[..., 0] >= 0
    d = here_xy - seed_xy
    sd = d[..., 0] * d[..., 0] + d[..., 1] * d[..., 1]
    return np.where(valid, sd, np.iinfo(np.int32).max)


def jfa_step(seed_in: np.ndarray, stride: int) -> np.ndarray:
    """Pass 2: one JFA iteration at the given stride. Returns the new seed."""
    H, W, _ = seed_in.shape
    yy, xx = np.mgrid[0:H, 0:W]
    here_xy = np.stack([xx, yy], axis=-1).astype(np.int32)

    best = seed_in.copy()
    best_d2 = _sqr_dist(here_xy, best[..., 0:2])

    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            if dx == 0 and dy == 0:
                continue
            sx = xx + dx * stride
            sy = yy + dy * stride
            in_bounds = (sx >= 0) & (sx < W) & (sy >= 0) & (sy < H)
            # Clamp so the gather is legal; in_bounds masks out illegal hits.
            sxc = np.clip(sx, 0, W - 1)
            syc = np.clip(sy, 0, H - 1)
            cand = seed_in[syc, sxc]
            cand_d2 = _sqr_dist(here_xy, cand[..., 0:2])
            cand_d2 = np.where(in_bounds, cand_d2, np.iinfo(np.int32).max)
            take = cand_d2 < best_d2
            best = np.where(take[..., None], cand, best)
            best_d2 = np.where(take, cand_d2, best_d2)
    return best


def converge_jfa(seed_init: np.ndarray, max_width_px: int) -> np.ndarray:
    """Run JFA until convergence for the given max outline width.

    Step count = ceil(log2(max_width_px)) at minimum 1, with strides
    starting at the largest power of two <= max_width_px and halving.
    """
    if max_width_px <= 1:
        return jfa_step(seed_init, stride=1)
    steps = max(1, int(np.ceil(np.log2(max_width_px))))
    stride = 1 << (steps - 1)
    seed = seed_init
    for _ in range(steps):
        seed = jfa_step(seed, stride=max(1, stride))
        stride //= 2
    # One extra fix-up pass at stride=1 catches the last few pixels JFA
    # occasionally mislabels at sharp corners.
    seed = jfa_step(seed, stride=1)
    return seed


def final_pass(
    seed: np.ndarray,
    palette: dict[int, tuple[tuple[float, float, float, float], float]],
    scene_colour: np.ndarray,
    scene_depth: np.ndarray | None = None,
    depth_occluded: bool = True,
    depth_bias_metres: float = 1e-4,
) -> tuple[np.ndarray, np.ndarray]:
    """Pass 3: distance-threshold + colour composite.

    palette: { material_id : ((r,g,b,a), width_px) }. If width_px is 0,
             the per-pixel width stored in the seed buffer is used.
    Returns: (composite RGBA, distance map for diagnostics).
    """
    H, W, _ = seed.shape
    yy, xx = np.mgrid[0:H, 0:W]
    here_xy = np.stack([xx, yy], axis=-1).astype(np.int32)

    valid = seed[..., 0] >= 0
    dx = (here_xy[..., 0] - seed[..., 0]).astype(np.float32)
    dy = (here_xy[..., 1] - seed[..., 1]).astype(np.float32)
    distance = np.where(valid, np.sqrt(dx * dx + dy * dy), 0.0).astype(np.float32)

    composite = scene_colour.copy()
    # Iterate per material to keep the reference readable; production
    # GPU path samples palette[matId] per fragment.
    for mat_id, (rgba, palette_width) in palette.items():
        if mat_id == 0:
            continue
        mask = valid & (seed[..., 2] == mat_id) & (distance > 0.0)
        if not np.any(mask):
            continue
        per_pixel_width = seed[..., 3].astype(np.float32)
        width = palette_width if palette_width > 0 else per_pixel_width
        # Avoid div-by-zero when both palette and seed width are 0.
        threshold = mask & (distance <= width)
        if not np.any(threshold):
            continue
        if depth_occluded and scene_depth is not None:
            silh_depth = scene_depth[seed[..., 1].clip(0, H - 1),
                                      seed[..., 0].clip(0, W - 1)]
            cur_depth = scene_depth
            occluded = silh_depth > (cur_depth + depth_bias_metres)
            threshold &= ~occluded
        aa = np.clip(width - distance, 0.0, 1.0)
        outline_rgba = np.broadcast_to(np.asarray(rgba, dtype=np.float32),
                                       composite.shape).copy()
        a = (aa * threshold).astype(np.float32)[..., None]
        composite = composite * (1.0 - a) + outline_rgba * a

    return composite, distance


def run(
    id_aov: np.ndarray,
    width_aov: np.ndarray,
    palette: dict[int, tuple[tuple[float, float, float, float], float]],
    scene_colour: np.ndarray,
    scene_depth: np.ndarray | None = None,
    max_width_px: int = 32,
) -> JFAResult:
    """End-to-end runner mirroring the host's three-pass dispatch."""
    seed_init = silhouette_init(id_aov, width_aov)
    seed = converge_jfa(seed_init, max_width_px=max_width_px)
    composite, distance = final_pass(seed, palette, scene_colour, scene_depth)
    return JFAResult(seed=seed, distance=distance, composite=composite)
