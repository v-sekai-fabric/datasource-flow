// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// idtx_transport — aria-storage HTTP transport for casync chunks +
// indexes. Talks to https://github.com/V-Sekai-fire/aria-storage over
// REST. Same C ABI consumed by the standalone CLI, the GDExtension,
// and the Godot engine-module shim.
//
// Backend: ixwebsocket IXHttpClient (vendored via scons/ixwebsocket.py
// at v11.4.6 with OpenSSL TLS). Implementation uses the *sync* API on
// a worker thread — the async-callback path is for WebSocket frame
// lifetimes, not multi-MB HTTP request lifetimes.
//
// Aria-storage REST contract (all paths relative to the base URL passed
// to idtx_transport_new):
//
//   PUT  /store/{hex[0:4]}/{full-hex}.cacnk
//     Upload chunk. Idempotent — chunks are content-addressed by
//     SHA-512/256, so re-uploading the same chunk is a no-op.
//
//   HEAD /store/{hex[0:4]}/{full-hex}.cacnk
//     Existence check for dedup. Bake-side does HEAD before PUT so
//     existing chunks are not re-uploaded.
//
//   GET  /store/{hex[0:4]}/{full-hex}.cacnk
//     Fetch chunk. Body is the zstd-compressed payload.
//
//   PUT  /index/{name}.caibx
//   GET  /index/{caibx-url-path}.caibx
//     Upload / fetch index. The PUT response includes the canonical
//     URL the index can be fetched from; that's what callers thread
//     through to the player side.

#ifndef IDTX_CORE_IDTX_TRANSPORT_H
#define IDTX_CORE_IDTX_TRANSPORT_H

#include "idtx_core/idtx_chunker.h"   // pulls idtx_buffer_t + chunk-id width

#ifdef __cplusplus
extern "C" {
#endif

// Opaque transport handle. Holds the base URL, optional bearer token,
// the IXHttpClient instance, and the local chunk cache directory.
typedef struct idtx_transport idtx_transport_t;

// Create a transport bound to `aria_base_url` (e.g.
// "https://aria.v-sekai.example" or "http://localhost:4000"). Caller
// owns the returned handle and frees with idtx_transport_destroy.
// Returns NULL if aria_base_url is NULL or the underlying IXHttpClient
// failed to initialize.
IDTX_CORE_API idtx_transport_t* idtx_transport_new(const char* aria_base_url);

IDTX_CORE_API void idtx_transport_destroy(idtx_transport_t* t);

// Set the bearer token sent in the `Authorization: Bearer <token>`
// header on every subsequent request. Pass NULL or "" to clear.
IDTX_CORE_API void idtx_transport_set_auth(idtx_transport_t* t, const char* bearer_token);

// Override the local chunk cache directory. Default layout matches the
// existing FabricMMOGAsset two-level scheme: `<cache_dir>/{hex[0:4]}/
// {full-hex}.cacnk`. Pass NULL to use the platform default
// (`%LOCALAPPDATA%/v-sekai/idtx_cache/` on Windows, `~/.cache/v-sekai/
// idtx_cache/` on Linux, `~/Library/Caches/v-sekai/idtx_cache/` on
// macOS).
IDTX_CORE_API void idtx_transport_set_cache_dir(idtx_transport_t* t, const char* cache_dir);

// Disable TLS verification — for localhost dev only. Production should
// never call this. Returns the previous setting.
IDTX_CORE_API int32_t idtx_transport_set_insecure_tls(idtx_transport_t* t, int32_t insecure);

// ---------------------------------------------------------------------
// Chunk-level operations (all synchronous).
//
// HTTP failure semantics:
//   0   = success
//   1   = invalid argument (NULL transport, etc.)
//   2   = network error (DNS, connect, timeout, TLS)
//   3   = HTTP non-2xx status; the status code is recorded internally
//         and surfaced via idtx_transport_last_status()
//   4   = local I/O failure (cache write)
// ---------------------------------------------------------------------

// Upload `zstd_blob` to /store/{shard}/{hex}.cacnk. `id` must be the
// SHA-512/256 of the DECOMPRESSED payload, matching .caibx convention.
IDTX_CORE_API int32_t idtx_transport_put_chunk(
    idtx_transport_t* t,
    const uint8_t     id[IDTX_CHUNKER_CHUNK_ID_BYTES],
    const uint8_t*    zstd_blob,
    size_t            len);

// HEAD /store/{shard}/{hex}.cacnk for dedup. Returns 0 if the chunk
// exists (HTTP 200/204), 3 if it does not (404), or 2 on network error.
IDTX_CORE_API int32_t idtx_transport_head_chunk(
    idtx_transport_t* t,
    const uint8_t     id[IDTX_CHUNKER_CHUNK_ID_BYTES]);

// GET /store/{shard}/{hex}.cacnk. Returns the zstd-compressed payload
// in `out_zstd` (caller frees with idtx_buffer_destroy). The fetched
// chunk is mirrored into the local cache so a re-fetch is satisfied
// from disk.
IDTX_CORE_API int32_t idtx_transport_get_chunk(
    idtx_transport_t* t,
    const uint8_t     id[IDTX_CHUNKER_CHUNK_ID_BYTES],
    idtx_buffer_t**   out_zstd);

// ---------------------------------------------------------------------
// Index-level operations.
// ---------------------------------------------------------------------

// PUT /index/<name>.caibx. On success, `out_url` is populated with the
// canonical fetch URL (caller-allocated, `cap` bytes; truncated +
// NUL-terminated if too small). The URL is what callers pass to
// idtx_transport_get_caibx() or share with player clients.
IDTX_CORE_API int32_t idtx_transport_put_caibx(
    idtx_transport_t* t,
    const char*       name,
    const uint8_t*    caibx_bytes,
    size_t            len,
    char*             out_url,
    size_t            cap);

// GET an index by canonical URL (typically the output of put_caibx).
// `url` may be an absolute URL or a path relative to the transport's
// base URL.
IDTX_CORE_API int32_t idtx_transport_get_caibx(
    idtx_transport_t* t,
    const char*       url,
    idtx_buffer_t**   out_caibx);

// ---------------------------------------------------------------------
// High-level workflows (compose chunker + transport).
//
// upload_asset: CDC-chunk `blob`, for each chunk { HEAD-skip || PUT },
//               emit a .caibx, PUT the .caibx, return the fetch URL.
//               This is what `idtxcli bake` calls.
//
// assemble:     for each entry in the .caibx { local-cache || GET },
//               decompress + verify, concatenate into out_blob. This
//               is what `idtxcli fetch` calls.
// ---------------------------------------------------------------------

IDTX_CORE_API int32_t idtx_chunker_upload_asset(
    idtx_transport_t* t,
    const char*       index_name,
    const uint8_t*    blob,
    size_t            len,
    char*             out_caibx_url,
    size_t            cap);

IDTX_CORE_API int32_t idtx_chunker_assemble(
    idtx_transport_t*  t,
    const idtx_caibx_t* idx,
    idtx_buffer_t**    out_blob);

// ---------------------------------------------------------------------
// Diagnostics.
// ---------------------------------------------------------------------

// Last HTTP status the transport observed. Useful when a function
// returns code 3 and the caller wants to distinguish 404 from 401 etc.
IDTX_CORE_API int32_t idtx_transport_last_status(const idtx_transport_t* t);

// Last underlying error message (network error string, status reason,
// etc.). NUL-terminated, owned by the transport — do not free.
IDTX_CORE_API const char* idtx_transport_last_error(const idtx_transport_t* t);

#ifdef __cplusplus
}
#endif

#endif // IDTX_CORE_IDTX_TRANSPORT_H
