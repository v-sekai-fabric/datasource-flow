"""
ctypes binding for libidtx_core — the shared C ABI every host dlopens
(CHI-312). Engine-agnostic: identical to the Blender binding
(openusd-fabric/blender/idtx_core_ctypes.py). The Unreal integration
(idtx_unreal_export.py) uses it from UE's built-in Python to drive
idtx_core_export_avatar_to_usd_ex with NO C++ plugin — "merely the .sigs":
the generated dlopen table + this thin binding are the whole integration.

Signatures stay faithful to core/idtx_core.sigs.

Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
"""

from __future__ import annotations

import ctypes
import os
import sys
from ctypes import POINTER, c_char_p, c_float, c_int32, c_void_p
from pathlib import Path

# idtx_usd_export_mode_t
NEW_FLAT, OVERLAY, LAYER_ONLY, FLATTEN = 0, 1, 2, 3


class IdtxUsdExportOpts(ctypes.Structure):
    """Mirror of idtx_usd_export_opts_t (idtx_core.h)."""
    _fields_ = [
        ("mode", c_int32),
        ("source_path", c_char_p),
        ("edit_target_id", c_char_p),
        ("reflect_per_prim", c_int32),
    ]


def _platform_basename() -> str:
    if sys.platform == "win32":
        return "libidtx_core.windows.x86_64.dll"
    if sys.platform == "darwin":
        machine = os.uname().machine.lower()
        arch = "arm64" if machine in ("arm64", "aarch64") else "x86_64"
        return f"libidtx_core.macos.{arch}.dylib"
    return "libidtx_core.linux.x86_64.so"


def find_library(repo_root: Path | None = None) -> Path | None:
    """Locate libidtx_core: IDTX_CORE_DLL env override, installed addon bin,
    then the in-repo build dir."""
    override = os.environ.get("IDTX_CORE_DLL")
    if override and Path(override).exists():
        return Path(override)
    base = _platform_basename()
    root = repo_root or Path(__file__).resolve().parents[2]
    plat = "windows" if sys.platform == "win32" else ("macos" if sys.platform == "darwin" else "linux")
    for c in (
        root / "addons" / "IDTXFlow" / "bin" / plat / base,
        root / "build" / "idtx_core" / base,
    ):
        if c.exists():
            return c
    return None


def _add_dependency_dirs(dll_path: Path, repo_root: Path) -> None:
    dep_dirs = [
        dll_path.parent,
        repo_root / "thirdparty" / "openusd-25.11" / "lib",
        repo_root / "thirdparty" / "openusd-25.11" / "bin",
        repo_root / "usd" / "libs" / "windows",
    ]
    if sys.platform == "win32":
        for d in dep_dirs:
            if d.exists():
                os.add_dll_directory(str(d))
    else:
        key = "DYLD_LIBRARY_PATH" if sys.platform == "darwin" else "LD_LIBRARY_PATH"
        existing = os.environ.get(key, "")
        parts = [str(d) for d in dep_dirs if d.exists()]
        os.environ[key] = os.pathsep.join(parts + ([existing] if existing else []))


def load(repo_root: Path | None = None) -> ctypes.CDLL:
    """Load libidtx_core, declare ABI signatures, idtx_core_init(), return the
    configured CDLL. Raises FileNotFoundError if the lib can't be found."""
    root = repo_root or Path(__file__).resolve().parents[2]
    dll = find_library(root)
    if dll is None:
        raise FileNotFoundError(
            f"libidtx_core not found (looked for {_platform_basename()} under "
            f"addons/IDTXFlow/bin and build/idtx_core; set IDTX_CORE_DLL to override)."
        )
    _add_dependency_dirs(dll, root)
    lib = ctypes.CDLL(str(dll))

    lib.idtx_core_version.restype = c_char_p
    lib.idtx_core_init.argtypes = [c_char_p]
    lib.idtx_core_init.restype = c_int32

    lib.idtx_avatar_create.restype = c_void_p
    lib.idtx_avatar_destroy.argtypes = [c_void_p]
    lib.idtx_avatar_set_name.argtypes = [c_void_p, c_char_p]
    lib.idtx_avatar_set_skeleton.argtypes = [c_void_p, c_void_p]
    lib.idtx_avatar_add_mesh.argtypes = [c_void_p, c_void_p, c_int32]
    lib.idtx_avatar_add_mesh.restype = c_int32
    lib.idtx_avatar_add_material.argtypes = [c_void_p, c_void_p]
    lib.idtx_avatar_add_material.restype = c_int32
    lib.idtx_avatar_set_root_transform.argtypes = [c_void_p, POINTER(c_float)]
    lib.idtx_avatar_set_source_usd_path.argtypes = [c_void_p, c_char_p]
    lib.idtx_avatar_get_source_usd_path.argtypes = [c_void_p]
    lib.idtx_avatar_get_source_usd_path.restype = c_char_p

    lib.idtx_skeleton_create.restype = c_void_p
    lib.idtx_skeleton_add_bone.argtypes = [
        c_void_p, c_char_p, c_int32, POINTER(c_float), POINTER(c_float)]
    lib.idtx_skeleton_add_bone.restype = c_int32

    lib.idtx_mesh_create.restype = c_void_p
    lib.idtx_mesh_set_vertices.argtypes = [
        c_void_p, c_int32, POINTER(c_float), POINTER(c_float),
        POINTER(c_float), POINTER(c_float)]
    lib.idtx_mesh_set_indices.argtypes = [c_void_p, c_int32, POINTER(c_int32)]

    lib.idtx_material_create.restype = c_void_p
    lib.idtx_material_set_base_color.argtypes = [
        c_void_p, c_float, c_float, c_float, c_float]

    lib.idtx_core_export_avatar_to_usd.argtypes = [c_void_p, c_char_p]
    lib.idtx_core_export_avatar_to_usd.restype = c_int32
    lib.idtx_usd_export_opts_init.argtypes = [POINTER(IdtxUsdExportOpts)]
    lib.idtx_core_export_avatar_to_usd_ex.argtypes = [
        c_void_p, c_char_p, POINTER(IdtxUsdExportOpts)]
    lib.idtx_core_export_avatar_to_usd_ex.restype = c_int32
    lib.idtx_core_import_avatar_from_usd.argtypes = [c_char_p]
    lib.idtx_core_import_avatar_from_usd.restype = c_void_p

    lib.idtx_core_init(None)
    return lib


def export_avatar_ex(lib, avatar, out_path, mode=OVERLAY, source_path=None,
                     edit_target_id=None, reflect_per_prim=False) -> int:
    """Drive idtx_core_export_avatar_to_usd_ex. Returns rc (0 == success)."""
    opts = IdtxUsdExportOpts()
    lib.idtx_usd_export_opts_init(ctypes.byref(opts))
    opts.mode = mode
    if source_path is not None:
        opts.source_path = str(source_path).encode("utf-8")
    if edit_target_id is not None:
        opts.edit_target_id = edit_target_id.encode("utf-8")
    opts.reflect_per_prim = 1 if reflect_per_prim else 0
    return lib.idtx_core_export_avatar_to_usd_ex(
        avatar, str(out_path).encode("utf-8"), ctypes.byref(opts))
