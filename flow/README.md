# flow — the IDTX Flow avatar-pipeline cluster

This folder is a hexagonal component cluster following the V-Sekai
core/ports/adapters convention:
<https://v-sekai-multiplayer-fabric.github.io/manuals/decisions/20260610-hexagonal-core-ports-adapters.html>

One domain core, narrow C-ABI ports, and one adapter per host runtime.
Hosts never see OpenUSD (`pxr::`), C++ STL types, or each other — the
flat C ABI in `ports/` is the only boundary they bind.

```
flow/
├── core/        domain implementation (compiles to libidtx_core + libidtx_usd)
│   ├── src/         USD<->VRM import/export, springbones, chunker/transport/AES,
│   │                idtx_scene FlatTree converter, in-core res:/user: ArResolver
│   ├── include/     private headers: idtx_core/internal/*, and the
│   │                OpenUSD-dependent converter framework <idtxflow/...>
│   ├── share/       committed slangc-lowered Lean artifacts (godot_scn writers)
│   ├── usd/         OpenUSD codeless+codeful schema plugin (libidtx_usd, plugInfo)
│   ├── libs/        vendored flat-C deps of the core (cgltf, jsmn)
│   └── spec/        fixture tests driving the built C ABI via ctypes — no engine,
│                    no hardware, exactly the "core/spec" suite of the decision doc
├── ports/       the component's port ring — everything a host may bind
│   ├── include/idtx_core/   public C ABI headers (driving port, see ports/README.md)
│   ├── idtx_core.sigs       single source of truth for the dlopen/delay-load surface
│   ├── stubgen_compat.h     prelude for the generated POSIX stub table
│   └── generated/           build artifact: dlsym thunks / .def (git-ignored)
└── adapters/    one folder per host runtime; depend on ports/, never on core/
    ├── godot/       GDExtension (libidtxflow): src/, include/ (idtxflow_godot,
    │                idtxflow_ext), ext/ (bootstrap lib for third-party extensions),
    │                libs/ (lemon — adapter-only matching dependency)
    ├── unity/       IdtxCore UPM package — C# P/Invoke bindings + editor importer
    └── cli/         idtxcli — standalone smoke/round-trip driver
```

## Dependency directions

- `core/` may include `ports/include` (it implements the ABI) and its own
  private headers. It statically encapsulates OpenUSD, ixwebsocket, zstd,
  OpenSSL — none of these leak past the port ring.
- `adapters/*` include only `ports/include` (plus, for the Godot adapter,
  the converter-framework headers in `core/include/idtxflow` it templates
  against at compile time). At runtime every adapter loads
  `libidtx_core` through the generated dispatch table — no link-time
  binding to core or its OpenUSD (CHI-312). Exception: `cli/` link-binds
  for simplicity.
- Nothing in `flow/` may include from another adapter.

## Sibling clusters

- `openusd-fabric/` (repo top level) is the build-time spec/codegen
  cluster: Lean 4 specs emit the maps, shaders, and `.scn` writers that
  are committed into `flow/core/share` and validated by drift tests. It
  is an authority, not a runtime dependency — no Lean runtime is linked
  anywhere in `flow/`.
- The repo root is a Godot project (`project.godot` + `addons/`); the
  built Godot adapter installs into `addons/IDTXFlow/bin`.

Build wiring lives in `scons/` (idtxcore.py, gdextension.py, idtxcli.py,
idtxflow_ext.py, idtxflow_sdk.py, openusdextension.py, generate_stubs.py,
godot_scn_writers.py).
