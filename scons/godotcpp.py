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

    # godot-cpp's bundled gdextension/extension_api.json comes from a
    # single-precision engine and its binding generator refuses a
    # precision=double build against it. For double builds, generate
    # bindings from the API dump of the actual target engine (the
    # v-sekai-multiplayer-fabric godot fork, 4.7-beta double):
    #   godot.windows.editor.double.x86_64 --headless --dump-extension-api
    if env.get("precision", "single") == "double":
        env["custom_api_file"] = os.path.abspath(
            "flow/adapters/godot/api/extension_api.double.json"
        )

    return env.SConscript(f"{godot_cpp_path}/SConstruct", exports=['env'])