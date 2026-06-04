#!/usr/bin/env bash
# OUR Emscripten build of libidtx_core (the USD reader) for the web spoke
# (CHI-312). This is not an external "USD-WASM" dependency — it compiles our own
# core/src sources and builds OpenUSD for wasm as a step we own, then links the
# web glue into public/idtx_core.{js,wasm}.
#
# Prereq: the emsdk toolchain active (`source <emsdk>/emsdk_env.sh`), so emcmake
# / emmake / em++ are on PATH.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
OUT="$HERE/../public"
USD_VER="25.11"
USD_SRC="$REPO/thirdparty/openusd-${USD_VER}-src"
USD_WASM="$REPO/build/wasm/openusd"
mkdir -p "$OUT" "$USD_WASM"

command -v em++ >/dev/null || { echo "emsdk not active (no em++). source emsdk_env.sh first."; exit 1; }

# 1) OpenUSD -> wasm (our build step). Monolithic static, no Python, no imaging —
#    the reader only needs Sdf/Usd/UsdGeom/UsdSkel. OpenUSD's CMake is driven
#    directly here (build_usd.py wraps this for native; for wasm we emcmake it).
if [ ! -f "$USD_WASM/lib/libusd_ms.a" ]; then
  emcmake cmake -S "$USD_SRC" -B "$USD_WASM" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DPXR_BUILD_MONOLITHIC=ON \
    -DPXR_ENABLE_PYTHON_SUPPORT=OFF \
    -DPXR_BUILD_IMAGING=OFF \
    -DPXR_BUILD_USD_IMAGING=OFF \
    -DPXR_BUILD_USDVIEW=OFF \
    -DPXR_BUILD_TESTS=OFF \
    -DPXR_BUILD_EXAMPLES=OFF \
    -DPXR_BUILD_TUTORIALS=OFF \
    -DPXR_ENABLE_GL_SUPPORT=OFF \
    -DPXR_ENABLE_PTEX_SUPPORT=OFF \
    -DPXR_ENABLE_OPENVDB_SUPPORT=OFF \
    -DPXR_PREFER_SAFETY_OVER_SPEED=ON
  emmake cmake --build "$USD_WASM" --target usd_ms -j"$(nproc)"
fi

# 2) Compile our core sources (the reader + IR) + the web glue with em++.
#    Exclude the engine-glue / network / crypto sources that need deps wasm
#    doesn't carry (Godot headers, ixwebsocket/OpenSSL).
EXCLUDE='idtx_godot_scn_glue|idtx_transport|idtx_aes|idtx_chunker'
CORE_SRCS=$(find "$REPO/core/src" -name '*.cpp' | grep -Ev "$EXCLUDE" | sort)

em++ \
  -O3 -std=c++17 \
  -I"$REPO/core/include" -I"$REPO/core/src" \
  -I"$USD_WASM/include" \
  $CORE_SRCS \
  "$HERE/idtx_core_web.cpp" \
  -L"$USD_WASM/lib" -lusd_ms \
  -s MODULARIZE=1 \
  -s EXPORT_ES6=1 \
  -s EXPORT_NAME=createIdtxCore \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s FORCE_FILESYSTEM=1 \
  -s 'EXPORTED_RUNTIME_METHODS=["ccall","cwrap","FS","UTF8ToString"]' \
  -s 'EXPORTED_FUNCTIONS=["_idtxweb_usd_to_json","_idtxweb_free","_idtxweb_version","_malloc","_free"]' \
  -o "$OUT/idtx_core.js"

echo "built our libidtx_core wasm -> $OUT/idtx_core.js + idtx_core.wasm"
