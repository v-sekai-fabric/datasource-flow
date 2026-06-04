// Copyright 2026 The openusd-fabric authors.
// SPDX-License-Identifier: MIT
//
// HdVSekaiOutlineJFATask -- implementation. See outlineJFATask.h.

#include "outlineJFATask.h"

#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/imaging/hd/renderIndex.h"
#include "pxr/imaging/hgi/blitCmdsOps.h"
#include "pxr/imaging/hgi/computeCmdsDesc.h"
#include "pxr/imaging/hgi/shaderFunctionDesc.h"
#include "pxr/imaging/hgi/types.h"
#include "pxr/base/tf/diagnostic.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(HdVSekaiOutlineJFATokens,
                        HD_V_SEKAI_OUTLINE_JFA_TOKENS);

// ---------------------------------------------------------------------------
// Embedded GLSL kernels.
//
// These three string literals are the slangc-emitted GLSL output of
// shaders/outline_jfa/*.slang. The plugin loads them via
// HgiShaderFunctionDesc and never reads from disk at runtime. To
// regenerate: `pixi run emit-shader-glsl`. CI diffs the regenerated
// bytes against the literals below; any divergence fails the build.
//
// The kernels are intentionally backend-agnostic GLSL 4.60 compute with
// std430 push constants; Hgi adapts the dispatch to GL / Vulkan /
// Metal via HgiShaderFunctionDesc.
// ---------------------------------------------------------------------------

static char const* const kSilhouetteInitGLSL = R"GLSL(
#version 460
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 0, rg16ui)  restrict readonly  uniform uimage2D id_aov;
layout(set = 0, binding = 1, rgba32i) restrict writeonly uniform iimage2D seed_rw;
void main() {
    ivec2 dim = imageSize(id_aov);
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    if (coord.x >= dim.x || coord.y >= dim.y) { return; }
    uvec2 s = imageLoad(id_aov, coord).rg;
    ivec4 seed = (s.x != 0u)
        ? ivec4(coord.x, coord.y, int(s.x), int(s.y))
        : ivec4(-1, -1, 0, 0);
    imageStore(seed_rw, coord, seed);
}
)GLSL";

static char const* const kJfaStepGLSL = R"GLSL(
#version 460
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(push_constant, std430) uniform Params { int stride; int _p0; int _p1; int _p2; } params;
layout(set = 0, binding = 0, rgba32i) restrict readonly  uniform iimage2D seed_in;
layout(set = 0, binding = 1, rgba32i) restrict writeonly uniform iimage2D seed_out;
int sqr_dist(ivec2 a, ivec2 b) { ivec2 d = a - b; return d.x*d.x + d.y*d.y; }
void main() {
    ivec2 dim = imageSize(seed_in);
    ivec2 here = ivec2(gl_GlobalInvocationID.xy);
    if (here.x >= dim.x || here.y >= dim.y) { return; }
    ivec4 best = imageLoad(seed_in, here);
    int best_d2 = (best.x < 0) ? 0x7FFFFFFF : sqr_dist(here, best.xy);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            ivec2 c = here + ivec2(dx, dy) * params.stride;
            if (c.x < 0 || c.y < 0 || c.x >= dim.x || c.y >= dim.y) continue;
            ivec4 cand = imageLoad(seed_in, c);
            if (cand.x < 0) continue;
            int d2 = sqr_dist(here, cand.xy);
            if (d2 < best_d2) { best = cand; best_d2 = d2; }
        }
    }
    imageStore(seed_out, here, best);
}
)GLSL";

static char const* const kFinalGLSL = R"GLSL(
#version 460
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
struct PaletteEntry { vec4 colour; float width_px; float _p0; float _p1; float _p2; };
layout(push_constant, std430) uniform Params {
    uint palette_count; uint depth_occluded; float depth_bias_metres; float _p0;
} params;
layout(set = 0, binding = 0, std430) restrict readonly buffer Palette { PaletteEntry entries[]; } palette;
layout(set = 0, binding = 1, rgba32i) restrict readonly uniform iimage2D seed_in;
layout(set = 0, binding = 2, r32f)    restrict readonly uniform image2D scene_depth;
layout(set = 0, binding = 3, rgba16f) restrict           uniform image2D scene_colour_rw;
void main() {
    ivec2 dim = imageSize(seed_in);
    ivec2 here = ivec2(gl_GlobalInvocationID.xy);
    if (here.x >= dim.x || here.y >= dim.y) { return; }
    ivec4 seed = imageLoad(seed_in, here);
    if (seed.x < 0) { return; }
    vec2 d = vec2(here - seed.xy);
    float dist = length(d);
    if (dist <= 0.0) { return; }
    uint mat_id = uint(seed.z);
    if (mat_id == 0u || mat_id >= params.palette_count) { return; }
    PaletteEntry entry = palette.entries[mat_id];
    float width_px = entry.width_px > 0.0 ? entry.width_px : float(seed.w);
    if (width_px <= 0.0 || dist > width_px) { return; }
    if (params.depth_occluded != 0u) {
        float cur_d  = imageLoad(scene_depth, here).r;
        float silh_d = imageLoad(scene_depth, seed.xy).r;
        if (silh_d > cur_d + params.depth_bias_metres) { return; }
    }
    float aa = clamp(width_px - dist, 0.0, 1.0);
    vec4 scene = imageLoad(scene_colour_rw, here);
    imageStore(scene_colour_rw, here, mix(scene, entry.colour, aa));
}
)GLSL";

// ---------------------------------------------------------------------------
// HdVSekaiOutlineJFATask
// ---------------------------------------------------------------------------

HdVSekaiOutlineJFATask::HdVSekaiOutlineJFATask(SdfPath const& id, Hgi* hgi)
    : HdTask(id), _hgi(hgi)
{
}

HdVSekaiOutlineJFATask::~HdVSekaiOutlineJFATask()
{
    _ReleasePipelines();
    _ReleaseTransientTextures();
}

void HdVSekaiOutlineJFATask::Sync(HdSceneDelegate* delegate,
                                  HdTaskContext* /*ctx*/,
                                  HdDirtyBits* dirtyBits)
{
    if (delegate && (*dirtyBits & HdChangeTracker::DirtyParams)) {
        VtValue v = delegate->Get(GetId(), HdTokens->params);
        if (v.IsHolding<HdVSekaiOutlineJFATaskParams>()) {
            _params = v.UncheckedGet<HdVSekaiOutlineJFATaskParams>();
        }
    }
    *dirtyBits = HdChangeTracker::Clean;
}

void HdVSekaiOutlineJFATask::Prepare(HdTaskContext* /*ctx*/,
                                     HdRenderIndex* /*renderIndex*/)
{
    if (!_pipelinesReady) {
        _pipelinesReady = _BuildPipelines();
    }
}

bool HdVSekaiOutlineJFATask::_BuildPipelines()
{
    if (!_hgi) {
        TF_CODING_ERROR("HdVSekaiOutlineJFATask: no Hgi instance bound");
        return false;
    }
    _shaderInit  = _CompileComputeProgram(
        HdVSekaiOutlineJFATokens->silhouetteInitShader, kSilhouetteInitGLSL);
    _shaderStep  = _CompileComputeProgram(
        HdVSekaiOutlineJFATokens->jfaStepShader, kJfaStepGLSL);
    _shaderFinal = _CompileComputeProgram(
        HdVSekaiOutlineJFATokens->finalShader, kFinalGLSL);
    if (!_shaderInit || !_shaderStep || !_shaderFinal) {
        return false;
    }
    HgiComputePipelineDesc desc;
    desc.shaderProgram = _shaderInit;
    _pipelineInit  = _hgi->CreateComputePipeline(desc);
    desc.shaderProgram = _shaderStep;
    _pipelineStep  = _hgi->CreateComputePipeline(desc);
    desc.shaderProgram = _shaderFinal;
    _pipelineFinal = _hgi->CreateComputePipeline(desc);
    return _pipelineInit && _pipelineStep && _pipelineFinal;
}

HgiShaderProgramHandle
HdVSekaiOutlineJFATask::_CompileComputeProgram(TfToken const& debugName,
                                               char const* glslSource)
{
    HgiShaderFunctionDesc fnDesc;
    fnDesc.debugName  = debugName.GetString();
    fnDesc.shaderStage = HgiShaderStageCompute;
    fnDesc.shaderCode  = glslSource;
    HgiShaderFunctionHandle fn = _hgi->CreateShaderFunction(fnDesc);
    if (!fn || !fn->IsValid()) {
        TF_CODING_ERROR("HdVSekaiOutlineJFATask: shader %s failed to compile: %s",
                        debugName.GetText(),
                        fn ? fn->GetCompileErrors().c_str() : "(null fn)");
        if (fn) _hgi->DestroyShaderFunction(&fn);
        return HgiShaderProgramHandle();
    }
    HgiShaderProgramDesc progDesc;
    progDesc.debugName = debugName.GetString();
    progDesc.shaderFunctions.push_back(fn);
    HgiShaderProgramHandle prog = _hgi->CreateShaderProgram(progDesc);
    // Hgi takes ownership of the function via the program; release our handle.
    _hgi->DestroyShaderFunction(&fn);
    return prog;
}

bool HdVSekaiOutlineJFATask::_EnsureSeedTextures(GfVec2i const& dim)
{
    if (dim == _seedSize && _seedA && _seedB) {
        return true;
    }
    _ReleaseTransientTextures();
    HgiTextureDesc texDesc;
    texDesc.debugName     = "VSekaiJFA_Seed";
    texDesc.dimensions    = GfVec3i(dim[0], dim[1], 1);
    texDesc.layerCount    = 1;
    texDesc.format        = HgiFormatInt32Vec4;
    texDesc.usage         = HgiTextureUsageBitsShaderRead
                          | HgiTextureUsageBitsShaderWrite;
    texDesc.mipLevels     = 1;
    texDesc.sampleCount   = HgiSampleCount1;
    _seedA = _hgi->CreateTexture(texDesc);
    _seedB = _hgi->CreateTexture(texDesc);
    _seedSize = dim;
    return _seedA && _seedB;
}

HgiTextureHandle
HdVSekaiOutlineJFATask::_AcquireAov(HdTaskContext* ctx,
                                    TfToken const& aovName)
{
    // The scene delegate writes resolved AOV handles into the task
    // context under their AOV names. Returning a null handle here is
    // fatal for the dispatch but is expected when the application has
    // not bound the V-Sekai outline ID AOV.
    auto it = ctx->find(aovName);
    if (it == ctx->end()) {
        return HgiTextureHandle();
    }
    if (it->second.IsHolding<HgiTextureHandle>()) {
        return it->second.UncheckedGet<HgiTextureHandle>();
    }
    return HgiTextureHandle();
}

void HdVSekaiOutlineJFATask::Execute(HdTaskContext* ctx)
{
    if (!_pipelinesReady) {
        return;
    }
    HgiTextureHandle idAov       = _AcquireAov(ctx, _params.idAov);
    HgiTextureHandle sceneColour = _AcquireAov(ctx, _params.sceneColourAov);
    HgiTextureHandle sceneDepth  = _AcquireAov(ctx, _params.sceneDepthAov);
    if (!idAov || !sceneColour) {
        // No silhouette source or no scene-colour target -> nothing to draw.
        return;
    }
    GfVec3i dim3 = idAov->GetDescriptor().dimensions;
    GfVec2i dim(dim3[0], dim3[1]);
    if (!_EnsureSeedTextures(dim)) {
        return;
    }

    auto const groupX = (dim[0] + 7) / 8;
    auto const groupY = (dim[1] + 7) / 8;

    HgiComputeCmdsUniquePtr cmds = _hgi->CreateComputeCmds(
        HgiComputeCmdsDesc{});
    if (!cmds) return;

    auto makeTexBind = [](uint32_t binding,
                          HgiTextureHandle const& tex,
                          bool writable) {
        HgiTextureBindDesc b;
        b.bindingIndex = binding;
        b.textures.push_back(tex);
        b.resourceType = HgiBindResourceTypeStorageImage;
        b.stageUsage   = HgiShaderStageCompute;
        b.writable     = writable;
        return b;
    };
    auto makeBufBind = [](uint32_t binding,
                          HgiBufferHandle const& buf) {
        HgiBufferBindDesc b;
        b.bindingIndex = binding;
        b.buffers.push_back(buf);
        b.offsets.push_back(0);
        b.sizes.push_back(buf->GetByteSizeOfResource());
        b.resourceType = HgiBindResourceTypeStorageBuffer;
        b.stageUsage   = HgiShaderStageCompute;
        b.writable     = false;
        return b;
    };

    // Pass 1: silhouette_init.
    {
        HgiResourceBindingsDesc rb;
        rb.textures.push_back(makeTexBind(0, idAov,  /*writable=*/false));
        rb.textures.push_back(makeTexBind(1, _seedA, /*writable=*/true));
        auto bindings = _hgi->CreateResourceBindings(rb);
        cmds->BindPipeline(_pipelineInit);
        cmds->BindResources(bindings);
        cmds->Dispatch(groupX, groupY);
        _hgi->DestroyResourceBindings(&bindings);
    }

    // Pass 2: ceil(log2(maxWidth)) JFA steps with stride halving each pass,
    // plus one fix-up stride=1 pass for the corner cases JFA mislabels.
    int steps = std::max(1,
        static_cast<int>(std::ceil(std::log2(
            std::max(2, _params.maxOutlineWidthPx)))));
    int stride = 1 << (steps - 1);
    HgiTextureHandle src = _seedA;
    HgiTextureHandle dst = _seedB;
    for (int i = 0; i <= steps; ++i) {  // <= steps to include the fix-up
        int useStride = (i == steps) ? 1 : std::max(1, stride);
        HgiResourceBindingsDesc rb;
        rb.textures.push_back(makeTexBind(0, src, /*writable=*/false));
        rb.textures.push_back(makeTexBind(1, dst, /*writable=*/true));
        auto bindings = _hgi->CreateResourceBindings(rb);
        cmds->BindPipeline(_pipelineStep);
        cmds->BindResources(bindings);

        struct StepPC { int stride; int p0; int p1; int p2; } pc{useStride, 0, 0, 0};
        cmds->SetConstantValues(_pipelineStep, /*bindIndex=*/0,
                                sizeof(pc), &pc);
        cmds->Dispatch(groupX, groupY);
        _hgi->DestroyResourceBindings(&bindings);
        std::swap(src, dst);
        stride >>= 1;
    }

    // Pass 3: final composite into scene colour.
    {
        HgiResourceBindingsDesc rb;
        if (_paletteBuffer) {
            rb.buffers.push_back(makeBufBind(0, _paletteBuffer));
        }
        rb.textures.push_back(makeTexBind(1, src, /*writable=*/false));
        if (sceneDepth) {
            rb.textures.push_back(makeTexBind(2, sceneDepth,
                                              /*writable=*/false));
        }
        rb.textures.push_back(makeTexBind(3, sceneColour,
                                          /*writable=*/true));
        auto bindings = _hgi->CreateResourceBindings(rb);
        cmds->BindPipeline(_pipelineFinal);
        cmds->BindResources(bindings);

        struct FinalPC {
            uint32_t paletteCount;
            uint32_t depthOccluded;
            float    depthBias;
            float    _pad;
        } pc{};
        pc.depthOccluded = _params.depthOccluded ? 1u : 0u;
        pc.depthBias     = _params.depthBiasMetres;
        // paletteCount is filled by the scene delegate when it uploads
        // the buffer; default to 0 when unbound.
        pc.paletteCount  = _paletteBuffer
            ? static_cast<uint32_t>(
                _paletteBuffer->GetByteSizeOfResource() / (sizeof(float) * 8))
            : 0u;
        cmds->SetConstantValues(_pipelineFinal, /*bindIndex=*/0,
                                sizeof(pc), &pc);
        cmds->Dispatch(groupX, groupY);
        _hgi->DestroyResourceBindings(&bindings);
    }

    _hgi->SubmitCmds(cmds.get(), HgiSubmitWaitTypeNoWait);
}

void HdVSekaiOutlineJFATask::_ReleasePipelines()
{
    if (!_hgi) return;
    if (_pipelineInit)  _hgi->DestroyComputePipeline(&_pipelineInit);
    if (_pipelineStep)  _hgi->DestroyComputePipeline(&_pipelineStep);
    if (_pipelineFinal) _hgi->DestroyComputePipeline(&_pipelineFinal);
    if (_shaderInit)    _hgi->DestroyShaderProgram(&_shaderInit);
    if (_shaderStep)    _hgi->DestroyShaderProgram(&_shaderStep);
    if (_shaderFinal)   _hgi->DestroyShaderProgram(&_shaderFinal);
    _pipelinesReady = false;
}

void HdVSekaiOutlineJFATask::_ReleaseTransientTextures()
{
    if (!_hgi) return;
    if (_seedA) _hgi->DestroyTexture(&_seedA);
    if (_seedB) _hgi->DestroyTexture(&_seedB);
    _seedSize = GfVec2i(0, 0);
}

PXR_NAMESPACE_CLOSE_SCOPE
