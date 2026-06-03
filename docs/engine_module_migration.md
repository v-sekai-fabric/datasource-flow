# Engine-module migration: `FabricMMOGAsset` → `libidtx_core` wrapper

**Repo affected:** [multiplayer-fabric-godot](https://github.com/V-Sekai/multiplayer-fabric-godot) — NOT this repo. This document is the migration spec; the bytes land in the fork.

**Local task:** #13 (ART-47).

## Goal

Shrink `modules/multiplayer_fabric_asset/fabric_mmog_asset.{cpp,h}`
from ~1956 lines (Godot types + algorithm intermixed) to a ~400-line
**shim** whose method bodies delegate to `libidtx_core`'s C ABI.
Preserves the existing `FabricMMOGAsset` RefCounted API + GDScript
bindings — **no breaking change** for in-engine consumers.

After the migration, the byte-level algorithm lives **once**, in this
repo's `core/src/`, and three deployment targets compile from the same
`.cpp` files: the engine module (this task), the GDExtension
(`IDTXFlowChunker`), and the standalone CLI (`idtxcli`).

## Step 1 — Pull `libidtx_core` into the Godot fork via `git subtree`

In the `multiplayer-fabric-godot` repo:

```bash
git subtree add --prefix=thirdparty/idtx_core \
    git@github.com:V-Sekai-fire/idtx-flow-v-sekai.git main --squash
```

Add `thirdparty/idtx_core/update.sh`:

```bash
#!/usr/bin/env bash
# Refresh thirdparty/idtx_core from upstream.
set -euo pipefail
git subtree pull --prefix=thirdparty/idtx_core \
    git@github.com:V-Sekai-fire/idtx-flow-v-sekai.git main --squash
```

Matches the convention of `thirdparty/zstd/update.sh`,
`thirdparty/desync/update.sh`, etc.

## Step 2 — Add `libidtx_core` sources to the engine module's `SCsub`

`modules/multiplayer_fabric_asset/SCsub`:

```python
Import("env")

env_mfa = env.Clone()
env_mfa.add_source_files(env.modules_sources, "*.cpp")

# Vendored libidtx_core (engine-module = static archive build).
# IDTX_CORE_STATIC suppresses dllexport/dllimport so MSVC doesn't
# generate LNK2019 against import thunks.
env_mfa.Append(CPPDEFINES=["IDTX_CORE_STATIC"])
env_mfa.Append(CPPPATH=["#thirdparty/idtx_core/core/include"])
env_mfa.add_source_files(
    env.modules_sources,
    Glob("#thirdparty/idtx_core/core/src/idtx_chunker.cpp") +
    Glob("#thirdparty/idtx_core/core/src/idtx_transport.cpp") +
    Glob("#thirdparty/idtx_core/core/src/idtx_aes.cpp"),
)
```

Note the `IDTX_CORE_STATIC` define — both the source files **and** any
TU that includes `idtx_core/*.h` must see it.

## Step 3 — Rewrite `fabric_mmog_asset.cpp` as a wrapper

Strip the inlined algorithm bodies and delegate everything to
`libidtx_core`. Example for the CDC + upload path
(`upload_asset` becomes ~10 lines instead of ~250):

```cpp
#include "thirdparty/idtx_core/core/include/idtx_core/idtx_chunker.h"
#include "thirdparty/idtx_core/core/include/idtx_core/idtx_transport.h"

Vector<uint8_t> FabricMMOGAsset::upload_asset(
        const String &p_store_url,
        const Vector<uint8_t> &p_file_data,
        String &r_error) {
    const CharString url = p_store_url.utf8();
    idtx_transport_t *t = idtx_transport_new(url.get_data());
    if (t == nullptr) {
        r_error = "idtx_transport_new failed";
        return Vector<uint8_t>();
    }

    char caibx_url[1024] = {0};
    int32_t rc = idtx_chunker_upload_asset(
            t, "asset", p_file_data.ptr(),
            size_t(p_file_data.size()),
            caibx_url, sizeof(caibx_url));
    if (rc != 0) {
        r_error = vformat("idtx_chunker_upload_asset rc=%d status=%d err=%s",
                          rc, idtx_transport_last_status(t),
                          idtx_transport_last_error(t));
        idtx_transport_destroy(t);
        return Vector<uint8_t>();
    }

    // upload_asset's legacy contract returns the caibx bytes, not the
    // URL — fetch them back and return.
    idtx_buffer_t *idx = nullptr;
    rc = idtx_transport_get_caibx(t, caibx_url, &idx);
    idtx_transport_destroy(t);
    if (rc != 0) {
        r_error = "could not fetch back uploaded caibx";
        return Vector<uint8_t>();
    }
    Vector<uint8_t> out;
    out.resize(int(idtx_buffer_size(idx)));
    if (!out.is_empty()) {
        memcpy(out.ptrw(), idtx_buffer_data(idx), idtx_buffer_size(idx));
    }
    idtx_buffer_destroy(idx);
    return out;
}
```

Apply the same pattern to:

| FabricMMOGAsset method | libidtx_core entrypoint |
| -- | -- |
| `sha512_256`                  | `idtx_chunker_sha512_256` |
| `build_chunk_url`             | `idtx_chunker_build_chunk_url` |
| `parse_caibx`                 | `idtx_chunker_parse_caibx` + accessors |
| `decompress_and_verify_chunk` | `idtx_chunker_decompress_and_verify` *(pending zstd)* |
| `put_chunk`                   | `idtx_transport_put_chunk` |
| `http_request_blocking`       | `idtx_transport_*` (REST verbs) |
| `parse_script_key_json`       | `idtx_aes_parse_script_key_json` |
| `request_asset_key`           | `idtx_transport_*` + the JSON parser above |
| `upload_asset`                | `idtx_chunker_upload_asset` (see snippet above) |
| `fetch_asset`                 | `idtx_chunker_assemble` |

Drop the local SHA-512/256 implementation, the Buzhash table, the
binary cursor helpers, and the manifest JSON parser — all gone.

The `GDREGISTER_CLASS` block and the GDScript-callable wrappers
(`upload_asset_gd`, `http_post_gd`) stay untouched; they keep their
existing signatures.

## Step 4 — Delete the redundant `thirdparty/desync/` fixtures (optional)

`thirdparty/desync/testdata/blob1.caibx` and the `.cacnk` chunk
fixtures can stay — they're still useful as test inputs for the
wrapper. The CDC code that referenced them is gone from the Godot
fork, but the wrapper-level tests in
`modules/multiplayer_fabric_asset/tests/` continue to feed those
files through the C ABI.

## Step 5 — Verify parity

Run the parity smoke test from this repo:

```bash
cd idtx-flow-v-sekai
GODOT=/path/to/godot-with-fabric-module-build \
ARIA_URL=http://localhost:4000 \
  ./tools/idtxcli/parity_smoke_test.sh
```

The engine-module path is the third arm of the smoke test; it's
silently skipped today (commented as "pending ART-47 #13"). Once this
migration lands, set `GODOT` to the engine-module-enabled binary and
the third arm runs against the same fixture. All three paths must
produce identical chunk-id lists and identical assembled SHA-512/256.

## Step 6 — Land the patch

PR against `multiplayer-fabric-godot` with:
1. `thirdparty/idtx_core/` (subtree add + update.sh)
2. `modules/multiplayer_fabric_asset/SCsub` (libidtx_core source list +
   `IDTX_CORE_STATIC` define)
3. `modules/multiplayer_fabric_asset/fabric_mmog_asset.{cpp,h}` (shim
   rewrite, ~400 lines down from ~1956)
4. Updated tests under
   `modules/multiplayer_fabric_asset/tests/` to assert the same output
   bytes as before the migration

Title: `multiplayer_fabric_asset: delegate byte-level work to libidtx_core (ART-47)`

## Why this is safe

The libidtx_core CDC, SHA-512/256, caibx parse/emit, and HTTP
transport are byte-for-byte ports of the Godot module's existing
algorithm. The Buzhash table is identical (256 uint32 entries from
`desync/chunker.go`), the discriminator formula is the same constants,
the caibx wire format magics match (FormatIndex `0x96824d9c7b129ff9`,
FormatTable `0xe75b9e112f17417d`, tail `0x4b4f050e5549ecd1`), and
SHA-512/256 uses the FIPS 180-4 §5.3.6 IVs (not a SHA-512 truncation).

A fixture round-trip through the wrapper produces the same caibx as
the inlined implementation does today.
