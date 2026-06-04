# idtx-flow — Web spoke (three.js + R3F + three-vrm)

The **web** node of the USD interop matrix (CHI-312), replacing the Unreal spoke.
It renders the avatar in the browser with **React-Three-Fiber** and
**[@pixiv/three-vrm](https://github.com/pixiv/three-vrm)**, consuming USD through
the **same `libidtx_core` reader** as the Godot/Blender/Unity spokes — compiled to
**WebAssembly** with Emscripten.

```
USD ──libidtx_core (WASM, Emscripten)──▶ idtx_avatar ──▶ geometry JSON ──▶ THREE.BufferGeometry
VRM ──@pixiv/three-vrm (GLTFLoader + VRMLoaderPlugin)──▶ humanoid scene (MToon + spring bones)
```

## Two render paths

| Component | Source | Notes |
|---|---|---|
| `UsdAvatar.tsx` | USD via the WASM core (`idtxCore.ts`), or pre-baked `public/miroir.json` | through-USD path, mirrors the Godot ArrayMesh spoke |
| `VrmAvatar.tsx` | a `.vrm` file via three-vrm | humanoid path; carries the same MToon/spring-bone schemas the USD pipeline does |

`VITE_USD_URL` points `UsdAvatar` at a `.usda` to read live via WASM; `VITE_VRM_URL`
switches to the three-vrm path. With neither set, it renders the pre-baked Miroir
geometry (`public/miroir.json`, produced by `libidtx_core` reading
`miroir_from_unity.usda` offline — the same Unity→USD artifact the other spokes use).

## Run

```bash
npm install
npm run dev          # renders the pre-baked Miroir USD geometry
```

## The WASM core (the `.sigs` reader, in the browser)

`wasm/idtx_core_web.cpp` is the Emscripten glue: it exposes
`idtxweb_usd_to_json(path)` — the browser writes a `.usda` into MEMFS, the core
reads it (`idtx_core_import_avatar_from_usd` → `idtx_mesh_get_positions/indices`)
and returns geometry JSON.

```bash
source <emsdk>/emsdk_env.sh   # emcc/emcmake/em++ on PATH
npm run build:wasm            # -> public/idtx_core.{js,wasm}
```

This is **our build**, not an external "USD-WASM" dependency: `build-wasm.sh`
compiles `libidtx_core`'s own `core/src` sources (the reader + IR) with `em++` and
builds OpenUSD for wasm via `emcmake` as a step we own — monolithic static, no
Python, no imaging (the reader only needs Sdf/Usd/UsdGeom/UsdSkel). The
engine-glue / transport / crypto sources are excluded (they need Godot / OpenSSL).

On the web there is no `.sigs` delay-load — the whole core (libidtx_core + its
OpenUSD) is statically linked into the one `.wasm` binary. Same sources every
host runs; different link strategy. Until you run the wasm build the app uses the
pre-baked JSON path, which exercises the full R3F + geometry pipeline meanwhile.
