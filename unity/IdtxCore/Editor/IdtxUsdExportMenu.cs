// Copyright 2026 V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// Editor menu entries that export the selected GameObject hierarchy to an
// OpenUSD file via libidtx_core (UnityAvatarBridge -> idtx_avatar -> USD).
// Available from the main Tools menu and the GameObject right-click menu.

using System.IO;
using IdtxCore.Bridge;
using UnityEditor;
using UnityEngine;

namespace IdtxCore.Editor
{
    public static class IdtxUsdExportMenu
    {
        private const string ToolsMenu = "Tools/IdtxCore/Export Selection to OpenUSD…";
        private const string GameObjectMenu = "GameObject/IdtxCore/Export to OpenUSD…";

        [MenuItem(ToolsMenu, priority = 200)]
        [MenuItem(GameObjectMenu, false, 30)]
        public static void ExportSelection()
        {
            GameObject go = Selection.activeGameObject;
            if (go == null)
            {
                EditorUtility.DisplayDialog("Export to OpenUSD",
                    "Select a GameObject (e.g. an avatar root) to export.", "OK");
                return;
            }

            string path = EditorUtility.SaveFilePanel(
                "Export to OpenUSD", "", go.name + ".usdc", "usdc");
            if (string.IsNullOrEmpty(path))
            {
                return;
            }

            // Extracted textures are written beside the .usd output so the
            // baked absolute paths resolve next to the file.
            UnityAvatarBridge.TextureDir = Path.Combine(
                Path.GetDirectoryName(path),
                Path.GetFileNameWithoutExtension(path) + "_textures");

            IdtxAvatar avatar = null;
            try
            {
                EditorUtility.DisplayProgressBar("Export to OpenUSD",
                    "Building avatar from " + go.name + "…", 0.3f);
                avatar = UnityAvatarBridge.AvatarFromGameObject(go);
                if (avatar == null)
                {
                    EditorUtility.DisplayDialog("Export to OpenUSD",
                        "Failed to build an avatar from " + go.name + ".", "OK");
                    return;
                }

                EditorUtility.DisplayProgressBar("Export to OpenUSD",
                    "Writing " + Path.GetFileName(path) + "…", 0.7f);
                int rc = avatar.ExportToUsd(path);
                if (rc == 0)
                {
                    Debug.Log($"[IdtxCore] Exported {go.name}: {avatar.MeshCount} mesh(es), " +
                              $"{avatar.MaterialCount} material(s) → {path}");
                    EditorUtility.RevealInFinder(path);
                }
                else
                {
                    EditorUtility.DisplayDialog("Export to OpenUSD",
                        $"libidtx_core export failed (code {rc}).", "OK");
                }
            }
            finally
            {
                EditorUtility.ClearProgressBar();
                avatar?.Dispose();
            }
        }

        // Only enabled when a GameObject is selected.
        [MenuItem(ToolsMenu, validate = true)]
        [MenuItem(GameObjectMenu, validate = true)]
        private static bool ValidateExportSelection()
        {
            return Selection.activeGameObject != null;
        }
    }
}
