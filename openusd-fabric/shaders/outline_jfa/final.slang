// Copyright 2026 The openusd-fabric authors.
// SPDX-License-Identifier: MIT
//
// JFA outline pass 3 of 3: threshold + colour composite.
//
// Reads the converged JFA seed buffer (every pixel knows its nearest
// silhouette pixel's coordinate, material id, and stored width). For
// each pixel:
//
//   * If the pixel itself is a silhouette (distance 0), output the
//     scene colour unchanged. The mesh's own surface owns that pixel.
//   * Otherwise, compute Euclidean distance to the nearest silhouette.
//     If the distance is within the per-material width AND the depth
//     test allows (occluded-outline mode), look up the material's
//     outline colour from the palette UBO and write it into the scene
//     colour AOV with optional anti-aliasing blend at the fringe.
//
// Material palette is a fixed-size lookup keyed by material id. The
// renderer is responsible for binding the palette and keeping it in
// sync with the schema's v_sekai:mtoon:outline* attributes.

struct PaletteEntry
{
    float4 colour;              // RGB + outlineLightingMix
    float  width_px;            // resolved, in pixels
    float  _pad0, _pad1, _pad2;
};

struct FinalParams
{
    uint  paletteCount;
    uint  depthOccluded;        // 0 or 1
    float depthBiasMetres;
    float _pad0;
};

[[vk::binding(0, 0)]] ConstantBuffer<FinalParams>        params;
[[vk::binding(1, 0)]] StructuredBuffer<PaletteEntry>     palette;
[[vk::binding(2, 0)]] Texture2D<int4>                    seedIn;
[[vk::binding(3, 0)]] Texture2D<float>                   sceneDepth;
[[vk::binding(4, 0)]] RWTexture2D<float4>                sceneColourRW;

float pixelDistance(int2 here, int2 nearest)
{
    float2 d = float2(here - nearest);
    return length(d);
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
    int4 seed = seedIn.Load(int3(here, 0));
    if (seed.x < 0) {
        return; // background — no outline in this region
    }

    float dist = pixelDistance(here, seed.xy);
    if (dist <= 0.0) {
        return; // silhouette pixel itself — let the mesh shade
    }

    uint matId = uint(seed.z);
    if (matId == 0u || matId >= params.paletteCount) {
        return;
    }
    PaletteEntry entry = palette[matId];

    // Width override: prefer the per-material palette value if non-zero,
    // else fall back to the width encoded at the silhouette pixel itself
    // (the silhouette pass's authored value).
    float widthPx = entry.width_px > 0.0 ? entry.width_px : float(seed.w);
    if (widthPx <= 0.0 || dist > widthPx) {
        return;
    }

    if (params.depthOccluded != 0u) {
        float curDepth = sceneDepth.Load(int3(here, 0));
        float silhDepth = sceneDepth.Load(int3(seed.xy, 0));
        // Outline only if the silhouette is not behind the current
        // surface. depthBiasMetres avoids self-occlusion fringing.
        if (silhDepth > curDepth + params.depthBiasMetres) {
            return;
        }
    }

    // 1-pixel anti-aliasing band at the outline edge.
    float aa = saturate(widthPx - dist);
    float4 scene = sceneColourRW[tid.xy];
    float4 outline = entry.colour;
    sceneColourRW[tid.xy] = lerp(scene, outline, aa);
}
