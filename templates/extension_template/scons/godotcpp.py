"""
SCons tool: godotcpp
Builds the godot-cpp library for use as a dependency in the GDExtension build.

The godot-cpp version need to match the targeted Godot version. For Godot 4.5, we use the 4.5 branch of godot-cpp.

Usage in SConstruct:
    env.BuildGodotCPP()
"""
import os
import subprocess

def generate(env):
    env.AddMethod(_build_godot_cpp, 'BuildGodotCPP')

def exists(env):
    return True

def _build_godot_cpp(env):
    godot_cpp_path = "thirdparty/godot-cpp"
    if not os.path.exists(godot_cpp_path):
        print("Cloning godot-cpp...")
        subprocess.run([
            "git", "clone", "-b", "4.5", "--recursive", 
            "https://github.com/godotengine/godot-cpp.git", 
            godot_cpp_path
        ])

    print("Building godot-cpp...")
    env["use_exceptions"] = "yes"
    env["use_rtti"] = "yes"
    env["use_threads"] = "yes"

    return env.SConscript(f"{godot_cpp_path}/SConstruct", exports=['env'])