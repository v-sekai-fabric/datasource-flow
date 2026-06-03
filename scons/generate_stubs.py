"""
SCons tool: generate_stubs

Turns core/idtx_core.sigs into a dlopen-backed dispatch table for the
libidtx_core C ABI, using the vendored Chromium generator at
thirdparty/generate_stubs/generate_stubs.py.

Why: every host that consumes libidtx_core (Godot GDExtension, Unity
P/Invoke, Unreal plugin, Blender hook, and the standalone idtxcli) can
load the shared library at RUNTIME through this table instead of linking
it. No -lidtx_core at link time means none of those hosts drag in the
statically-linked OpenUSD that lives inside core — the C ABI is the only
boundary they see. Adding a host is then "fill idtx_avatar_t, call
idtx_core_export_avatar_to_usd_ex", with zero new link dependencies.

Two outputs depending on platform (mirrors the native_media backend):
  POSIX (linux/macos): `posix_stubs` → idtx_core_stubs.{h,cc}. The .cc
        dlopen()s the lib and dlsym()s each symbol into a function-pointer
        table; weak forwarders make the normal idtx_core_* call route
        through it.
  Windows: `windows_lib` → an import .lib + .def for the delay-load
        mechanism (LoadLibrary on first call).

This tool ALSO gates ABI drift: it parses the export list out of
core/include/idtx_core/idtx_core.h and the declaration list out of the
.sigs and fails the build if they disagree. The .sigs is the single
source of truth for the dlopen surface; this keeps it honest against the
header without anyone remembering to.

Usage in SConstruct (any time after the env is configured; the generated
table is a build artifact under core/generated/, git-ignored):
    env.GenerateCoreStubs()
"""
import os
import re
import subprocess
import sys


def generate(env):
    env.AddMethod(_generate_core_stubs, 'GenerateCoreStubs')


def exists(env):
    return True


# --- ABI drift gate ----------------------------------------------------

def _header_exports(header_path):
    """Function names carrying IDTX_CORE_API in the public header."""
    text = open(header_path).read()
    names = set()
    for m in re.finditer(r'IDTX_CORE_API\b(.*?)\(', text, re.S):
        idents = re.findall(r'[A-Za-z_]\w*', m.group(1))
        if idents:
            names.add(idents[-1])
    # The macro-definition block itself matches (it mentions
    # __declspec / __attribute__); drop non-idtx tokens.
    return {n for n in names if n.startswith('idtx_')}


def _sigs_exports(sigs_path):
    """Function names declared in the .sigs manifest."""
    names = set()
    for line in open(sigs_path):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        m = re.match(r'.*?([A-Za-z_]\w*)\s*\(', line)
        if m:
            names.add(m.group(1))
    return names


def _check_drift(header_path, sigs_path):
    hdr = _header_exports(header_path)
    sig = _sigs_exports(sigs_path)
    missing = sorted(hdr - sig)   # exported but not in .sigs
    extra = sorted(sig - hdr)     # in .sigs but no longer exported
    if missing or extra:
        msg = ["idtx_core.sigs is out of sync with idtx_core.h:"]
        if missing:
            msg.append("  exported in header but MISSING from .sigs: " + ", ".join(missing))
        if extra:
            msg.append("  in .sigs but NOT exported by header: " + ", ".join(extra))
        msg.append("  → update core/idtx_core.sigs to match the ABI.")
        raise RuntimeError("\n".join(msg))


# --- stub emission -----------------------------------------------------

def _generate_core_stubs(env):
    print("Generating libidtx_core dlopen stubs from core/idtx_core.sigs...")

    sigs = "core/idtx_core.sigs"
    header = "core/include/idtx_core/idtx_core.h"
    gen_dir = "core/generated"
    generator = "thirdparty/generate_stubs/generate_stubs.py"

    if not os.path.isfile(sigs):
        print(f"  [stubs] {sigs} absent; skipping (hosts link core directly)")
        env['idtx_core_stubs_available'] = False
        return None

    _check_drift(header, sigs)
    print("  [stubs] .sigs <-> header ABI in sync")

    if not os.path.isdir(gen_dir):
        os.makedirs(gen_dir)

    platform_name = env["platform_name"]

    if platform_name == "windows":
        # Delay-load: emit a .def listing the exports. The host links its
        # consumer with /DELAYLOAD:libidtx_core.dll and the loader pulls
        # the DLL on first call. windows_def is toolchain-free (plain text
        # .def, no lib.exe invocation) — the import .lib is produced by
        # the host's own linker from this .def.
        module = env.get('idtx_core_lib_name', 'libidtx_core')
        cmd = [
            sys.executable, generator,
            "-o", gen_dir, "-i", gen_dir,
            "-t", "windows_def",
            "-m", module,
            sigs,
        ]
        # generate_stubs strips the last extension off the module name
        # for the .def filename (os.path.splitext on the basename), so
        # "libidtx_core.windows.x86_64" -> "libidtx_core.windows.def".
        outputs = [os.path.join(gen_dir, os.path.splitext(module)[0] + ".def")]
    else:
        # POSIX dlopen stubs.
        cmd = [
            sys.executable, generator,
            "-o", gen_dir, "-i", gen_dir,
            "-t", "posix_stubs",
            "-s", "idtx_core_stubs",
            "-p", "core",
            "-l", "(::std::cerr)",
            "-n", "iostream",
            "--macro-include", "core/stubgen_compat.h",
            sigs,
        ]
        outputs = [
            os.path.join(gen_dir, "idtx_core_stubs.h"),
            os.path.join(gen_dir, "idtx_core_stubs.cc"),
        ]

    # Run eagerly (not as a deferred Command node) so the drift gate and
    # any generator error surface during SConstruct evaluation, same as
    # GenerateGodotScnWriters. The artifacts are small and regenerating
    # them every configure is cheap.
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(
            "generate_stubs.py failed:\n" + result.stdout + result.stderr)

    env['idtx_core_stubs_available'] = True
    env['idtx_core_stubs_dir'] = gen_dir
    env['idtx_core_stubs_sources'] = [o for o in outputs if o.endswith((".cc", ".def"))]
    print(f"  [stubs] OK — emitted {', '.join(os.path.basename(o) for o in outputs)} in {gen_dir}/")
    return outputs
