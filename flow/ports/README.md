# flow/ports — the port ring of the avatar-pipeline component

Ports are the narrow contracts between `flow/core` and every host
adapter, labeled per the hexagonal decision doc by **direction**
(driving = host calls core, driven = core calls host) and **data flow**
(`source` = core reads, `sink` = core writes).

| Port | Direction | Flow | Mechanism |
|---|---|---|---|
| `include/idtx_core/idtx_core.h` | driving | source+sink | flat C ABI: opaque handles (avatar/skeleton/mesh/material), import/export entry points |
| `include/idtx_core/idtx_scene.h` | driving | sink | engine-neutral converted-stage tree every host walks to build native entities |
| `include/idtx_core/idtx_chunker.h`, `idtx_transport.h`, `idtx_aes.h` | driving | source+sink | caibx/casync content transport surface |
| `include/idtx_core/idtx_asset_io.h` (`idtx_asset_io_t`, registered via `idtx_core_set_asset_io`) | driven | source | C struct-of-function-pointers vtable the host registers so core can resolve `res://`/`user://` URIs |
| `idtx_progress_fn` (registered via `idtx_core_set_progress_cb`, `idtx_core.h`) | driven | sink | progress reporting back into the host |

`idtx_core.sigs` is the single source of truth for the runtime-bound
surface: `scons/generate_stubs.py` drift-gates it against every public
header here, then emits the dlopen/delay-load dispatch table into
`generated/` (a build artifact, git-ignored). Hosts bind the library at
runtime through that table — adding a host adds zero link dependencies.

Rules of the ring:

- Headers here are C-ABI only: primitives, opaque handles, function
  pointers. No `pxr::`, no STL, no engine types (enforced culture-side;
  the Unity ABI validator and the `.sigs` gate enforce the symbol list).
- Private implementation headers live in
  `flow/core/include/idtx_core/internal/` — intentionally outside the
  ring and excluded from the drift gate.
