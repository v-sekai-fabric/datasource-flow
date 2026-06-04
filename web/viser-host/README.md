# idtx-flow — Web host (viser)

The **web host** for idtx-flow: a Python [viser](https://github.com/nerfstudio-project/viser)
server that runs `libidtx_core` **server-side natively** (via ctypes, the same
path the Blender host uses) and streams geometry to a three.js client in the
browser over websockets — **no WebAssembly**.

This replaces the experimental `web/idtx-three/` three.js preview. Tracked in
CHI-314 (under CHI-312, the dlopen `.sigs` ABI adoption).

## Status

`serve.py` is a full host adapter: it reads the avatar through the **same core
ABI the Godot and Unity importers use** and renders it in a viser scene, plus a
GUI round-trip.

Geometry parity with the other adapters:

- **Skeleton + skinning** — bones (bind pose) and per-vertex weights drive a
  viser `add_mesh_skinned` (toon material), so the rig is real, not a static
  blob.
- **Blend shapes** — every target is read; the panel exposes live sliders that
  morph the mesh CPU-side (capped at 64 for very large rigs — Miroir has 538).
- **Material base color** and **normals** feed the shaded mesh.

Round-trip buttons:

- **Import USD …** — upload a `.usd` / `.usda` / `.usdc` / `.usdz`; imported with
  `idtx_core_import_avatar_from_usd` (the unified reader — same Y-up change of
  basis as the Godot host) and swapped into the scene.
- **Export OpenUSD** — re-author the loaded avatar with
  `idtx_core_export_avatar_to_usd` and download the `.usda`.

On launch it seeds the scene with `build/miroir_from_unity.usda` if present.

Not yet wired (render-fidelity extras the viewer doesn't surface yet): base-color
/ normal **textures**, metallic-roughness, MToon shade/rim/outline, blend-shape
**normal** deltas, and skeletal **animation** playback. Scene editing and zones
are also future work.

## Run

```bash
pixi run serve     # then open the printed http://localhost:8080 URL
```

Everything runs through pixi — the `serve` task and the env (numpy + viser) are
defined in `pixi.toml`; the core itself is loaded natively via ctypes.

Requires the built core at `build/idtx_core/libidtx_core.<platform>` (currently
Windows only) and its OpenUSD deps under `thirdparty/openusd-25.11/`. Override
the library location with the `IDTX_CORE_DLL` env var if needed.
