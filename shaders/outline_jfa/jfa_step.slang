// Copyright 2026 The openusd-fabric authors.
// SPDX-License-Identifier: MIT
//
// JFA outline pass 2 of 3: one ping-pong JFA iteration.
//
// For each pixel, samples its 3x3 neighbourhood at offsets of (±stride, 0, ±stride)
// scaled by the current step size, keeps the seed whose stored coordinate
// is nearest to the current pixel by squared Euclidean distance.
//
// The host dispatches this `ceil(log2(maxOutlineWidthPx))` times with
// `params.stride` halved each pass (starting at the largest power of two
// not exceeding maxOutlineWidthPx). Ping-pongs between two seed textures;
// the host owns the swap.

struct StepParams
{
    int stride;     // pixel offset between neighbour samples in this pass
    int _pad0;
    int _pad1;
    int _pad2;
};

[[vk::binding(0, 0)]] ConstantBuffer<StepParams>  params;
[[vk::binding(1, 0)]] Texture2D<int4>             seedIn;     // read
[[vk::binding(2, 0)]] RWTexture2D<int4>           seedOut;    // write

static const int2 kOffsets[9] = {
    int2(-1, -1), int2( 0, -1), int2( 1, -1),
    int2(-1,  0), int2( 0,  0), int2( 1,  0),
    int2(-1,  1), int2( 0,  1), int2( 1,  1)
};

int sqrDist(int2 a, int2 b)
{
    int2 d = a - b;
    return d.x * d.x + d.y * d.y;
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint2 dim;
    seedIn.GetDimensions(dim.x, dim.y);
    if (tid.x >= dim.x || tid.y >= dim.y) {
        return;
    }
    int2 here = int2(tid.xy);

    // Start from the current pixel's stored seed (may be invalid).
    int4 best   = seedIn.Load(int3(here, 0));
    int  bestD2 = (best.x < 0) ? 0x7FFFFFFF : sqrDist(here, best.xy);

    [unroll]
    for (int i = 0; i < 9; ++i) {
        int2 sampleCoord = here + kOffsets[i] * params.stride;
        if (any(sampleCoord < int2(0, 0)) || any(sampleCoord >= int2(dim))) {
            continue;
        }
        int4 cand = seedIn.Load(int3(sampleCoord, 0));
        if (cand.x < 0) {
            continue;
        }
        int candD2 = sqrDist(here, cand.xy);
        if (candD2 < bestD2) {
            best   = cand;
            bestD2 = candD2;
        }
    }
    seedOut[tid.xy] = best;
}
