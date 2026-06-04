// Copyright 2026 The openusd-fabric authors.
// SPDX-License-Identifier: MIT
//
// Bidirectional SCSS <-> VRM 1.0 MToon parameter bridge for Unity.
//
// Reads the canonical JSON table at maps/scss_mtoon_map.json (produced
// by lean/EmitArtifacts.lean from Fabric.VrmUpgrade.ScssMToon) and
// applies the named per-entry value transforms to translate a Unity
// Material's shader properties in either direction.
//
// Usage from the V-Sekai usd-converter-for-vrchat fork:
//
//   // SCSS -> MToon (VRC upload prep, V-Sekai schema mapper)
//   var mtoonValues = ScssMToonBridge.ScssToMToon(scssMaterial);
//   ApplyMToonToMaterial(univrm10Material, mtoonValues);
//
//   // MToon -> SCSS (VRM import to VRC editor)
//   var scssValues = ScssMToonBridge.MToonToScss(mtoonMaterial);
//   ApplyScssToMaterial(scssMaterial, scssValues);
//
// This file lives in openusd-fabric/unity/Editor/ScssMToon/ and gets
// copied or referenced from the V-Sekai usd-converter-for-vrchat fork.
// The two repos must point at the same maps/scss_mtoon_map.json to
// guarantee Blender, Godot, and Unity agree on the wire conversion.

using System;
using System.Collections.Generic;
using System.IO;
using UnityEngine;
using UnityEditor;

namespace VSekai.UsdFabric.Editor.ScssMToon
{
    public static class ScssMToonBridge
    {
        const string MapPathFromAssets = "maps/scss_mtoon_map.json";

        // Outline mode token <-> SCSS enum index.
        static readonly string[] OutlineModeTokens =
            { "none", "screenCoordinates", "worldCoordinates" };

        /// <summary>Translate a SCSS-shader material's properties into the
        /// MToon-equivalent value dictionary. Returns keys named after VRM 1.0
        /// MToon spec fields (e.g. "shadeColorFactor", "outlineWidthMode").</summary>
        public static Dictionary<string, object> ScssToMToon(Material scss)
        {
            if (scss == null) throw new ArgumentNullException(nameof(scss));
            var map = LoadMap();
            var result = new Dictionary<string, object>();
            foreach (var entry in map.entries)
            {
                if (!scss.HasProperty(entry.scss)) continue;
                switch (entry.xform)
                {
                    case "color4_identity":
                        result[entry.mtoon] = ToFloat4(scss.GetColor(entry.scss));
                        break;
                    case "color3_identity":
                        result[entry.mtoon] = ToFloat3(scss.GetColor(entry.scss));
                        break;
                    case "float_identity":
                        result[entry.mtoon] = scss.GetFloat(entry.scss);
                        break;
                    case "color3_via_shadow":
                    {
                        float shadow = scss.HasProperty("_Shadow") ? scss.GetFloat("_Shadow") : 0.5f;
                        float f = 1.0f - shadow * 0.5f;
                        var c = scss.GetColor(entry.scss);
                        result[entry.mtoon] = new[] { c.r * f, c.g * f, c.b * f };
                        break;
                    }
                    case "fresnel_width_to_power":
                        result[entry.mtoon] = FresnelWidthToPower(scss.GetFloat(entry.scss));
                        break;
                    case "color3_times_strength":
                    {
                        float s = scss.HasProperty("_Matcap1Strength")
                            ? scss.GetFloat("_Matcap1Strength") : 1.0f;
                        var c = scss.GetColor(entry.scss);
                        result[entry.mtoon] = new[] { c.r * s, c.g * s, c.b * s };
                        break;
                    }
                    case "outline_mode_enum":
                    {
                        int i = Mathf.RoundToInt(scss.GetFloat(entry.scss));
                        result[entry.mtoon] = (i >= 0 && i < OutlineModeTokens.Length)
                            ? OutlineModeTokens[i] : "screenCoordinates";
                        break;
                    }
                    case "alpha_sharp_to_zwrite":
                        result[entry.mtoon] = scss.GetFloat(entry.scss) >= 1.5f;
                        break;
                    default:
                        Debug.LogWarning($"ScssMToonBridge: unknown xform {entry.xform} on {entry.scss}");
                        break;
                }
            }
            return result;
        }

        /// <summary>Inverse: translate an MToon-keyed value dictionary back to a
        /// SCSS-property dictionary. The reverse pins SCSS-only knobs
        /// (`_Shadow`, `_Matcap1Strength`) at the spec-documented defaults.</summary>
        public static Dictionary<string, object> MToonToScss(IDictionary<string, object> mtoon)
        {
            if (mtoon == null) throw new ArgumentNullException(nameof(mtoon));
            var map = LoadMap();
            var result = new Dictionary<string, object>();
            foreach (var entry in map.entries)
            {
                if (!mtoon.TryGetValue(entry.mtoon, out var v)) continue;
                switch (entry.xform)
                {
                    case "color4_identity":
                    case "color3_identity":
                        result[entry.scss] = v; // already in canonical form
                        break;
                    case "float_identity":
                        result[entry.scss] = Convert.ToSingle(v);
                        break;
                    case "color3_via_shadow":
                    {
                        // The Lean spec pins _Shadow=0.5 on the reverse path;
                        // mult factor = 0.75. Invert by dividing, cap at 1.
                        var f3 = ToFloat3Array(v);
                        result[entry.scss] = new[] {
                            Mathf.Min(1f, f3[0] / 0.75f),
                            Mathf.Min(1f, f3[1] / 0.75f),
                            Mathf.Min(1f, f3[2] / 0.75f),
                        };
                        break;
                    }
                    case "fresnel_width_to_power":
                        result[entry.scss] = FresnelPowerToWidth(Convert.ToSingle(v));
                        break;
                    case "color3_times_strength":
                        result[entry.scss] = ToFloat3Array(v); // strength pinned at 1.0
                        break;
                    case "outline_mode_enum":
                    {
                        var token = v as string ?? "screenCoordinates";
                        int idx = Array.IndexOf(OutlineModeTokens, token);
                        result[entry.scss] = (float)(idx < 0 ? 1 : idx);
                        break;
                    }
                    case "alpha_sharp_to_zwrite":
                        result[entry.scss] = ((bool)v) ? 2.0f : 0.0f;
                        break;
                }
            }
            result["_Shadow"] = result.TryGetValue("_Shadow", out var s) ? s : (object)0.5f;
            result["_Matcap1Strength"] = result.TryGetValue("_Matcap1Strength", out var m1) ? m1 : (object)1.0f;
            return result;
        }

        // ----------------------------------------------------------------
        // Map loading + serializable model
        // ----------------------------------------------------------------

        [Serializable] public class MapEntry
        {
            public string scss;
            public string mtoon;
            public string xform;
            public bool lossless;
            public string lossy_reason;
        }
        [Serializable] public class MapDoc
        {
            public int version;
            public string comment;
            public List<MapEntry> entries;
            public List<string> lossy_mtoon_only;
            public List<string> lossy_scss_only;
        }

        static MapDoc _cachedMap;
        static MapDoc LoadMap()
        {
            if (_cachedMap != null) return _cachedMap;
            var assetsDir = Application.dataPath;
            var repoRoot = Path.GetFullPath(Path.Combine(assetsDir, ".."));
            // Candidate paths: repo root sibling, package install location,
            // and an Assets/-relative fallback that the openusd-fabric UPM
            // would populate when distributed via the V-Sekai scoped registry.
            string[] candidates =
            {
                Path.Combine(repoRoot, MapPathFromAssets),
                Path.Combine(assetsDir, "openusd-fabric", MapPathFromAssets),
                Path.Combine(assetsDir, "openusd_fabric", MapPathFromAssets),
            };
            foreach (var p in candidates)
            {
                if (File.Exists(p))
                {
                    _cachedMap = JsonUtility.FromJson<MapDoc>(File.ReadAllText(p));
                    if (_cachedMap == null)
                        throw new InvalidDataException($"ScssMToonBridge: failed to parse {p}");
                    return _cachedMap;
                }
            }
            throw new FileNotFoundException(
                "ScssMToonBridge: could not locate maps/scss_mtoon_map.json under " +
                string.Join(", ", candidates));
        }

        // ----------------------------------------------------------------
        // Transforms (match scripts/scss_mtoon.py exactly)
        // ----------------------------------------------------------------

        static float FresnelWidthToPower(float w)
        {
            w = Mathf.Clamp(w, 0f, 20f);
            if (w <= 0f)  return 8f;
            if (w >= 20f) return 0.25f;
            return 8f / (1f + w);
        }
        static float FresnelPowerToWidth(float p)
        {
            p = Mathf.Clamp(p, 0.25f, 8f);
            if (p <= 0.25f) return 20f;
            if (p >= 8f)    return 0f;
            return (8f / p) - 1f;
        }

        static float[] ToFloat4(Color c) => new[] { c.r, c.g, c.b, c.a };
        static float[] ToFloat3(Color c) => new[] { c.r, c.g, c.b };
        static float[] ToFloat3Array(object v)
        {
            if (v is float[] fa) return fa;
            if (v is List<object> lo)
                return lo.ConvertAll(o => Convert.ToSingle(o)).ToArray();
            if (v is Color c) return new[] { c.r, c.g, c.b };
            throw new InvalidCastException(
                $"Cannot read float3 from {v?.GetType().FullName ?? "null"}");
        }
    }
}
