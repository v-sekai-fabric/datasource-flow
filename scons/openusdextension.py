import os
import platform
import shutil
import subprocess
import sysconfig
from SCons.Script import Exit

def generate(env):
    env.AddMethod(_generate_usd_extensions_code, 'GenerateUsdExtensionCode')
    env.AddMethod(_build_usd_extension, 'BuildUsdExtension')

def exists(env):
    return True

#-----------------------------------------------------------------------------------------------------------------------
# Generate the openUSD plugin code based on the provided 'schema.usda' file in the 'flow/core/usd/source/' folder
#-----------------------------------------------------------------------------------------------------------------------
def _generate_usd_extensions_code(env):
    print("Generate openUSD Extension code from schema...")

    extension_root = f"flow/core/usd"
    openusd_version = env.get('openusd_version', '')
    openusd_root = os.path.abspath(f"./thirdparty/openusd-{openusd_version}-withPython")
    openusd_env = os.environ.copy()
    openusd_env["USD_ROOT"] = openusd_root
    openusd_env["PYTHONPATH"] = f"{openusd_root}/lib/python"
    openusd_bin_path = f"{openusd_root}/bin"
    openusd_env["PATH"] = f"{openusd_root}/bin{os.pathsep}{openusd_root}/lib{os.pathsep}{os.environ.get('PATH', '')}"

    genschema_cmd = f"{openusd_bin_path}/usdGenSchema.cmd" if platform.system() == "Windows" else f"{openusd_bin_path}/usdGenSchema"
    result = subprocess.run([
        genschema_cmd,
        "schema.usda",
        f"../generated"
    ],
        cwd=os.path.abspath(f"{extension_root}/source"),
        env=openusd_env)

    if result.returncode != 0:
        print(f"Failed to generate openUSD extension code")
        Exit(f"Build aborted due to subprocess failure (exit code: {result.returncode} / {result.stdout} / {result.args}  )")
        
def _build_usd_extension(env):
    print("Building USD Extensions...")

    openusd_version = env.get('openusd_version', '')
    openusd_root = os.path.abspath(f"./thirdparty/openusd-{openusd_version}")

    extension_root = f"./flow/core/usd"
    # compile the usd plugin with the same header and lib files used in the
    # IDTXFlow-SDK to ensure compatibility with it
    idtxflow_sdk_path = "flow/core/include"

    platform_name = env["platform_name"]
    build_target = env["target"]

    extension_env = env.Clone()
    # Parallel MSVC PDB safety: this target's /Zi compiles route through
    # mspdbsrv for the shared vc140.pdb. Pin it to its own endpoint so it
    # never contends with the extension link's /DEBUG pdb (which otherwise
    # surfaces as an intermittent LNK1201 under `-j`). See gdextension.py.
    if platform_name == "windows":
        extension_env["ENV"]["_MSPDBSRV_ENDPOINT_"] = f"idtx_usd_{build_target}"
    # Python include path is needed because the OpenUSD withPython build
    # headers transitively include Python.h (via pySafePython.h / wrap_python.hpp)
    python_include = sysconfig.get_path('include')

    extension_env.Append(CPPPATH=[
        f"{extension_root}/generated",
        #f"{idtxflow_sdk_path}/include",
        f"{openusd_root}/include",
        python_include,
    ])

    extension_env.Append(LIBPATH=[
        #f"{idtxflow_sdk_path}/lib",
        f"{openusd_root}/lib"
    ])

    # oneTBB's library name differs per platform: tbb12.lib (Windows),
    # libtbb.12.dylib (macOS, linked as -ltbb.12), and libtbb.so.12 (Linux).
    # Linux can't -l a versioned .so, so link the libtbb.so dev symlink with
    # plain -ltbb; the recorded DT_NEEDED is still libtbb.so.12 (deployed).
    if platform_name == "windows":
        tbb_lib = "tbb12"
    elif platform_name == "macos":
        tbb_lib = "tbb.12"
    else:
        tbb_lib = "tbb"
    libs = ["usd_ms", tbb_lib]

    # generic build flags
    if platform.system() == "Windows" and (extension_env["CXX"] == "cl" or extension_env["CC"] == "cl"):
        extension_env.Append(CXXFLAGS=['/EHsc', '/GR', '/FS', '/arch:AVX2'])
        extension_env.Append(CCFLAGS=["/O2" if build_target == "template_release" else "/Zi"])
    else:
        extension_env.Append(CXXFLAGS=['-fexceptions', '-frtti', '-g'])
        extension_env.Append(CCFLAGS=["-O3" if build_target == "template_release" else "-g"])

    # Platform-specific configuration
    if platform_name == "linux":
        # Shared library settings
        extension_env.Append(LIBS=libs + ["dl", "pthread", "m"])
        extension_env.Append(CCFLAGS=["-fPIC", "-g", "-frtti"])
        extension_env.Append(LINKFLAGS=["-Wl,-rpath,$ORIGIN"])
        extension_env.Append(CPPDEFINES=["IDTX_EXPORTS"])

    elif platform_name == "windows":
        common_libs = libs # + ["advapi32", "shell32", "ole32"]
        common_defines = ["NOMINMAX", "WIN32_LEAN_AND_MEAN"]

        # Shared library settings
        extension_env.Append(LIBS=common_libs)
        extension_env.Append(CCFLAGS=["/EHsc", "/MD"])  # Use /MD for shared
        extension_env.Append(CPPDEFINES=common_defines + ["IDTX_EXPORTS"])

    elif platform_name == "macos":
        # Shared library settings
        extension_env.Append(LIBS=libs)
        extension_env.Append(CCFLAGS=["-fPIC", "-g", "-Og", "-O0", "-frtti"])
        extension_env.Append(LINKFLAGS=["-framework", "CoreFoundation"])
        extension_env.Append(LINKFLAGS=["-install_name", "@rpath/libidtx_usd.dylib", "-Wl,-rpath,@loader_path"])
        extension_env.Append(LINKFLAGS=["-g"])
        extension_env.Append(CPPDEFINES=["IDTX_EXPORTS"])

    # Source files excluding the python wrapper files as we do not need them
    sources = list(set(extension_env.Glob(f"{extension_root}/generated/*.cpp", exclude=f"{extension_root}/generated/wrap*.cpp")))

    # Set build directories
    build_dir = f"{extension_root}/build/{platform_name}"
    if not os.path.exists(build_dir):
        os.makedirs(build_dir)

    # Output library names
    shared_lib_name = f"libidtx_usd"
    if platform_name == "windows":
        shared_lib_name += ".dll"
    elif platform_name == "macos":
        shared_lib_name += ".dylib"
    else:
        shared_lib_name += ".so"

    # Build the libraries using their respective environments
    shared_library = extension_env.SharedLibrary(f"{build_dir}/{shared_lib_name}", sources)


    # install/copy the header files to the shared include directory
    include_dest = f"{extension_root}/include/idtx"
    lib_dest = f"{extension_root}/libs/{platform_name}"
    install_header = extension_env.Install(include_dest, extension_env.Glob(f"{extension_root}/generated/*.h"))
    install_libs = extension_env.Install(lib_dest, shared_library)

    # Build the extension library and copy the created header files
    extension_env.Default(shared_library, install_header + install_libs)
    extension_env.AddPostAction(shared_library, _copy_plugin_files)

def _copy_plugin_files(target, source, env):
    source_dir = f"./flow/core/usd/generated"
    target_dir = "./flow/core/usd/plugin/idtx/resources"
    shutil.copy(f"{source_dir}/generatedSchema.usda", f"{target_dir}")
    shutil.copy(f"{source_dir}/plugInfo.json",f"{target_dir}")