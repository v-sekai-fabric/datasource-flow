"""
SCons tool: idtxcore

Builds libidtx_core in TWO configurations from one source tree:

1. **Shared library** (`libidtx_core.<platform>.<arch>.{dll,so,dylib}`)
   — consumed by the GDExtension (addons/IDTXFlow/), the standalone CLI
   (tools/idtxcli/), and any future P/Invoke host. Symbols are
   `__declspec(dllexport)` on Windows, default ELF visibility elsewhere.

2. **Static archive** (`libidtx_core_static.<platform>.<arch>.{lib,a}`)
   — consumed by the multiplayer-fabric-godot engine module, which
   compiles libidtx_core's source directly into the Godot binary
   via a `git subtree --squash` at `thirdparty/idtx_core/`. Symbols
   carry NO import/export decoration (defined via `IDTX_CORE_STATIC`).

Single source list, single compile flags (modulo the export-decoration
define). The two artifacts cannot algorithmically diverge — they are
the same `.cpp` files.

Usage in SConstruct:
    env.BuildIdtxCore()              # both targets, default
    env.BuildIdtxCore(static=False)  # shared only (legacy callers)
    env.BuildIdtxCore(shared=False)  # static only (engine-module CI)
"""
import glob
import os
import platform
import shutil


def generate(env):
    env.AddMethod(_build_idtx_core, 'BuildIdtxCore')


def exists(env):
    return True


def _thirdparty_includes():
    """Discover header dirs for the flat-C third-party deps idtx_core
    compiles against — zstd.h (idtx_chunker / idtx_godot_scn_glue) and
    openssl/evp.h (idtx_aes). The libraries are already linked via LIBS;
    only the headers need to be on CPPPATH. Returned dirs are filtered to
    those that exist, so this is a no-op where a dev shell already has
    them on the INCLUDE env var.

    NOTE: the long-term move is to dlopen libcrypto/zstd through a .sigs
    stub table (see scons/generate_stubs.py) instead of compiling against
    their headers at all — at which point this discovery goes away.
    """
    dirs = []
    # openssl — vcpkg static triplet ships openssl/evp.h under include/.
    dirs += glob.glob("thirdparty/vcpkg/installed/*/include")
    # zstd — derive from the zstd CLI's sibling include/ (scoop/brew/etc),
    # and fall back to scanning scoop app include dirs since the CLI may
    # not be on PATH inside the build shell.
    zstd_exe = shutil.which("zstd")
    if zstd_exe:
        inc = os.path.normpath(os.path.join(os.path.dirname(zstd_exe), "..", "include"))
        dirs.append(inc)
    # Specifically the zstd app — NOT a wildcard over all scoop apps, or a
    # partial zstd.h bundled by another tool (e.g. curl ships a stripped
    # one with a different ZSTD_decompress signature) can shadow the real
    # header on the include search path.
    home = os.path.expanduser("~")
    dirs += glob.glob(os.path.join(home, "scoop", "apps", "zstd", "current", "include"))
    # Keep only dirs that actually hold the headers we need.
    keep = []
    for d in dirs:
        if not os.path.isdir(d):
            continue
        if os.path.isfile(os.path.join(d, "zstd.h")) or \
           os.path.isfile(os.path.join(d, "openssl", "evp.h")):
            keep.append(d)
    return keep


def _thirdparty_libdirs():
    """Discover lib dirs holding zstd.lib / libcrypto.lib for the caibx
    transport + AES link. Mirrors _thirdparty_includes — the vcpkg static
    triplet builds both. Filtered to dirs that actually hold one of them.
    """
    dirs = glob.glob("thirdparty/vcpkg/installed/*/lib")
    keep = []
    for d in dirs:
        if not os.path.isdir(d):
            continue
        if os.path.isfile(os.path.join(d, "zstd.lib")) or \
           os.path.isfile(os.path.join(d, "libcrypto.lib")):
            keep.append(d)
    return keep


def _common_env(env, *, building_dll, static):
    """Configure a build env for one of the two artifacts.

    Shared library build: `building_dll=True, static=False`
    Static archive build: `building_dll=False, static=True`
    """
    platform_name = env["platform_name"]
    build_target = env["target"]
    openusd_version = env.get('openusd_version', '')
    usd_root = f"thirdparty/openusd-{openusd_version}"
    usd_extension_path = "usd"

    cfg_env = env.Clone()

    cfg_env.Append(CPPPATH=[
        "core/include",
        # idtx_scene converter: the engine-agnostic converter framework lives in
        # shared/include (angle includes <idtxflow/...>), the FlatTree target in
        # core/src ("scene/..."), and "core" lets StageConverter's relative
        # `../usd/include/idtx/*` schema include resolve (core/../usd/include).
        "core/src",
        "core",
        "shared/include",
        f"{usd_root}/include",
        f"{usd_extension_path}/include",
        "libs/cgltf",
        "libs/jsmn",
        # ixwebsocket headers for idtx_transport.cpp. The library
        # itself is linked via the existing BuildIXWebSocket() artifact
        # produced upstream in SConstruct.
        "thirdparty/ixwebsocket",
        # slangc-emitted .cpp lives in core/share/godot_scn/ (set by
        # GenerateGodotScnWriters). If absent, fall back to that path
        # anyway — harmless when no .cpp is on the source list.
        env.get('idtx_godot_scn_include', 'core/share/godot_scn'),
        # slang-cpp-prelude.h lives next to the slangc binary; the
        # godot_scn writer .cpp includes it via <angle brackets>.
        env.get('idtx_godot_scn_prelude_dir', ''),
    ])

    # zstd.h (idtx_chunker / idtx_godot_scn_glue) and openssl/evp.h
    # (idtx_aes) — headers for already-linked flat-C deps. Discovered so
    # a clean checkout builds without a hand-primed INCLUDE env var.
    cfg_env.Append(CPPPATH=_thirdparty_includes())

    cfg_env.Append(LIBPATH=[
        f"{usd_root}/lib",
        f"{usd_extension_path}/libs/{platform_name}",
    ])
    # Transport + AES link: the IXWebSocket static lib (HTTP/TLS for
    # idtx_transport) plus zstd/openssl from the vcpkg static triplet.
    # LIBPATHs discovered so the link resolves without a hand-set LIB var.
    ixws_dir = (f"thirdparty/ixwebsocket/build_{platform_name}_{build_target}/Release"
                if platform_name == "windows"
                else f"thirdparty/ixwebsocket/build_{platform_name}_{build_target}")
    cfg_env.Append(LIBPATH=[ixws_dir] + _thirdparty_libdirs())

    cfg_env.Append(LIBS=[
        "usd_ms",
        "tbb12" if platform_name == "windows" else "tbb.12",
        "libidtx_usd",
    ])
    # caibx transport + AES dependencies:
    #   ixwebsocket — HTTP/TLS client (idtx_transport.cpp)
    #   zstd        — .cacnk compress/decompress (idtx_chunker.cpp); OpenUSD
    #                 pulls it transitively but doesn't re-export it.
    #   libssl/libcrypto — TLS + AES-128-GCM (idtx_aes.cpp)
    #   Win32 syslibs — advapi32 (OpenSSL RegisterEventSource), crypt32
    #                   (cert store), user32, ws2_32 (sockets), bcrypt (RAND/BIO).
    if platform_name == "windows":
        cfg_env.Append(LIBS=[
            "ixwebsocket", "zstd", "libssl", "libcrypto",
            "ws2_32", "crypt32", "advapi32", "user32", "bcrypt",
        ])
    else:
        cfg_env.Append(LIBS=[
            "ixwebsocket", "ssl", "crypto", "zstd", "z", "pthread", "dl",
        ])

    if platform.system() == "Windows" and (env["CXX"] == "cl" or env["CC"] == "cl"):
        cfg_env.Append(CXXFLAGS=['/EHsc', '/GR', '/FS', '/std:c++20'])
        cfg_env.Append(CPPDEFINES=["NOMINMAX", "WIN32_LEAN_AND_MEAN"])
        if build_target in ["editor", "template_debug"]:
            cfg_env.Append(CCFLAGS=["/Z7", "/Od", "/MT"])
        else:
            cfg_env.Append(CCFLAGS=["/O2", "/MT"])
    else:
        cfg_env.Append(CXXFLAGS=['-fexceptions', '-frtti', '-std=c++20'])
        cfg_env.Append(CCFLAGS=["-fPIC"])
        cfg_env.Append(CCFLAGS=["-O3" if build_target == "template_release" else "-g"])

    # The two configs diverge ONLY here. IDTX_CORE_BUILDING_DLL turns on
    # dllexport for the shared lib; IDTX_CORE_STATIC suppresses both
    # dllexport AND dllimport for the static archive. Defining both at
    # once is a bug — assert it.
    assert not (building_dll and static), "shared and static configs are mutually exclusive"
    if building_dll:
        cfg_env.Append(CPPDEFINES=["IDTX_CORE_BUILDING_DLL"])
    if static:
        cfg_env.Append(CPPDEFINES=["IDTX_CORE_STATIC"])

    # idtx_export_scn.cpp gates the slangc-emitted writer call behind
    # IDTX_GODOT_SCN_AVAILABLE so the lib still builds when lake/slangc
    # are not on PATH.
    if env.get('idtx_godot_scn_available'):
        cfg_env.Append(CPPDEFINES=["IDTX_GODOT_SCN_AVAILABLE"])

    return cfg_env


def _sources():
    """The single canonical source list. Both artifacts compile these."""
    return [
        "core/src/idtx_core.cpp",
        "core/src/idtx_skeleton.cpp",
        "core/src/idtx_mesh.cpp",
        "core/src/idtx_mesh_quads.cpp",
        "core/src/idtx_material.cpp",
        "core/src/idtx_avatar.cpp",
        "core/src/idtx_physics_collider.cpp",
        "core/src/idtx_springbone.cpp",
        "core/src/string_utils.cpp",
        "core/src/usd_helpers.cpp",
        "core/src/json_writer.cpp",
        "core/src/idtx_export_usd.cpp",
        "core/src/idtx_import_usd.cpp",
        "core/src/idtx_vrm.cpp",
        "core/src/idtx_vrm_import.cpp",
        "core/src/idtx_vrm_springbone_parse.cpp",
        "core/src/vrm_humanoid_bones.cpp",
        "core/src/idtx_chunker.cpp",
        "core/src/idtx_export_scn.cpp",
        "core/src/idtx_transport.cpp",
        "core/src/idtx_aes.cpp",
        # idtx_scene: USD stage -> engine-neutral node tree (FlatTree converter).
        "core/src/idtx_scene.cpp",
    ]


def _build_idtx_core(env, shared=True, static=True):
    print("Building libidtx_core (engine-agnostic C ABI)...")

    platform_name = env["platform_name"]
    build_arch = env["arch"]

    build_dir = "build/idtx_core"
    if not os.path.exists(build_dir):
        os.makedirs(build_dir)

    library_name = f"libidtx_core.{platform_name}.{build_arch}"
    shared_extension = "dll" if platform_name == "windows" else ("dylib" if platform_name == "macos" else "so")
    static_extension = "lib" if platform_name == "windows" else "a"

    sources = list(_sources())

    # The .scn writer functions are emitted by slangc -target cpp from
    # the committed openusd-fabric/lean/shaders/godot_scn.slang (see
    # scons/godot_scn_writers.py). When available, link in both the
    # slangc-emitted entry point AND the C++ glue that constructs the
    # slang runtime buffers and bridges idtx_avatar → bake_scn_kernel.
    if env.get('idtx_godot_scn_available'):
        sources.append(env['idtx_godot_scn_cpp'])
        sources.append("core/src/idtx_godot_scn_glue.cpp")

    targets = []

    shared_lib = None
    if shared:
        shared_env = _common_env(env, building_dll=True, static=False)
        # SCons SharedObject objects must be distinct from the static
        # ones (same .cpp → two different .obj/.o files) — use a
        # variant_dir or duplicated SharedObject calls. Here we call
        # SharedLibrary directly which handles object distinction.
        shared_lib = shared_env.SharedLibrary(
            f"{build_dir}/{library_name}.{shared_extension}",
            sources,
        )
        env['idtx_core_lib_name'] = library_name
        env['idtx_core_lib_dir'] = os.path.abspath(build_dir)
        env['idtx_core_library_node'] = shared_lib
        shared_env.Default(shared_lib)
        targets.append(shared_lib)

    static_lib = None
    if static:
        static_env = _common_env(env, building_dll=False, static=True)
        # SCons compiles each .cpp twice — once per config — because the
        # CPPDEFINES differ (IDTX_CORE_BUILDING_DLL vs IDTX_CORE_STATIC).
        # The two compiles must land on DISTINCT object files or SCons
        # raises "Two environments with different actions for the same
        # target": on Windows SHOBJSUFFIX == OBJSUFFIX == .obj, so the
        # shared and static objects derive the same name from each source
        # and collide. Giving the static config its own object prefix
        # separates them (the `_static` archive name alone does not —
        # that renames the .lib, not the per-source .obj/.o).
        static_env['OBJPREFIX'] = 'static_' + static_env.get('OBJPREFIX', '')
        static_lib = static_env.StaticLibrary(
            f"{build_dir}/{library_name}_static.{static_extension}",
            sources,
        )
        env['idtx_core_static_lib_name'] = f"{library_name}_static"
        env['idtx_core_static_lib_dir'] = os.path.abspath(build_dir)
        env['idtx_core_static_library_node'] = static_lib
        static_env.Default(static_lib)
        targets.append(static_lib)

    # Backwards-compat: callers that only consumed `idtx_core_library_node`
    # still work because the shared lib is built by default.
    return targets[0] if len(targets) == 1 else targets
