# idtx-flow — Web host (viser)

The **web host** for idtx-flow: a Python [viser](https://github.com/nerfstudio-project/viser)
server that runs `libidtx_core` **server-side natively** (via ctypes, the same
path the Blender host uses) and streams geometry to a three.js client in the
browser over websockets — **no WebAssembly**.

This replaces the experimental `web/idtx-three/` three.js preview. Tracked in
CHI-314 (under CHI-312, the dlopen `.sigs` ABI adoption).

## Status

Proof-of-concept: `serve.py` renders an avatar in a viser scene and exposes a
round-trip through the core in the browser GUI panel:

- **Import USD …** — upload a `.usd` / `.usda` / `.usdc` / `.usdz`; it is imported
  with `idtx_core_import_avatar_from_usd` (the unified reader — same Y-up change
  of basis as the Godot host) and replaces the scene.
- **Export OpenUSD** — re-author the loaded avatar with
  `idtx_core_export_avatar_to_usd` and download the resulting `.usda`.

On launch it seeds the scene with `build/miroir_from_unity.usda` if present. This
proves the no-WASM thesis end-to-end (ctypes load → reader/writer ABI → browser
round-trip). Scene editing, zones, and the VRM path are not built yet.

## Run

```bash
pixi run serve     # then open the printed http://localhost:8080 URL
```

Everything runs through pixi — the `serve` task and the env (numpy + viser) are
defined in `pixi.toml`; the core itself is loaded natively via ctypes.

Requires the built core at `build/idtx_core/libidtx_core.<platform>` (currently
Windows only) and its OpenUSD deps under `thirdparty/openusd-25.11/`. Override
the library location with the `IDTX_CORE_DLL` env var if needed.
