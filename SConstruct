# Build Script for the IDTXFlow GDExtension
import os
import platform
import shutil

from  SCons.Environment import Environment
from SCons.Script import ARGUMENTS

# USD Version configuration
openusd_version = "26.05"

# configure the main environment to use the different tools to build all we need
env = Environment(
    ENV=os.environ.copy(),
    tools=[
    	"default",
    	"mdlsdk",
    	"godotcpp",
    	"gdextension",
    	"openusd",
    	"openusdextension",
    	"ixwebsocket",
    	"godot_scn_writers",
    	"generate_stubs",
    	"idtxcore",
    	"idtxcli",
    	"idtxflow_ext",
    	"idtxflow_sdk"
    ],
    toolpath=["scons"],
    MSVC_VERSION='14.3',
    PATH=os.environ.get("PATH", "")
)

if platform.system() == "Windows":
    env["PLATFORM"] = "windows"
elif platform.system() == "Darwin":
    env["PLATFORM"] = "macos"
else:
    env["PLATFORM"] = "linux"

env['platform_name'] = env["PLATFORM"]
arch = platform.machine().lower()
if arch in ("aarch64", "arm64"):
    arch = "arm64"
env['arch'] = ARGUMENTS.get('arch', arch )
# default build target should be debug
env['target'] = ARGUMENTS.get('target', 'template_debug')
# float precision of the Godot build we target: 'single' (official builds)
# or 'double' (e.g. the V-Sekai engine fork). godot-cpp reads the same
# `precision=` argument itself; we keep it on the env so gdextension.py can
# name the output DLL per the idtxflow.gdextension manifest
# (libidtxflow.<plat>.<target>.<arch>[.double].<ext>).
env['precision'] = ARGUMENTS.get('precision', 'single')

if platform.system() == "Windows" and (env["CXX"] == "cl" or env["CC"] == "cl"):
    # MSVC: Enable C++20
    env.Append(CXXFLAGS=['/std:c++20'])
else:
    # GCC/Clang: Enable C++20
    env.Append(CXXFLAGS=['-std=c++20'])

env['openusd_version'] = openusd_version

# Optionally emit compile_commands.json for clangd / IDE include resolution.
# Enabled with `scons compiledb=yes`. The compilation_db tool hooks the object
# builders' emitters, so it MUST be added here on the base env — BEFORE the
# per-target env.Clone() calls in BuildGodotCPP / BuildIdtxCore / BuildGdExtension
# — so every clone inherits the emitter and feeds the one shared database.
emit_compiledb = ARGUMENTS.get('compiledb', 'no') not in ('0', 'no', 'false', '')
if emit_compiledb:
    env.Tool('compilation_db')

# Optional compiler caching via sccache. Enabled when USE_SCCACHE is set and
# sccache is on PATH (CI wires it to the GitHub Actions cache backend; see
# .github/workflows). We prepend sccache to the compile COMMAND strings rather
# than to CC/CXX, so env["CXX"] stays "cl"/"g++" and every `== "cl"` check in the
# SConstruct and scons/*.py keeps working. Done on the base env before any
# BuildXxx()/Clone() call so all per-target environments inherit it.
if os.environ.get('USE_SCCACHE', '') not in ('', '0', 'no', 'false'):
    sccache = shutil.which('sccache')
    if sccache:
        launcher = '"%s" ' % sccache
        for com in ('CCCOM', 'CXXCOM', 'SHCCCOM', 'SHCXXCOM'):
            command = env.get(com)
            if command and 'sccache' not in command:
                env[com] = launcher + command
        print('sccache enabled for compilation: %s' % sccache)
    else:
        print('USE_SCCACHE set but sccache not found on PATH; building without it')

# download and build IXWebSocket from source as a static library
env.BuildIXWebSocket()
# download and build openUSD from source without python support, as we don't need it and it will speed up the build process significantly
env.BuildOpenUSD(with_python_support=False)
env.BuildOpenUSD(with_python_support=True)  # with python support, to be able to generate the usd plugin code
# generate the openUSD extension (plugin) code
env.GenerateUsdExtensionCode()
# compile the openUSD extension into it's library
env.BuildUsdExtension()
# download NVIDIA's mdlSdk
env.DownloadMdlSdk()
# download and build the Godot C++ bindings
env = env.BuildGodotCPP()
# Run LeanSlang → Slang → slangc to emit the Godot .scn binary writer
# C++ source BEFORE BuildIdtxCore, so the emitted .cpp is on the source
# list when libidtx_core compiles. Silently skips with a clear message
# if `lake` or `slangc` are not on PATH — in that case
# idtx_core_export_avatar_to_scn() returns code 99 at runtime.
env.GenerateGodotScnWriters()
# Build the engine-agnostic C ABI core (idtx_core) — both libidtxflow
# (Godot) and the future Unity P/Invoke assembly link against this.
env.BuildIdtxCore(static=ARGUMENTS.get('idtx_static', '1') != '0')
# Emit the dlopen dispatch table for the core C ABI from
# flow/ports/idtx_core.sigs (POSIX stubs / Windows .def), and gate .sigs<->
# header ABI drift. Runs after BuildIdtxCore so the Windows .def can be
# named for the freshly-resolved libidtx_core artifact. Hosts that load
# core at runtime (instead of linking it) compile the emitted table; the
# standalone build itself just regenerates + drift-checks it.
env.GenerateCoreStubs()
# Standalone CLI for smoke-testing the avatar pipeline against the core.
env.BuildIdtxCli()
# Build the extension bootstrap static library (for dependent third-party extensions)
env.BuildExtBootstrapLib()
# finally build the GDExtension itself, which will link against the previously built OpenUSD and Godot C++ bindings
env.BuildGdExtension()
# with the GDExtension built, we can grab everything that is required to form an IDTXFlowGodotExtension SDK to implement
# an extension of this very GDExtension
env.ComposeIdtxFlowGodotSDK()

# Write the captured compile commands to compile_commands.json at the repo root
# and fold it into the default targets, so `scons compiledb=yes` refreshes it as
# part of the normal build. `env` here is the post-godot-cpp clone, which carries
# the CompilationDatabase builder inherited from the base env above.
if emit_compiledb:
    compdb = env.CompilationDatabase('compile_commands.json')
    env.Alias('compiledb', compdb)
    env.Default(compdb)