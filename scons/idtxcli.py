"""
SCons tool: idtxcli
Builds idtxcli.exe — a standalone command-line driver for libidtx_core.
Useful for smoke-testing the avatar pipeline (USD <-> VRM round trips)
without spinning up Godot or Unity.

Usage in SConstruct:
    env.BuildIdtxCli()
"""
import os
import platform


def generate(env):
    env.AddMethod(_build_idtxcli, 'BuildIdtxCli')


def exists(env):
    return True


def _build_idtxcli(env):
    print("Building idtxcli (libidtx_core CLI driver)...")

    platform_name = env["platform_name"]
    build_target  = env["target"]
    build_arch    = env["arch"]

    cli_env = env.Clone()

    cli_env.Append(CPPPATH=[
        "core/include",
    ])
    cli_env.Append(LIBPATH=[
        "build/idtx_core",
    ])
    cli_env.Append(LIBS=[
        f"libidtx_core.{platform_name}.{build_arch}",
    ])

    if platform.system() == "Windows" and (env["CXX"] == "cl" or env["CC"] == "cl"):
        cli_env.Append(CXXFLAGS=['/EHsc', '/GR', '/FS', '/std:c++20'])
        cli_env.Append(CPPDEFINES=["NOMINMAX", "WIN32_LEAN_AND_MEAN"])
        if build_target in ["editor", "template_debug"]:
            cli_env.Append(CCFLAGS=["/Z7", "/Od", "/MT"])
        else:
            cli_env.Append(CCFLAGS=["/O2", "/MT"])
    else:
        cli_env.Append(CXXFLAGS=['-fexceptions', '-frtti', '-std=c++20'])
        cli_env.Append(CCFLAGS=["-fPIC"])
        cli_env.Append(CCFLAGS=["-O3" if build_target == "template_release" else "-g"])

    sources = ["tools/idtxcli/idtxcli.cpp"]

    exe_name = "idtxcli"
    build_dir = "build/idtxcli"
    if not os.path.exists(build_dir):
        os.makedirs(build_dir)

    program = cli_env.Program(f"{build_dir}/{exe_name}", sources)
    if 'idtx_core_library_node' in env:
        cli_env.Depends(program, env['idtx_core_library_node'])
    cli_env.Default(program)
    return program
