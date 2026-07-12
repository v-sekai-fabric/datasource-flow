# Requirements and Setup to Build from Source

The IDTX Flow plugin for Godot uses `scons` (a python build tool) to download and install all required dependencies to run the build that creates the binaries.
However, some tools and software are still required to be installed upfront.

The following software and components are required to be installed first. Usually prior or directly after cloning the repository.

## All Operating Systems

- **Python3**, once installed use `pip` to install `scons`, `jinja2` and `pyside6`
- **Godot4.5**, to be able to test the plugin within a Godot project. The Godot version need to match the version of the [C++ bindings](https://github.com/godotengine/godot-cpp) used as a dependency.

## Windows

- **C++ Buildtools** - MS Visual Studio Build tools
- **CMake** - usually part of the MS Visual Studio Build tools

## MacOS

 - **C++ Buildtools** - The full Xcode IDE is required because OpenUSD's build script uses xcodebuild for codesigning,
 - **CMake** - use `brew install cmake` for example.

Once all required software and tools are installed and configured the plugin can be build with the following command, executed at the root folder of this repository.

```bash
checked_out_repo_dir $>scons
```

> Please be patient, as the first initial build will download and compile openUSD from source. Depending on the used hardware this may take up to 40 minutes or more.

The binary artifacts and all additional required files for the plugin can be found in the folder `addons/IDTXFlow`. Just copy this folder into the `addons` folder of the Godot project. To verify and enable the plugin, open the project in Godot, go to **Project → Project Settings → Plugins**, find the plugin and click the respective checkbox.
