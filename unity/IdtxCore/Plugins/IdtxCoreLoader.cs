// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// CHI-312: runtime loader that maps the logical [DllImport] name "idtx_core"
// to the real per-platform file libidtx_core.<plat>.<arch>.<ext> — the same
// basename every other host (Godot GDExtension, Blender hook, the viser web
// host, idtxcli) dlopen's. Without this, [DllImport("idtx_core")] would look for
// "idtx_core.dll", which never matches the shipped "libidtx_core.windows.
// x86_64.dll".
//
// Uses System.Runtime.InteropServices.NativeLibrary.SetDllImportResolver
// (netstandard2.1, Unity 2021.2+). The resolver runs once per assembly before
// the first P/Invoke, registered from both the editor load hook and the player
// runtime-init hook.

using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
#if UNITY_EDITOR
using UnityEditor;
#endif
using UnityEngine;

namespace IdtxCore.Native
{
    public static class IdtxCoreLoader
    {
        // Logical name shared by every [DllImport] in IdtxCoreNative.cs.
        public const string LogicalName = Lib.DllName; // "idtx_core"

        private static bool _registered;
        private static IntPtr _handle = IntPtr.Zero;

        /// <summary>The platform-specific file we actually dlopen.</summary>
        public static string PlatformFileName()
        {
#if UNITY_EDITOR_WIN || UNITY_STANDALONE_WIN
            return "libidtx_core.windows.x86_64.dll";
#elif UNITY_EDITOR_OSX || UNITY_STANDALONE_OSX
            return RuntimeInformation.ProcessArchitecture == Architecture.Arm64
                ? "libidtx_core.macos.arm64.dylib"
                : "libidtx_core.macos.x86_64.dylib";
#else
            return "libidtx_core.linux.x86_64.so";
#endif
        }

        // Registered as the import resolver. Returns a loaded module handle for
        // our logical name, or Zero to fall through to the default loader.
        private static IntPtr Resolve(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
        {
            if (libraryName != LogicalName)
                return IntPtr.Zero;
            if (_handle != IntPtr.Zero)
                return _handle;

            string file = PlatformFileName();
            foreach (string dir in SearchDirectories())
            {
                if (string.IsNullOrEmpty(dir))
                    continue;
                string candidate = Path.Combine(dir, file);
                if (File.Exists(candidate) && NativeLibrary.TryLoad(candidate, out _handle))
                    return _handle;
            }

            // Last resort: let the OS loader resolve the bare file name from the
            // process search path (PATH / rpath / Unity's plugin dirs).
            if (NativeLibrary.TryLoad(file, out _handle))
                return _handle;

            Debug.LogError($"[IdtxCore] could not locate native library '{file}' for logical name '{LogicalName}'.");
            return IntPtr.Zero;
        }

        // Candidate directories, most-specific first: next to this managed
        // assembly, then Unity's standard native-plugin locations.
        private static System.Collections.Generic.IEnumerable<string> SearchDirectories()
        {
            string asmDir = null;
            try { asmDir = Path.GetDirectoryName(typeof(IdtxCoreLoader).Assembly.Location); } catch { }
            if (!string.IsNullOrEmpty(asmDir))
                yield return asmDir;

            // Player builds stage native plugins under <data>/Plugins[/<arch>].
            string data = Application.dataPath;
            if (!string.IsNullOrEmpty(data))
            {
                yield return Path.Combine(data, "Plugins", "x86_64");
                yield return Path.Combine(data, "Plugins");
            }
#if UNITY_EDITOR
            // In the editor the lib ships in the package/Assets Plugins folder.
            yield return Path.Combine(Application.dataPath, "Plugins", "x86_64");
#endif
        }

        /// <summary>Register the resolver. Idempotent; safe to call repeatedly.</summary>
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSplashScreen)]
#if UNITY_EDITOR
        [InitializeOnLoadMethod]
#endif
        public static void Register()
        {
            if (_registered)
                return;
            _registered = true;
            try
            {
                NativeLibrary.SetDllImportResolver(typeof(NativeMethods).Assembly, Resolve);
            }
            catch (InvalidOperationException)
            {
                // A resolver was already set for this assembly (e.g. domain
                // reload re-ran both hooks) — harmless.
            }
        }
    }
}
