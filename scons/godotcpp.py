"""
SCons tool: godotcpp
Builds the godot-cpp library for use as a dependency in the GDExtension build.

The godot-cpp version need to match the targeted Godot version. For Godot 4.5, we use the 4.5 stable release of godot-cpp.

Usage in SConstruct:
    env.BuildGodotCPP()
"""
import os

from download_utils import download_file, extract_archive

GODOTCPP_VERSION = "godot-4.5-stable"
GODOTCPP_SHA256 = "ac78539c0042554c494ea419549d2de88758d448721aeb0e5d41129aa87e339c"
BASE_URL = "https://github.com/godotengine/godot-cpp/archive/refs/tags"

def generate(env):
    env.AddMethod(_build_godot_cpp, 'BuildGodotCPP')

def exists(env):
    return True

def _build_godot_cpp(env):
    godot_cpp_path = "thirdparty/godot-cpp"
    if not os.path.exists(godot_cpp_path):
        print("Downloading godot-cpp...")
        os.makedirs("./thirdparty", exist_ok=True)

        url = f"{BASE_URL}/{GODOTCPP_VERSION}.tar.gz"
        archive_path = os.path.join("./thirdparty", f"{GODOTCPP_VERSION}.tar.gz")

        download_file(url, archive_path, "godot-cpp", GODOTCPP_SHA256)
        extract_archive(archive_path, "./thirdparty")

        # The archive extracts to godot-cpp-{VERSION}/ -> rename to godot-cpp
        extracted_dir = os.path.join("./thirdparty", f"godot-cpp-{GODOTCPP_VERSION}")
        if os.path.exists(extracted_dir):
            os.rename(extracted_dir, godot_cpp_path)

        os.remove(archive_path)
        print("godot-cpp downloaded and extracted successfully.")
        
    print("Building godot-cpp...")
    env["use_exceptions"] = "yes"
    env["use_rtti"] = "yes"
    env["use_threads"] = "yes"

    return env.SConscript(f"{godot_cpp_path}/SConstruct", exports=['env'])