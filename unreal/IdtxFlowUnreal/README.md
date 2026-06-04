# IDTX Flow — Unreal integration (no plugin, "merely the `.sigs`")

Export Unreal meshes through `libidtx_core`'s layer-aware USD exporter
(`idtx_core_export_avatar_to_usd_ex`) using Unreal's **built-in Python +
ctypes** — there is **no C++ `.uplugin` and nothing to compile**. The
generated dlopen table (from `core/idtx_core.sigs`) plus the thin ctypes
binding here are the whole integration, identical in spirit to the Blender
hook (`openusd-fabric/blender/`).

This is the Unreal half of the two-implementations-per-category interop rule
(editors: Blender + raw CLI; runtime: Unity + Godot; Unreal joins as a host).

## Files

| File | Role |
|------|------|
| `idtx_core_ctypes.py` | Engine-agnostic ctypes binding, faithful to `core/idtx_core.sigs`. Locates + dlopens `libidtx_core.<plat>.<arch>`, declares the avatar/skeleton/mesh/material/`_ex` ABI, exposes `export_avatar_ex(...)`. |
| `idtx_unreal_export.py` | Runs inside UE Python (`import unreal`): builds an avatar from a selected Static/Skeletal mesh and drives the `_ex` export. |
| `tests/test_idtx_unreal_export.py` | Headless test of the binding + `_ex` path (synthetic avatar; no `unreal` module needed). |

## Run it

In the editor — **Output Log → Cmd dropdown = Python**:

```python
exec(open(r"<repo>/unreal/IdtxFlowUnreal/idtx_unreal_export.py").read())
# select a Skeletal/Static mesh in the Content Browser, then:
export_selected(r"D:/out.usda", mode="overlay", source=r"D:/base.usda")
```

Headless:

```bat
UnrealEditor-Cmd.exe <Project>.uproject -run=pythonscript ^
    -script="<repo>/unreal/IdtxFlowUnreal/idtx_unreal_export.py"
```
(set `IDTX_OUT`, `IDTX_MODE`, `IDTX_SOURCE` env vars for the headless form).

The binding finds `libidtx_core` under `addons/IDTXFlow/bin/<platform>/` or
`build/idtx_core/`; override with the `IDTX_CORE_DLL` env var.

## Engine support

- **UE5**: geometry is extracted via **GeometryScripting** (copy asset →
  `DynamicMesh` → query vertices/triangles) — stock UE5 Python. Skeleton +
  materials too.
- **UE4.27**: no GeometryScripting, so the geometry path logs a warning and is
  skipped; skeleton + materials still export. For full UE4.27 geometry,
  pre-export the asset (FBX/OBJ) or build on UE5. (The earlier C++ builder is
  preserved in git history if a compiled path is ever needed.)

## Coordinate conversion

UE is left-handed, Z-up, centimetres; USD/idtx is right-handed, Y-up, metres.
`_ue_to_usd_root()` bakes the cm→m scale + Z-up→Y-up swap into the avatar root
transform — the single place to tune (validate via `usdchecker` round-trip).

## Verify

```bash
python -m pytest unreal/IdtxFlowUnreal/tests/ -v   # binding + _ex, headless
```
