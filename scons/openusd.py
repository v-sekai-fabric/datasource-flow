"""
SCons tool: openusd
Builds the OpenUSD library from source using the provided build scripts.
The built OpenUSD library is a dependency for the IDTXFlow GDExtension. The OpenUSD version
can be configured via the 'openusd_version' variable in the SCons environment. Usually the OpenUSD library is
build without Python support, as the IDTXFlow GDExtension does not require it. However, you can enable Python support
by passing 'with_python_support=True' to the BuildOpenUSD method.

Usage in SConstruct:
    env.BuildOpenUSD(with_python_support=False)  # Set to True to include Python bindings
"""
import os
import shutil
import subprocess
import sys
import sysconfig

from SCons.Script import Exit


def _build_python_info(platform_name):
    """Return the (executable, include_dir, library, version) OpenUSD's
    build_usd.py needs, pointing LIBRARY at the shared libpython that
    actually exists.

    conda-forge's CPython (pinned via pixi) reports
    ``sysconfig LDLIBRARY = libpython<ver>.a`` even though it ships only the
    shared ``libpython<ver>.so``/``.dylib``. build_usd.py's own detection
    then looks for a static ``.a`` that isn't there and CMake's FindPython3
    aborts ("Cannot find the library ... libpython3.14.a"). Passing this via
    ``--build-python-info`` bypasses that detection with the real shared lib.
    """
    version = sysconfig.get_config_var("py_version_short")  # e.g. "3.14"
    include_dir = sysconfig.get_path("include")
    libdir = sysconfig.get_config_var("LIBDIR") or ""
    if platform_name == "windows":
        nodot = sysconfig.get_config_var("py_version_nodot")  # e.g. "314"
        candidates = [os.path.join(sys.base_prefix, "libs", f"python{nodot}.lib")]
    elif platform_name == "macos":
        candidates = [os.path.join(libdir, f"libpython{version}.dylib")]
    else:
        candidates = [os.path.join(libdir, f"libpython{version}.so")]
    library = next((c for c in candidates if os.path.exists(c)), candidates[0])
    return [sys.executable, include_dir, library, version]


def generate(env):
    env.AddMethod(_build_open_usd, 'BuildOpenUSD')

def exists(env):
    return True

def _patch_openusd_vs2026(open_usd_path):
    """Add VS2026 generator support to the cloned OpenUSD build_usd.py.

    Idempotent: skips if already patched. Applies the tracked unified diff
    via `git apply` (the clone is a git repo); falls back to an in-place
    string edit if `git apply` can't apply it.
    """
    build_usd = os.path.join(open_usd_path, "build_scripts", "build_usd.py")
    if not os.path.isfile(build_usd):
        return
    with open(build_usd, encoding="utf-8") as f:
        src = f.read()
    if "IsVisualStudio2026OrGreater" in src:
        return  # already patched

    patch = os.path.abspath(
        os.path.join("scons", "patches", "openusd-vs2026-generator.patch"))
    print("Patching OpenUSD build_usd.py for VS2026 generator support...")
    if os.path.isfile(patch):
        result = subprocess.run(["git", "apply", "-p1", patch], cwd=open_usd_path)
        if result.returncode == 0:
            return

    # Fallback: apply the same two edits directly.
    patched = src.replace(
        "def IsVisualStudio2022OrGreater():",
        "def IsVisualStudio2026OrGreater():\n"
        "    VISUAL_STUDIO_2026_VERSION = (14, 50)\n"
        "    return IsVisualStudioVersionOrGreater(VISUAL_STUDIO_2026_VERSION)\n"
        "def IsVisualStudio2022OrGreater():",
        1,
    ).replace(
        "        if IsVisualStudio2022OrGreater():\n"
        "            generator = \"Visual Studio 17 2022\"",
        "        if IsVisualStudio2026OrGreater():\n"
        "            generator = \"Visual Studio 18 2026\"\n"
        "        elif IsVisualStudio2022OrGreater():\n"
        "            generator = \"Visual Studio 17 2022\"",
        1,
    )
    if patched != src:
        with open(build_usd, "w", encoding="utf-8") as f:
            f.write(patched)


def _build_open_usd(env, with_python_support=False):
    open_usd_version = env.get('openusd_version', '')
    open_usd_path = f"thirdparty/openusd-{open_usd_version}-src"
    print("USD ROOT" + os.environ.get("USD_ROOT", "thirdparty/openusd"))
    
    # check if we have cloned openUSD already
    if not os.path.exists(open_usd_path):
        print("Cloning openUSD...")
        result = subprocess.run([
            "git", "clone", "-b", "v" + open_usd_version, "--recursive", "--depth", "2",
            "https://github.com/PixarAnimationStudios/OpenUSD.git",
            open_usd_path
        ])
        if result.returncode != 0:
            print(f"Failed to clone openUSD repo.")
            Exit(f"Build aborted due to subprocess failure (exit code: {result.returncode})")

    # OpenUSD's build_usd.py picks its CMake VS generator by version range:
    # IsVisualStudio2022OrGreater() is true for VS2026 too, so on a VS2026-
    # only host (e.g. the upgraded GitHub windows runner) it selects the
    # VS2022 generator and CMake then "could not find any instance of Visual
    # Studio". Apply the tracked patch that adds a VS2026 branch. The
    # vendored source is git-ignored, so we re-apply after each clone.
    _patch_openusd_vs2026(open_usd_path)

    platform_name = env["platform_name"]
    build_target = env["target"]

    # check if we have build the openUSD lib already
    open_usd_build_path = f"thirdparty/openusd-{open_usd_version}" if not with_python_support else f"thirdparty/openusd-{open_usd_version}-withPython"
    if platform_name == "windows":
        open_usd_lib = f"{open_usd_build_path}/lib/usd_ms.dll"
    elif platform_name == "macos":
        open_usd_lib = f"{open_usd_build_path}/lib/libusd_ms.dylib"
    else:
        open_usd_lib = f"{open_usd_build_path}/lib/libusd_ms.so"

    # An existing lib alone isn't proof the build (or a restored cache) is
    # complete. The withPython build must also carry usdGenSchema in bin/ —
    # GenerateUsdExtensionCode() invokes it, and it's only installed when
    # Jinja2 was found at OpenUSD configure time. A cache saved before Jinja2
    # was reliably present has the lib but NOT usdGenSchema; treat that as a
    # miss so we rebuild instead of failing later with FileNotFoundError.
    build_complete = os.path.exists(open_usd_lib)
    if build_complete and with_python_support:
        genschema = "usdGenSchema.cmd" if platform_name == "windows" else "usdGenSchema"
        build_complete = os.path.exists(f"{open_usd_build_path}/bin/{genschema}")

    if not build_complete:
        print("Building openUSD...")
        openusd_env = {}
        # when building openUSD we need to ensure that proper env-vars are set
        # on Windows
        if platform_name == "windows":
            _get_windows_msvc_env(openusd_env)
        else:
            # ensure the current system path is passed to the openUSD python build process
            openusd_env["PATH"] = os.environ.get("PATH", "")

        # Propagate the sccache client + GitHub Actions cache-backend config into
        # the OpenUSD build subprocess (it otherwise runs with a minimal env, so
        # the sccache launcher couldn't reach the running server / GHA backend).
        for _k, _v in os.environ.items():
            if _k.startswith(("SCCACHE_", "ACTIONS_")) or _k == "USE_SCCACHE":
                openusd_env[_k] = _v

        # Try python3 first, fallback to python if not available
        python_cmd = "python3"
        try:
            subprocess.run([python_cmd, "--version"], check=True, capture_output=True)
        except (subprocess.CalledProcessError, FileNotFoundError):
            python_cmd = "python"

        print(f"Building openUSD without python support = {with_python_support}...")
        # Route OpenUSD's own CMake compiles through sccache when CI (or a
        # developer) enables it — same USE_SCCACHE switch the SConstruct honors,
        # backed by the GitHub Actions cache. OpenUSD's built-in cache is ccache
        # (which the runners don't install), so disable it and set an explicit
        # sccache compiler launcher instead. Use sccache's absolute path so it
        # resolves regardless of the reduced env we hand the subprocess.
        cmake_build_args = "-DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_CXX_STANDARD=17"
        use_sccache = os.environ.get("USE_SCCACHE", "") not in ("", "0", "no", "false")
        sccache_path = shutil.which("sccache") if use_sccache else None
        if sccache_path:
            launcher = sccache_path.replace("\\", "/")
            cmake_build_args += (
                f' -DCMAKE_C_COMPILER_LAUNCHER="{launcher}"'
                f' -DCMAKE_CXX_COMPILER_LAUNCHER="{launcher}"'
            )
            print(f"sccache enabled for the openUSD build: {sccache_path}")
        build_usd_cmd = [
            python_cmd,
            f"{open_usd_path}/build_scripts/build_usd.py",
            f"{open_usd_build_path}",
            "--verbose",
            "--build-variant", "release" if build_target == "template_release" else "relwithdebuginfo", #debug,release,relwithdebuginfo
            "--build-monolithic",
            "--no-python" if not with_python_support else "--python",
            "--no-examples",
            "--no-tutorials",
            # Never build the C++ command-line tools (usdcat, sdfdump, ...). We
            # only need usdGenSchema from the withPython build, and it is gated
            # on PXR_ENABLE_PYTHON_SUPPORT + Jinja2, NOT PXR_BUILD_USD_TOOLS, so
            # it is still produced. Building the tools additionally fails to
            # link on Linux: the monolithic libusd_ms.so leaves its Python
            # symbols for dynamic lookup, which ld cannot resolve when linking a
            # standalone executable (macOS defers, so it slipped through there).
            "--no-tools",
            "--no-debug-python",
            "--no-openvdb",
            "--no-usdview",
            "--no-imaging",
            "--no-vulkan",
            "--no-materialx",
            "--onetbb",
            "--no-compiler-cache" if sccache_path else "--compiler-cache",
            "--cmake-build-args", cmake_build_args,
        ]
        # Point OpenUSD at the shared libpython that actually exists (conda's
        # sysconfig otherwise reports a static .a that isn't shipped). Only
        # matters for the Python-enabled build; the non-Python build links no
        # libpython.
        if with_python_support:
            build_usd_cmd += ["--build-python-info", *_build_python_info(platform_name)]
        result = subprocess.run(build_usd_cmd, env=openusd_env)
        
        if result.returncode != 0:
            print(f"Failed to build openUSD")
            Exit(f"Build aborted due to subprocess failure (exit code: {result.returncode})")        

def _get_windows_msvc_env(env):
    vswhere_path = r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if not os.path.exists(vswhere_path):
        raise RuntimeError("vswhere.exe not found")

    # Step 1: Find the installation path of Visual Studio
    cmd = [
        vswhere_path,
        "-latest",
        "-products", "*",
        "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
        "-property", "installationPath"
    ]
    vs_path = subprocess.check_output(cmd, encoding="utf-8").strip()
    if not vs_path:
        raise RuntimeError("No Visual Studio installation with required components found")
    
    """Runs vcvars64.bat and returns its environment as a dict"""
    vcvars_path = os.path.join(vs_path, "VC", "Auxiliary", "Build", "vcvars64.bat")
    
    # Use a cmd trick to output all environment variables after calling vcvars
    cmd = f'"{vcvars_path}" >nul && set'
    
    # Run and capture output
    output = subprocess.check_output(cmd, shell=True, text=True)
    
    # Parse into a dictionary
    for line in output.splitlines():
        if '=' in line:
            key, value = line.split('=', 1)
            env[key.upper()] = value
            
    return env
