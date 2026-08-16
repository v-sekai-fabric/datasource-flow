#!/usr/bin/env python3
# Copyright 2026 V-Sekai contributors.
# SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
"""Fail the build if the deployed Unity native plugin is incomplete.

This guards the exact regression that silently broke USD / USDZ export from
Unity. `idtx_core.dll`'s import table references `usd_ms.dll`, `tbb12.dll`
and `libidtx_usd.dll`; the OS loader resolves them from the same directory.
OpenUSD additionally needs its plugin registry (the `usd~/` tree, pointed at
by PXR_PLUGINPATH_NAME) to create a stage during export. If ANY companion or
registry piece is missing, Unity's first P/Invoke throws
`DllNotFoundException("idtx_core")` with no hint at the real cause, and the
export fails.

The historical failure had two layers, both checked here:
  1. scons silently skipped deploying libidtx_usd.dll (a graph-time
     os.path.exists guard that ran before the lib was built).
  2. .gitignore's `*.dll` meant only idtx_core.dll + usd_ms.dll were ever
     committed, so a fresh clone was missing tbb12.dll + libidtx_usd.dll.

Run after a `scons unity_plugin` build (or against a committed package) to
assert the folder is complete. Exits non-zero, listing every missing piece.
"""
import argparse
import os
import platform
import sys


def detect_arch():
    """Match SConstruct's arch naming: arm64 for Apple Silicon / aarch64,
    x86_64 otherwise. The build deploys companions to Plugins/<arch>, so the
    verifier must look under the same arch the build actually produced (e.g.
    macos-latest runners are arm64, not x86_64)."""
    machine = platform.machine().lower()
    if machine in ("arm64", "aarch64"):
        return "arm64"
    return "x86_64"

# Required native companions per platform, by their DEPLOYED filenames (the
# core is installed under the plain logical name `idtx_core.<ext>`). Mirrors
# the companion lists in scons/idtxcore.py.
COMPANIONS = {
    "windows": ["idtx_core.dll", "usd_ms.dll", "tbb12.dll", "libidtx_usd.dll"],
    "macos":   ["idtx_core.dylib", "libusd_ms.dylib", "libidtx_usd.dylib"],
    "linux":   ["idtx_core.so", "libusd_ms.so", "libtbb.so.12", "libidtx_usd.so"],
}

# OpenUSD plugin registry that must ship beside the core under usd~/. The
# master plugInfo.json Includes "*/resources/", so each listed plugin needs a
# resources/plugInfo.json. idtx_resolver (res://, user://) and vSekaiUsd (the
# codeless V-Sekai applied schemas) are the project's own additions.
USD_REGISTRY_MASTER = "plugInfo.json"
USD_REGISTRY_PLUGINS = [
    "ar", "sdf", "usd", "usdGeom", "usdShade", "usdSkel",
    "idtx_resolver", "vSekaiUsd",
]


def detect_platform():
    if sys.platform.startswith("win"):
        return "windows"
    if sys.platform == "darwin":
        return "macos"
    return "linux"


def check(plugin_root, platform_name, arch):
    arch_dir = os.path.join(plugin_root, arch)
    missing = []

    if not os.path.isdir(arch_dir):
        return [f"plugin arch directory does not exist: {arch_dir}"]

    for name in COMPANIONS.get(platform_name, []):
        path = os.path.join(arch_dir, name)
        if not os.path.isfile(path):
            missing.append(f"native companion missing: {os.path.relpath(path, plugin_root)}")

    usd_dir = os.path.join(arch_dir, "usd~")
    if not os.path.isdir(usd_dir):
        missing.append(f"OpenUSD plugin registry missing: {os.path.relpath(usd_dir, plugin_root)} "
                       "(PXR_PLUGINPATH_NAME target — without it stage creation aborts the editor)")
    else:
        master = os.path.join(usd_dir, USD_REGISTRY_MASTER)
        if not os.path.isfile(master):
            missing.append(f"USD registry master missing: usd~/{USD_REGISTRY_MASTER}")
        for plugin in USD_REGISTRY_PLUGINS:
            res = os.path.join(usd_dir, plugin, "resources", "plugInfo.json")
            if not os.path.isfile(res):
                missing.append(f"USD plugin registration missing: usd~/{plugin}/resources/plugInfo.json")

    return missing


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--plugin-root",
                    default=os.path.join("flow", "adapters", "unity", "IdtxCore", "Plugins"),
                    help="Path to the Unity package's Plugins/ directory "
                         "(default: flow/adapters/unity/IdtxCore/Plugins).")
    ap.add_argument("--platform", choices=sorted(COMPANIONS),
                    default=detect_platform(),
                    help="Target platform to verify (default: host platform).")
    ap.add_argument("--arch", default=detect_arch(),
                    help="Plugin arch subdirectory to verify (default: host "
                         "arch — arm64 on Apple Silicon, else x86_64).")
    args = ap.parse_args()

    print(f"Verifying Unity plugin completeness: root={args.plugin_root!r} "
          f"platform={args.platform} arch={args.arch}")
    missing = check(args.plugin_root, args.platform, args.arch)

    if missing:
        print("\nFAIL: the deployed Unity plugin is INCOMPLETE — USD/USDZ export "
              "would throw DllNotFoundException(\"idtx_core\"):", file=sys.stderr)
        for m in missing:
            print(f"  - {m}", file=sys.stderr)
        print("\nBuild the full deploy with `scons unity_plugin` and ensure the "
              "USD extension + companions are produced before packaging.",
              file=sys.stderr)
        return 1

    print("OK: all required native companions and the OpenUSD plugin registry are present.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
