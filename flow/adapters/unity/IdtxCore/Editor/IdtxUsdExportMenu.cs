// Copyright 2026 V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// Editor menu entries that export the selected GameObject hierarchy to an
// OpenUSD file via libidtx_core (UnityAvatarBridge -> idtx_avatar -> USD).
// Three flavours are offered: a plain crate (.usdc), a self-contained package
// (.usdz, which the core builds by writing a temp .usdc and repackaging it with
// every referenced texture), and "both" which writes the crate and the package
// from a single avatar build. Available from the main Tools menu and the
// GameObject right-click menu.

using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using IdtxCore.Bridge;
using UnityEditor;
using UnityEngine;

namespace IdtxCore.Editor
{
    public static class IdtxUsdExportMenu
    {
        private const string ToolsUsd     = "Tools/IdtxCore/Export Selection to OpenUSD…";
        private const string ToolsUsdz    = "Tools/IdtxCore/Export Selection to OpenUSD Package (.usdz)…";
        private const string ToolsBoth    = "Tools/IdtxCore/Export Selection to OpenUSD + USDZ…";
        private const string ObjectUsd    = "GameObject/IdtxCore/Export to OpenUSD…";
        private const string ObjectUsdz   = "GameObject/IdtxCore/Export to OpenUSD Package (.usdz)…";
        private const string ObjectBoth   = "GameObject/IdtxCore/Export to OpenUSD + USDZ…";

        // ----- plain OpenUSD crate (.usdc) -----------------------------------
        [MenuItem(ToolsUsd, priority = 200)]
        [MenuItem(ObjectUsd, false, 30)]
        public static void ExportUsd()
        {
            ExportSelection("usdc", alsoPackage: false);
        }

        // ----- self-contained OpenUSD package (.usdz) ------------------------
        [MenuItem(ToolsUsdz, priority = 201)]
        [MenuItem(ObjectUsdz, false, 31)]
        public static void ExportUsdz()
        {
            ExportSelection("usdz", alsoPackage: false);
        }

        // ----- both the crate and the package in one build -------------------
        [MenuItem(ToolsBoth, priority = 202)]
        [MenuItem(ObjectBoth, false, 32)]
        public static void ExportBoth()
        {
            ExportSelection("usdc", alsoPackage: true);
        }

        // Build the avatar from the selection once and write it to one or both
        // targets. `extension` is the format the save dialog asks for; when
        // `alsoPackage` is set, a sibling .usdz is written from the same avatar.
        private static void ExportSelection(string extension, bool alsoPackage)
        {
            GameObject go = Selection.activeGameObject;
            if (go == null)
            {
                EditorUtility.DisplayDialog("Export to OpenUSD",
                    "Select a GameObject (e.g. an avatar root) to export.", "OK");
                return;
            }

            string path = EditorUtility.SaveFilePanel(
                "Export to OpenUSD", "", go.name + "." + extension, extension);
            if (string.IsNullOrEmpty(path))
            {
                return;
            }

            // Collect the target paths. "Both" pairs the chosen .usdc with a
            // .usdz sibling of the same base name.
            List<string> targets = new List<string> { path };
            if (alsoPackage)
            {
                string usdzPath = Path.Combine(
                    Path.GetDirectoryName(path),
                    Path.GetFileNameWithoutExtension(path) + ".usdz");
                targets.Add(usdzPath);
            }

            // Extracted textures are written beside the .usd output so the baked
            // relative paths resolve next to the crate; the .usdz packager then
            // collects them into the archive.
            UnityAvatarBridge.TextureDir = Path.Combine(
                Path.GetDirectoryName(path),
                Path.GetFileNameWithoutExtension(path) + "_textures");

            IdtxAvatar avatar = null;
            GameObject baked = null;
            bool isClone = false;
            try
            {
                // Bake non-destructively first so the exported avatar matches the
                // in-game build: Modular Avatar / VRCFury merge every outfit into a
                // SINGLE armature and retarget the bones. Without this the source
                // carries each outfit as its own skeleton, so the avatar exports as
                // multiple skeletons (and clothing rides the wrong, un-retargeted
                // bones). The bake runs on a throwaway clone; we export only it.
                EditorUtility.DisplayProgressBar("Export to OpenUSD",
                    "Baking avatar (Modular Avatar / VRCFury)…", 0.1f);
                baked = BakeForExport(go, out isClone);

                EditorUtility.DisplayProgressBar("Export to OpenUSD",
                    "Building avatar from " + baked.name + "…", 0.2f);
                avatar = UnityAvatarBridge.AvatarFromGameObject(baked);
                if (avatar == null)
                {
                    EditorUtility.DisplayDialog("Export to OpenUSD",
                        "Failed to build an avatar from " + go.name + ".", "OK");
                    return;
                }

                List<string> written = new List<string>();
                for (int i = 0; i < targets.Count; ++i)
                {
                    string target = targets[i];
                    EditorUtility.DisplayProgressBar("Export to OpenUSD",
                        "Writing " + Path.GetFileName(target) + "…",
                        0.4f + 0.5f * (i / (float)targets.Count));
                    int rc = avatar.ExportToUsd(target);
                    if (rc != 0)
                    {
                        EditorUtility.DisplayDialog("Export to OpenUSD",
                            $"libidtx_core export of {Path.GetFileName(target)} failed (code {rc}).", "OK");
                        return;
                    }
                    written.Add(target);
                }

                Debug.Log($"[IdtxCore] Exported {go.name}: {avatar.MeshCount} mesh(es), " +
                          $"{avatar.MaterialCount} material(s) → {string.Join(", ", written)}");
                EditorUtility.RevealInFinder(written[written.Count - 1]);
            }
            finally
            {
                EditorUtility.ClearProgressBar();
                avatar?.Dispose();
                if (isClone && baked != null)
                {
                    UnityEngine.Object.DestroyImmediate(baked);
                }
            }
        }

        // Run the project's non-destructive avatar pipeline on a clone of `source`
        // and return the baked result to export. Mirrors Modular Avatar's "Manual
        // Bake Avatar": NDMF's AvatarProcessor.ProcessAvatar runs every registered
        // NDMF plugin — Modular Avatar AND VRCFury (both ship as NDMF plugins) — in
        // the correct order, merging outfits into one armature. Invoked via
        // reflection so the IdtxCore package carries no hard dependency on those
        // optional packages: when NDMF is absent, the source is exported unchanged.
        // `isClone` is set true when a clone was created (the caller destroys it).
        private static GameObject BakeForExport(GameObject source, out bool isClone)
        {
            isClone = false;

            Type processor = FindType("nadena.dev.ndmf.AvatarProcessor");
            MethodInfo process = processor?.GetMethod(
                "ProcessAvatar", BindingFlags.Public | BindingFlags.Static,
                null, new[] { typeof(GameObject) }, null);
            if (process == null)
            {
                // No NDMF in this project: nothing to bake, export the source as-is.
                Debug.Log("[IdtxCore] No NDMF/Modular Avatar/VRCFury pipeline found; " +
                          "exporting the avatar without baking.");
                return source;
            }

            GameObject clone = UnityEngine.Object.Instantiate(source);
            clone.name = source.name;   // drop the "(Clone)" suffix for clean prim names
            clone.SetActive(true);
            try
            {
                process.Invoke(null, new object[] { clone });
            }
            catch (Exception e)
            {
                UnityEngine.Object.DestroyImmediate(clone);
                throw new Exception("NDMF bake (Modular Avatar / VRCFury) failed: " +
                                    (e.InnerException ?? e).Message);
            }
            isClone = true;
            Debug.Log("[IdtxCore] Baked avatar via NDMF (Modular Avatar / VRCFury) before export.");
            return clone;
        }

        // First loaded type matching the full name, or null. Used so optional
        // avatar-framework assemblies are referenced only when actually present.
        private static Type FindType(string fullName)
        {
            foreach (Assembly a in AppDomain.CurrentDomain.GetAssemblies())
            {
                Type t = a.GetType(fullName);
                if (t != null)
                {
                    return t;
                }
            }
            return null;
        }

        // The menu entries are only enabled when a GameObject is selected.
        [MenuItem(ToolsUsd, validate = true)]
        [MenuItem(ToolsUsdz, validate = true)]
        [MenuItem(ToolsBoth, validate = true)]
        [MenuItem(ObjectUsd, validate = true)]
        [MenuItem(ObjectUsdz, validate = true)]
        [MenuItem(ObjectBoth, validate = true)]
        private static bool ValidateExportSelection()
        {
            return Selection.activeGameObject != null;
        }
    }
}
