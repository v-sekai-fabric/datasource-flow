// Copyright 2026 V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// CHI-312 drift gate: verifies every [DllImport] entry point in
// IdtxCore.Native.NativeMethods exists in flow/ports/idtx_core.sigs — the single
// source of truth for the exported ABI. A P/Invoke whose name drifts from the
// ABI (renamed/removed export) fails here instead of at first call.
//
// Run from the menu (Tools/IdtxCore/Validate ABI against .sigs) or headless:
//   Unity -batchmode -quit -executeMethod IdtxCore.EditorTools.IdtxCoreAbiValidator.ValidateBatch

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using UnityEditor;
using UnityEngine;
using IdtxCore.Native;

namespace IdtxCore.EditorTools
{
    public static class IdtxCoreAbiValidator
    {
        [MenuItem("Tools/IdtxCore/Validate ABI against .sigs")]
        public static void ValidateMenu()
        {
            var (ok, msg) = Validate();
            if (ok) Debug.Log(msg);
            else Debug.LogError(msg);
        }

        // Batch entry: exits non-zero on drift so CI fails the build.
        public static void ValidateBatch()
        {
            var (ok, msg) = Validate();
            if (ok) { Debug.Log(msg); EditorApplication.Exit(0); }
            else { Debug.LogError(msg); EditorApplication.Exit(1); }
        }

        public static (bool ok, string message) Validate()
        {
            string sigsPath = FindSigs();
            if (sigsPath == null)
                return (false, "[IdtxCore] ABI validate: could not locate flow/ports/idtx_core.sigs");

            HashSet<string> abi = ParseSigsFunctionNames(sigsPath);
            List<string> entryPoints = CollectEntryPoints();

            var missing = entryPoints.Where(e => !abi.Contains(e)).OrderBy(e => e).ToList();
            if (missing.Count > 0)
            {
                return (false,
                    $"[IdtxCore] ABI DRIFT: {missing.Count} P/Invoke entry point(s) not in {Path.GetFileName(sigsPath)}:\n  " +
                    string.Join("\n  ", missing));
            }
            return (true,
                $"[IdtxCore] ABI OK: all {entryPoints.Count} P/Invoke entry points present in {Path.GetFileName(sigsPath)} ({abi.Count} exports).");
        }

        // The entry point of a [DllImport] is its explicit EntryPoint, else the
        // method name (the C# convention this binding uses).
        private static List<string> CollectEntryPoints()
        {
            var result = new List<string>();
            foreach (var m in typeof(NativeMethods).GetMethods(BindingFlags.Public | BindingFlags.Static))
            {
                var attr = m.GetCustomAttribute<DllImportAttribute>();
                if (attr == null)
                    continue;
                result.Add(string.IsNullOrEmpty(attr.EntryPoint) ? m.Name : attr.EntryPoint);
            }
            return result;
        }

        private static HashSet<string> ParseSigsFunctionNames(string path)
        {
            var names = new HashSet<string>();
            // identifier immediately preceding '(' on a non-comment line
            var rx = new Regex(@"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(");
            foreach (string raw in File.ReadAllLines(path))
            {
                string line = raw.Trim();
                if (line.Length == 0 || line.StartsWith("#"))
                    continue;
                var match = rx.Match(line);
                if (match.Success)
                    names.Add(match.Groups[1].Value);
            }
            return names;
        }

        // Walk up from this package toward the repo root looking for the sigs.
        private static string FindSigs()
        {
            string[] seeds =
            {
                Path.GetDirectoryName(typeof(IdtxCoreAbiValidator).Assembly.Location),
                Application.dataPath,
            };
            foreach (string seed in seeds)
            {
                var dir = string.IsNullOrEmpty(seed) ? null : new DirectoryInfo(seed);
                for (int i = 0; dir != null && i < 12; i++, dir = dir.Parent)
                {
                    string candidate = Path.Combine(dir.FullName, "flow", "ports", "idtx_core.sigs");
                    if (File.Exists(candidate))
                        return candidate;
                    // pre-hexagonal-layout location, kept so the validator still
                    // works against older checkouts of the repo
                    string legacy = Path.Combine(dir.FullName, "core", "idtx_core.sigs");
                    if (File.Exists(legacy))
                        return legacy;
                }
            }
            return null;
        }
    }
}
