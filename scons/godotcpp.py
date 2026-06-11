"""
SCons tool: godotcpp
Builds the godot-cpp library for use as a dependency in the GDExtension build.

godot-cpp is vendored as a git subtree at thirdparty/godot-cpp (upstream
https://github.com/godotengine/godot-cpp, read-only). Update it with:
    git subtree pull --prefix=thirdparty/godot-cpp \
        https://github.com/godotengine/godot-cpp.git <ref> --squash
The vendored version must match the targeted Godot engine version.

Usage in SConstruct:
    env.BuildGodotCPP()
"""
import os

def generate(env):
    env.AddMethod(_build_godot_cpp, 'BuildGodotCPP')

def exists(env):
    return True

def _build_godot_cpp(env):
    godot_cpp_path = "thirdparty/godot-cpp"
    if not os.path.exists(os.path.join(godot_cpp_path, "SConstruct")):
        raise RuntimeError(
            f"{godot_cpp_path} missing — the godot-cpp subtree is gone; "
            "restore it with git checkout or git subtree add."
        )

    print("Building godot-cpp...")
    env["use_exceptions"] = "yes"
    env["use_rtti"] = "yes"
    env["use_threads"] = "yes"

    return env.SConscript(f"{godot_cpp_path}/SConstruct", exports=['env'])