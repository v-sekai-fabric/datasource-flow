// Copyright 2026 V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// IdtxUsdImporter — wires libidtx_core's USD reader into Unity's
// asset import pipeline. When a .usda / .usdc / .usd lands in Assets/,
// this ScriptedImporter walks it through idtx_core_import_avatar_from_usd,
// materialises a GameObject via UnityAvatarBridge.AvatarToGameObject,
// and saves the result as the imported asset's main object.
//
// IdtxCoreLoader runs as [InitializeOnLoad] at Editor startup, before
// any USD work happens, to set PXR_PLUGINPATH_NAME so the V-Sekai
// schema plugin resolves. This is the Unity-side counterpart to
// Blender's openusd-fabric/blender/post_export_hook.py — both rely
// on the same plugInfo.json under openusd-fabric/schema/.

using System;
using System.IO;
using IdtxCore.Bridge;
using UnityEditor;
using UnityEditor.AssetImporters;
using UnityEngine;

namespace IdtxCore.Editor
{
    /// <summary>
    /// Idempotent V-Sekai schema plugin registration. Runs once at Editor
    /// startup (and on every domain reload) before any USD import.
    /// </summary>
    [InitializeOnLoad]
    internal static class IdtxCoreLoader
    {
        // Convention: the V-Sekai schema directory lives at
        // <ProjectRoot>/Packages/com.vsekai.idtxcore/Schema/  when the
        // package is dropped in via UPM, or anywhere on
        // PXR_PLUGINPATH_NAME otherwise. We probe the package layout
        // first, fall back to the env var the user already set.
        private const string EnvVar = "PXR_PLUGINPATH_NAME";

        static IdtxCoreLoader()
        {
            try
            {
                // CRITICAL ORDER: register OpenUSD's plugin path BEFORE the first
                // native call below loads usd_ms and initializes its PlugRegistry.
                // If it's empty when a USD stage is later created (the export), the
                // stage-create faults and takes the whole editor down. The core
                // plugin tree (ar/ sdf/ usd/ + plugInfo.json) ships beside usd_ms
                // at Plugins/<arch>/usd (deployed by scons/idtxcore.py).
                string coreUsd = FindCoreUsdPluginDir();
                if (!string.IsNullOrEmpty(coreUsd))
                    PrependEnv(EnvVar, coreUsd);
                string pkgSchema = FindPackageSchemaDir();
                if (!string.IsNullOrEmpty(pkgSchema))
                    PrependEnv(EnvVar, pkgSchema);

                // First native call — binds libidtx_core (+ usd_ms) with the path set.
                string ver = IdtxAvatar.CoreVersion();
                Debug.Log($"[IdtxCore] libidtx_core {ver} registered; PXR_PLUGINPATH_NAME = " +
                          Environment.GetEnvironmentVariable(EnvVar));
            }
            catch (Exception e)
            {
                Debug.LogWarning($"[IdtxCore] init: {e.Message}");
            }
        }

        private static string FindPackageSchemaDir()
        {
            // Resolve via Unity's package manager — works whether the
            // package lives under Packages/ (immutable cache) or
            // Assets/Plugins/ (embedded).
            var pkgInfo = UnityEditor.PackageManager.PackageInfo.FindForAssembly(
                typeof(IdtxCoreLoader).Assembly);
            if (pkgInfo == null) return null;
            string candidate = Path.Combine(pkgInfo.resolvedPath, "Schema");
            return Directory.Exists(candidate) ? candidate : null;
        }

        // The bundled OpenUSD core plugin tree, deployed beside usd_ms at
        // Plugins/<arch>/usd (scons/idtxcore.py). OpenUSD needs it on
        // PXR_PLUGINPATH_NAME to register Ar/Sdf/Usd before any stage op.
        private static string FindCoreUsdPluginDir()
        {
            var pkgInfo = UnityEditor.PackageManager.PackageInfo.FindForAssembly(
                typeof(IdtxCoreLoader).Assembly);
            string root = pkgInfo != null
                ? pkgInfo.resolvedPath
                : Path.GetDirectoryName(typeof(IdtxCoreLoader).Assembly.Location);
            if (string.IsNullOrEmpty(root)) return null;
            // "usd~" — trailing tilde keeps Unity's AssetDatabase from importing the
            // OpenUSD schema .usda files; OpenUSD reads the plain path regardless.
            string candidate = Path.Combine(root, "Plugins", "x86_64", "usd~");
            return Directory.Exists(candidate) ? candidate : null;
        }

        private static void PrependEnv(string name, string value)
        {
            string current = Environment.GetEnvironmentVariable(name) ?? string.Empty;
            char sep = Path.PathSeparator;
            foreach (var part in current.Split(sep))
            {
                if (part == value) return;  // already there
            }
            string updated = string.IsNullOrEmpty(current) ? value : $"{value}{sep}{current}";
            Environment.SetEnvironmentVariable(name, updated);
        }
    }

    /// <summary>
    /// ScriptedImporter for V-Sekai-schema USD files. Triggers on any
    /// .usda / .usdc / .usd dropped into Assets/. Output:
    ///   * Main asset:    GameObject reconstructed from the avatar.
    ///   * Sub-assets:    All Meshes and Materials it referenced.
    /// </summary>
    [ScriptedImporter(version: 1, exts: new[] { "usda", "usdc", "usd" })]
    public sealed class IdtxUsdImporter : ScriptedImporter
    {
        [Tooltip("If true, applies VRChat-friendly transforms (axis remap, scale " +
                 "normalization) on import.")]
        public bool vrchatNormalize = false;

        public override void OnImportAsset(AssetImportContext ctx)
        {
            string absPath = Path.GetFullPath(ctx.assetPath);
            IdtxAvatar avatar = null;
            try
            {
                avatar = IdtxAvatar.ImportFromUsd(absPath);
                if (avatar == null)
                {
                    ctx.LogImportError(
                        $"libidtx_core failed to import {ctx.assetPath} " +
                        $"(check PXR_PLUGINPATH_NAME points at the V-Sekai schema dir).");
                    return;
                }

                GameObject go = UnityAvatarBridge.AvatarToGameObject(avatar);
                if (go == null)
                {
                    ctx.LogImportError($"AvatarToGameObject returned null for {ctx.assetPath}.");
                    return;
                }

                if (vrchatNormalize)
                {
                    // Placeholder for the VRChat-side normalization pass
                    // (axis remap, scale to 1m, etc). Lives in a follow-up
                    // commit when the VRChat SDK side is wired in.
                }

                // Register sub-assets so meshes / materials persist
                // alongside the main GameObject.
                RegisterRenderables(ctx, go);

                ctx.AddObjectToAsset("main", go);
                ctx.SetMainObject(go);

                Debug.Log($"[IdtxCore] imported {ctx.assetPath}: " +
                          $"{avatar.MeshCount} mesh(es), {avatar.MaterialCount} material(s).");
            }
            finally
            {
                avatar?.Dispose();
            }
        }

        private static void RegisterRenderables(AssetImportContext ctx, GameObject root)
        {
            // Persist meshes + materials as sub-assets so they show up
            // in the Project view under the .usda and survive a re-import.
            foreach (var mf in root.GetComponentsInChildren<MeshFilter>())
            {
                if (mf.sharedMesh != null) ctx.AddObjectToAsset(
                    "mesh:" + mf.sharedMesh.GetInstanceID(), mf.sharedMesh);
                var mr = mf.GetComponent<MeshRenderer>();
                if (mr != null && mr.sharedMaterial != null)
                    ctx.AddObjectToAsset(
                        "mat:" + mr.sharedMaterial.GetInstanceID(), mr.sharedMaterial);
            }
            foreach (var smr in root.GetComponentsInChildren<SkinnedMeshRenderer>())
            {
                if (smr.sharedMesh != null) ctx.AddObjectToAsset(
                    "smesh:" + smr.sharedMesh.GetInstanceID(), smr.sharedMesh);
                if (smr.sharedMaterial != null)
                    ctx.AddObjectToAsset(
                        "smat:" + smr.sharedMaterial.GetInstanceID(), smr.sharedMaterial);
            }
        }
    }
}
