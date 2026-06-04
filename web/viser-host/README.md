# idtx-flow — Web host (viser)

The **web host** for idtx-flow: a Python [viser](https://github.com/nerfstudio-project/viser)
server that runs `libidtx_core` **server-side natively** (via ctypes, the same
path the Blender host uses) and streams geometry to a three.js client in the
browser over websockets — **no WebAssembly**.

This replaces the experimental `web/idtx-three/` three.js preview. Tracked in
CHI-314 (under CHI-312, the dlopen `.sigs` ABI adoption).

## Status

Proof-of-concept only: `serve.py` imports `build/miroir_from_unity.usda` through
the core and renders its meshes in a viser scene. It proves the no-WASM thesis
end-to-end (ctypes load → reader ABI → browser render). Scene editing, zones, and
the VRM path are not built yet.

## Run

```bash
pixi run serve     # then open the printed http://localhost:8080 URL
```

Requires the built core at `build/idtx_core/libidtx_core.<platform>` (currently
Windows only) and its OpenUSD deps under `thirdparty/openusd-25.11/`. Override
the library location with the `IDTX_CORE_DLL` env var if needed.
