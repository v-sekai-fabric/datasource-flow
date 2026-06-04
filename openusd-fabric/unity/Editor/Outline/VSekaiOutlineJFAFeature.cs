// Copyright 2026 The openusd-fabric authors.
// SPDX-License-Identifier: MIT
//
// V-Sekai JFA outline ScriptableRendererFeature (CHI-255).
//
// URP renderer feature that runs the three Slang/HLSL compute kernels
// from shaders/outline_jfa/ as a post-transparent screen-space pass.
// Reads a silhouette ID/width AOV produced by VSekaiMToonAPI materials
// during the opaque pass; writes the outline composite into the
// camera's colour target.
//
// Per-material outline colours/widths are uploaded into the palette
// compute buffer by the V-Sekai schema mapper editor script when it
// builds the avatar prefab from a .usda stage (the upstream
// counterpart to godot/outline_jfa/v_sekai_outline_jfa.gd).
//
// VRChat caveat: per-avatar renderer features are not allowed on
// uploaded avatars, so this feature is editor-time only. The exported
// .vrm falls back to a backface-extrusion outline mesh emitted by the
// V-Sekai-forked usd-converter-for-vrchat (CHI-255 "Out of scope").

using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.Universal;

namespace VSekai.Outline
{
    public sealed class VSekaiOutlineJFAFeature : ScriptableRendererFeature
    {
        [System.Serializable]
        public sealed class Settings
        {
            public ComputeShader silhouetteInit;
            public ComputeShader jfaStep;
            public ComputeShader finalComposite;
            public int maxOutlineWidthPx = 64;
            public bool depthOccluded = true;
            [Range(0f, 0.01f)] public float depthBiasMetres = 0.0001f;
        }

        public Settings settings = new Settings();
        VSekaiOutlineJFAPass _pass;

        public override void Create()
        {
            if (settings.silhouetteInit == null
                || settings.jfaStep == null
                || settings.finalComposite == null)
            {
                Debug.LogWarning("VSekaiOutlineJFAFeature: compute shaders unassigned;"
                                 + " feature disabled.");
                return;
            }
            _pass = new VSekaiOutlineJFAPass(settings);
            _pass.renderPassEvent = RenderPassEvent.AfterRenderingTransparents;
        }

        public override void AddRenderPasses(ScriptableRenderer renderer,
                                             ref RenderingData renderingData)
        {
            if (_pass != null) renderer.EnqueuePass(_pass);
        }
    }

    /// <summary>
    /// The actual pass. Dispatches silhouette_init, then ceil(log2(maxWidth))
    /// JFA step passes, then the final composite. Ping-pongs between two
    /// transient RTHandles allocated from the URP renderer's pool.
    /// </summary>
    public sealed class VSekaiOutlineJFAPass : ScriptableRenderPass
    {
        const int LocalSize = 8;
        const int InitKernel = 0;
        const int StepKernel = 0;
        const int FinalKernel = 0;

        readonly VSekaiOutlineJFAFeature.Settings _s;
        RTHandle _seedA, _seedB, _idAov;
        ComputeBuffer _palette;

        public VSekaiOutlineJFAPass(VSekaiOutlineJFAFeature.Settings s)
        {
            _s = s;
        }

        public override void OnCameraSetup(CommandBuffer cmd,
                                           ref RenderingData renderingData)
        {
            var desc = renderingData.cameraData.cameraTargetDescriptor;
            var seedDesc = new RenderTextureDescriptor(desc.width, desc.height,
                RenderTextureFormat.RGBAInt, 0)
            {
                enableRandomWrite = true,
            };
            RenderingUtils.ReAllocateIfNeeded(ref _seedA, seedDesc,
                FilterMode.Point, TextureWrapMode.Clamp, name: "VSekaiJFA_SeedA");
            RenderingUtils.ReAllocateIfNeeded(ref _seedB, seedDesc,
                FilterMode.Point, TextureWrapMode.Clamp, name: "VSekaiJFA_SeedB");
            var idDesc = new RenderTextureDescriptor(desc.width, desc.height,
                RenderTextureFormat.RGInt, 0)
            {
                enableRandomWrite = true,
            };
            RenderingUtils.ReAllocateIfNeeded(ref _idAov, idDesc,
                FilterMode.Point, TextureWrapMode.Clamp, name: "VSekaiJFA_IDAov");
        }

        public override void Execute(ScriptableRenderContext context,
                                     ref RenderingData renderingData)
        {
            var cmd = CommandBufferPool.Get("VSekaiOutlineJFA");
            try
            {
                var desc = renderingData.cameraData.cameraTargetDescriptor;
                int gx = Mathf.CeilToInt(desc.width / (float)LocalSize);
                int gy = Mathf.CeilToInt(desc.height / (float)LocalSize);

                // Pass 1: silhouette_init.
                cmd.SetComputeTextureParam(_s.silhouetteInit, InitKernel,
                    "id_aov", _idAov);
                cmd.SetComputeTextureParam(_s.silhouetteInit, InitKernel,
                    "seed_rw", _seedA);
                cmd.DispatchCompute(_s.silhouetteInit, InitKernel, gx, gy, 1);

                // Pass 2: jfa_step, log2 schedule with one stride=1 fix-up.
                int steps = Mathf.Max(1,
                    Mathf.CeilToInt(Mathf.Log(_s.maxOutlineWidthPx, 2f)));
                int stride = 1 << (steps - 1);
                RTHandle src = _seedA, dst = _seedB;
                for (int i = 0; i < steps; ++i)
                {
                    cmd.SetComputeIntParam(_s.jfaStep, "stride", Mathf.Max(1, stride));
                    cmd.SetComputeTextureParam(_s.jfaStep, StepKernel, "seed_in", src);
                    cmd.SetComputeTextureParam(_s.jfaStep, StepKernel, "seed_out", dst);
                    cmd.DispatchCompute(_s.jfaStep, StepKernel, gx, gy, 1);
                    (src, dst) = (dst, src);
                    stride >>= 1;
                }
                // Final stride=1 fix-up.
                cmd.SetComputeIntParam(_s.jfaStep, "stride", 1);
                cmd.SetComputeTextureParam(_s.jfaStep, StepKernel, "seed_in", src);
                cmd.SetComputeTextureParam(_s.jfaStep, StepKernel, "seed_out", dst);
                cmd.DispatchCompute(_s.jfaStep, StepKernel, gx, gy, 1);
                (src, dst) = (dst, src);

                // Pass 3: final composite into the camera colour target.
                cmd.SetComputeIntParam(_s.finalComposite,
                    "depth_occluded", _s.depthOccluded ? 1 : 0);
                cmd.SetComputeFloatParam(_s.finalComposite,
                    "depth_bias_metres", _s.depthBiasMetres);
                cmd.SetComputeTextureParam(_s.finalComposite, FinalKernel,
                    "seed_in", src);
                cmd.SetComputeTextureParam(_s.finalComposite, FinalKernel,
                    "scene_depth", renderingData.cameraData.renderer.cameraDepthTargetHandle);
                cmd.SetComputeTextureParam(_s.finalComposite, FinalKernel,
                    "scene_colour_rw", renderingData.cameraData.renderer.cameraColorTargetHandle);
                if (_palette != null)
                {
                    cmd.SetComputeBufferParam(_s.finalComposite, FinalKernel,
                        "palette", _palette);
                    cmd.SetComputeIntParam(_s.finalComposite, "palette_count",
                        _palette.count);
                }
                cmd.DispatchCompute(_s.finalComposite, FinalKernel, gx, gy, 1);

                context.ExecuteCommandBuffer(cmd);
            }
            finally
            {
                CommandBufferPool.Release(cmd);
            }
        }

        public void UpdatePalette(ComputeBuffer palette)
        {
            _palette = palette;
        }

        public void Dispose()
        {
            _seedA?.Release();
            _seedB?.Release();
            _idAov?.Release();
        }
    }
}
