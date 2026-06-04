# IDTX Runtime Instancer (Unreal)

The Unreal **runtime-import** adapter for the USD interop matrix (CHI-312). Reads
a USD-authored avatar at runtime and instances it as a live
`USkeletalMeshComponent` — no editor import, no cooked asset.

```
USD ──libidtx_core (.sigs dlopen)──▶ idtx_avatar_t
      ──▶ TArray<FMeshSurface> + USkeleton
      ──▶ FRuntimeSkeletalMeshGenerator::GenerateSkeletalMeshComponent(Actor, …)
```

## Use

```cpp
USkeletalMeshComponent* C =
    UIdtxRuntimeInstancer::InstanceAvatarFromUSD(MyActor, TEXT("D:/avatar.usda"));
```
Also Blueprint-callable (`IDTX > Instance Avatar From USD`).

## Skinned *and* regular meshes

`GenerateSkeletalMeshComponent` always yields a **skeletal** component, so regular
(unskinned) meshes are carried by binding **every vertex to a single root bone
(weight 1)** — the mesh then renders rigid/static. The instancer applies this
automatically:
- avatar has a skeleton + per-vertex weights → true skinned mesh;
- no skeleton / no weights → a one-bone `Root` skeleton + rigid binding;
- degenerate all-zero weights → pinned to root so no vertex is left invalid.

For genuinely static content a `UProceduralMeshComponent` / runtime `UStaticMesh`
would be lighter, but routing through the skeletal path keeps a single code path
for the whole avatar (body + clothing, skinned or not).

## Dependencies (vendored as sibling subtrees)
- `RuntimeSkeletalMeshGenerator` — the runtime mesh build.
- `SeamlessAnimatedSkeletons` (optional) — runtime skeleton retarget/animation.
- `libidtx_core` — loaded via the generated `.sigs` dlopen table (delay-load on
  Windows, dlsym stubs on POSIX); **not** statically linked.

## Status
Scaffold grounded in the real plugin APIs (`FMeshSurface`, `FRawBoneInfluence`,
`GenerateSkeletalMeshComponent`) and the `idtx_core` reader getters. **Not built
in-session** (no UE toolchain). Must-verify spots are marked in the source: the
runtime `FReferenceSkeleton`→`USkeleton` assignment and the USD↔UE coordinate
conversion (validate via a `usdchecker` round-trip).
