#!/usr/bin/env bash
# OUR Emscripten build of libidtx_core (the USD reader) for the web spoke
# (CHI-312). Not an external "USD-WASM" dependency — it builds OpenUSD's TBB dep
# and OpenUSD itself for wasm, then compiles our own core/src + the glue.
#
# Validated against OpenUSD 25.11 (which has wasm awareness: it auto-sets
# PXR_BUILD_EXEC=OFF when targeting wasm) + oneTBB 2021.12 + emsdk 5.0.7.
#
# Prereq: emsdk active (`source <emsdk>/emsdk_env.sh`). On Windows from bash the
# tools are emcc.bat / emcmake.bat / emmake.bat (bash does not resolve .bat).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
OUT="$HERE/../public"
USD_VER="25.11"
USD_SRC="$REPO/thirdparty/openusd-${USD_VER}-src"
TBB_SRC="$REPO/thirdparty/openusd-${USD_VER}/src/oneTBB-2021.12.0"
WASM="$REPO/build/wasm"
TBB_ROOT="$WASM/tbb-root"
USD_BUILD="$WASM/openusd"
mkdir -p "$OUT" "$WASM"

# .bat suffix on Windows (Git Bash); bare names elsewhere.
EMCMAKE=emcmake; EMMAKE=emmake; EMXX=em++
command -v emcmake >/dev/null || { EMCMAKE=emcmake.bat; EMMAKE=emmake.bat; EMXX=em++.bat; }
command -v "$EMCMAKE" >/dev/null || { echo "emsdk not active. source emsdk_env.sh first."; exit 1; }

# 1) oneTBB -> wasm (OpenUSD's hard dependency; no prebuilt wasm TBB exists).
if [ ! -f "$TBB_ROOT/lib/libtbb.a" ]; then
  "$EMCMAKE" cmake -S "$TBB_SRC" -B "$WASM/tbb" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
    -DTBB_TEST=OFF -DTBB_EXAMPLES=OFF -DTBB_STRICT=OFF
  "$EMMAKE" cmake --build "$WASM/tbb" --target tbb -j"$(nproc)"
  mkdir -p "$TBB_ROOT/lib"
  cp "$(find "$WASM/tbb" -name libtbb.a | head -1)" "$TBB_ROOT/lib/"
  cp -r "$TBB_SRC/include" "$TBB_ROOT/include"
fi

# 2) OpenUSD -> wasm (monolithic static, no Python/imaging — reader needs only
#    Sdf/Usd/UsdGeom/UsdSkel). The Emscripten toolchain restricts find_library
#    to its sysroot, so open CMAKE_FIND_ROOT_PATH for our TBB.
if [ ! -f "$USD_BUILD/pxr/libusd_m.a" ] && ! find "$USD_BUILD" -name libusd_m.a | grep -q .; then
  "$EMCMAKE" cmake -S "$USD_SRC" -B "$USD_BUILD" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DPXR_BUILD_MONOLITHIC=ON \
    -DPXR_ENABLE_PYTHON_SUPPORT=OFF -DPXR_BUILD_IMAGING=OFF -DPXR_BUILD_USD_IMAGING=OFF \
    -DPXR_BUILD_USDVIEW=OFF -DPXR_BUILD_TESTS=OFF -DPXR_BUILD_EXAMPLES=OFF \
    -DPXR_BUILD_TUTORIALS=OFF -DPXR_ENABLE_GL_SUPPORT=OFF \
    -DTBB_ROOT_DIR="$TBB_ROOT" \
    -DCMAKE_FIND_ROOT_PATH="$TBB_ROOT" \
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=BOTH \
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=BOTH
  "$EMMAKE" cmake --build "$USD_BUILD" --target usd_m -j"$(nproc)"
fi
USD_LIB_DIR="$(dirname "$(find "$USD_BUILD" -name libusd_m.a | head -1)")"
USD_INC="$USD_BUILD/include"

# 3) Compile our core (reader + IR) + the web glue, link the monolithic USD +
#    TBB. Exclude engine-glue / transport / crypto sources (need Godot/OpenSSL).
EXCLUDE='idtx_godot_scn_glue|idtx_transport|idtx_aes|idtx_chunker'
CORE_SRCS=$(find "$REPO/core/src" -name '*.cpp' | grep -Ev "$EXCLUDE" | sort)

"$EMXX" \
  -O3 -std=c++17 \
  -I"$REPO/core/include" -I"$REPO/core/src" \
  -I"$USD_INC" -I"$TBB_ROOT/include" \
  $CORE_SRCS "$HERE/idtx_core_web.cpp" \
  -L"$USD_LIB_DIR" -lusd_m \
  -L"$TBB_ROOT/lib" -ltbb \
  -s MODULARIZE=1 -s EXPORT_ES6=1 -s EXPORT_NAME=createIdtxCore \
  -s ALLOW_MEMORY_GROWTH=1 -s FORCE_FILESYSTEM=1 \
  -s 'EXPORTED_RUNTIME_METHODS=["ccall","cwrap","FS","UTF8ToString"]' \
  -s 'EXPORTED_FUNCTIONS=["_idtxweb_usd_to_json","_idtxweb_free","_idtxweb_version","_malloc","_free"]' \
  -o "$OUT/idtx_core.js"

echo "built our libidtx_core wasm -> $OUT/idtx_core.js + idtx_core.wasm"
