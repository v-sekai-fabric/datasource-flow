# Copyright 2026 The openusd-fabric authors.
# SPDX-License-Identifier: MIT
#
# Builds hdVSekaiOutlineJFA, a Hydra task plugin shared library + the
# matching plugInfo.json. Locates OpenUSD via pxrConfig.cmake (shipped
# with the conda-forge `openusd` package; pixi installs it under
# $PIXI_PROJECT_ROOT/.pixi/envs/default/Library on Windows).
#
# Usage (from the repo root):
#     pixi run hydra-jfa
#
# Or directly:
#     cmake -S hydra/HdVSekaiOutlineJFA -B hydra/HdVSekaiOutlineJFA/build \
#           -G Ninja -DCMAKE_BUILD_TYPE=Release
#     cmake --build hydra/HdVSekaiOutlineJFA/build

cmake_minimum_required(VERSION 3.28)
project(hdVSekaiOutlineJFA CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(pxr CONFIG REQUIRED)

add_library(hdVSekaiOutlineJFA SHARED
    outlineJFATask.cpp
    outlineJFAPlugin.cpp
)

target_include_directories(hdVSekaiOutlineJFA
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(hdVSekaiOutlineJFA
    PRIVATE
        hd
        hgi
        tf
        gf
        sdf
)

# Stage plugInfo.json next to the shared library so PXR_PLUGINPATH_NAME
# pointed at this build dir resolves the plugin. The JSON's LibraryPath
# is a relative path that works for both the build tree and the install
# tree (`../hdVSekaiOutlineJFA.dll` from the resources/ directory).
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/plugInfo.json
    ${CMAKE_CURRENT_BINARY_DIR}/resources/plugInfo.json
    COPYONLY)

# Install layout matches USD's plugin convention:
#   <prefix>/plugin/usd/hdVSekaiOutlineJFA.dll
#   <prefix>/plugin/usd/hdVSekaiOutlineJFA/resources/plugInfo.json
install(TARGETS hdVSekaiOutlineJFA
        LIBRARY DESTINATION plugin/usd
        RUNTIME DESTINATION plugin/usd)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/resources/plugInfo.json
        DESTINATION plugin/usd/hdVSekaiOutlineJFA/resources)
