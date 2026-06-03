# IDTX Core for Unity

Unity P/Invoke wrapper around `libidtx_core` — the engine-agnostic C ABI
for the idtx-flow avatar pipeline. Targets **Unity 2022.3 LTS** (the
new `com.unity.importer.usd` packages require Unity 2023.1+; this
package is what to use when you're locked to 2022.3).

## Install (Unity Package Manager)

1. Copy `unity/IdtxCore/` from this repo into your Unity project as
   `Packages/com.vsekai.idtxcore/`, or add it via Package Manager's
   "Add package from disk" pointing at the `unity/IdtxCore/package.json`.
2. Copy `addons/IDTXFlow/bin/windows/libidtx_core.windows.x86_64.dll`
   (and the USD runtime DLLs it depends on — see below) into
   `Assets/Plugins/x86_64/`.
3. In Unity's PluginImporter inspector for `libidtx_core...dll`,
   strip the `lib` prefix and platform/arch suffix so the runtime
   filename Unity sees is `idtx_core.dll`. This matches the
   `DllName = "idtx_core"` constant in `IdtxCoreNative.cs`.

Runtime dependencies (must be alongside `idtx_core.dll`):

| File | Source |
|---|---|
| `usd_ms.dll`     | thirdparty/openusd-25.11/lib/usd_ms.dll      |
| `tbb12.dll`      | thirdparty/openusd-25.11/bin/tbb12.dll       |
| `libidtx_usd.dll`| usd/libs/windows/libidtx_usd.dll             |

## Use

```csharp
using IdtxCore;

// Read a USD file produced by idtx-flow (or any USD with the
// VSekai* applied schemas).
using (var avatar = IdtxAvatar.ImportFromUsd("Assets/Avatars/foo.usda"))
{
    Debug.Log($"Loaded {avatar.Name} with {avatar.MeshCount} meshes");
    // ... convert handles into Unity GameObjects via your own
    //     bridge code (a UnityEngine-aware bridge ships in a
    //     follow-up; see UnityAvatarBridge stub).
}

// Round trip back out:
using (var avatar = new IdtxAvatar())
{
    avatar.Name = "MyAvatar";
    // populate handles via the IdtxSkeleton / IdtxMesh / IdtxMaterial
    // wrappers (also in this package).
    int rc = avatar.ExportToUsd("Assets/Avatars/out.usda");
    if (rc != 0) Debug.LogError($"USD export failed: rc={rc}");
}
```

## Editor integration

Drop a `.usda` / `.usdc` / `.usd` into `Assets/` and `IdtxUsdImporter`
(under `Editor/`) automatically materializes it via `libidtx_core`'s
USD reader, then through `UnityAvatarBridge.AvatarToGameObject`. The
main asset is the reconstructed `GameObject`; sub-assets are the
`Mesh`es and `Material`s it references so they survive re-import.

`IdtxCoreLoader` runs at Editor startup (and every domain reload)
to set `PXR_PLUGINPATH_NAME` to the V-Sekai schema directory — point
at `Packages/com.vsekai.idtxcore/Schema/` when consumed as a UPM
package, or whatever you set the env var to manually.

To ship the V-Sekai schema alongside the package, copy
`openusd-fabric/schema/plugInfo.json` + `v_sekai_schema.usda` into
`unity/IdtxCore/Schema/`. Without it, the importer still produces
geometry / skeleton / materials but `prim.HasAPI("VSekai*API")`
returns false and the V-Sekai-specific attributes won't apply.

## Status

| API | State |
|---|---|
| USD export / import | ✅ Working |
| VRM export / import | ✅ Working (export full; import via cgltf) |
| Editor ScriptedImporter | ✅ `IdtxUsdImporter` auto-materializes .usda/.usdc/.usd |
| MToon material round-trip | ✅ via VSekaiMToonAPI / VRMC_materials_mtoon mapping |
| Humanoid bone mapping (VRChat-ready) | ✅ in VRM export; UniVRM picks it up |
| Spring bones (chain + collider) | ✅ end-to-end USD ↔ VRM |
| Physics colliders (incl. tapered) | ✅ USD round-trip via UsdPhysicsCollisionAPI + V-Sekai extension |
| VRChat PhysBone auto-conversion | 🚧 Pending — needs VRC SDK references |

## Compatibility

- **.NET API**: netstandard2.1 (Unity's Mono backend on 2022.3 LTS).
- **IL2CPP**: All declarations use marshallable types only; safe under
  IL2CPP after a `link.xml` directive to preserve `IdtxCore.Native.*`.
- **Architecture**: x86_64 only (Windows). macOS / Linux / arm64
  builds need their respective `libidtx_core.{platform}.{arch}.dll/dylib/so`.
