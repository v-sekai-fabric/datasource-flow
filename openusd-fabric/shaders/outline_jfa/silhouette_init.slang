// Copyright 2026 The openusd-fabric authors.
// SPDX-License-Identifier: MIT
//
// JFA outline pass 1 of 3: silhouette init.
//
// Reads the rasterized ID image (R = material_id; 0 = background) and
// initialises the JFA seed buffer. Every silhouette pixel stores its
// own integer coordinate as its "nearest silhouette" hit; every
// background pixel stores the sentinel (-1, -1). Subsequent JFA-step
// passes propagate hits outward.
//
// Width is decoded from the same input AOV's G channel. Encoded as
// `uint(width_px)` for the 8/16-bit unsigned path; the final pass
// reads it from the nearest-silhouette pixel rather than per-fragment,
// so width can vary per material without per-pixel storage in the
// step textures.
//
// Dispatch with one thread per scene-colour pixel.

[[vk::binding(0, 0)]] Texture2D<uint2>      idAov;            // R=id, G=width_px
[[vk::binding(1, 0)]] RWTexture2D<int4>     seedRW;           // RG=int2 seed coord, BA=id+width

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint2 dim;
    idAov.GetDimensions(dim.x, dim.y);
    if (tid.x >= dim.x || tid.y >= dim.y) {
        return;
    }

    uint2 sample = idAov.Load(int3(tid.xy, 0)).rg;
    uint  id     = sample.x;
    uint  width  = sample.y;

    int4 seed;
    if (id != 0u) {
        seed = int4(int(tid.x), int(tid.y), int(id), int(width));
    } else {
        seed = int4(-1, -1, 0, 0);
    }
    seedRW[tid.xy] = seed;
}
