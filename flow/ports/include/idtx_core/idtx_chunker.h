// Copyright 2026 V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// idtx_chunker — casync-compatible content-defined chunking + index
// I/O, ported from multiplayer-fabric-godot/modules/multiplayer_fabric_
// asset/fabric_mmog_asset.cpp into a Godot-free C ABI.
//
// Three host targets compile this from the same source tree:
//   1. shared lib (GDExtension addon/IDTXFlow/ + standalone CLI)
//   2. static archive (multiplayer-fabric-godot engine module via
//      `git subtree --squash` at thirdparty/idtx_core/)
//   3. Godot module shim (~400-line wrapper exposing the existing
//      FabricMMOGAsset RefCounted API; method bodies delegate here)
//
// All three produce byte-identical chunk IDs from the same input —
// CDC, SHA-512/256, and zstd are deterministic and the .cpp files do
// not differ across builds. The parity smoke test (ART-47) only needs
// to validate the marshaling layers, not the algorithm.

#ifndef IDTX_CORE_IDTX_CHUNKER_H
#define IDTX_CORE_IDTX_CHUNKER_H

#include "idtx_core/idtx_core.h"   // pulls in IDTX_CORE_API + stdint/stddef

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------
// Opaque handles. Layout is private; ABI-stable across patch releases.
// ---------------------------------------------------------------------

typedef struct idtx_buffer       idtx_buffer_t;       // owned byte buffer
typedef struct idtx_chunklist    idtx_chunklist_t;    // list of (offset, size, id)
typedef struct idtx_caibx        idtx_caibx_t;        // parsed .caibx index

// Generic byte-buffer accessors. The buffer's bytes live inside the
// library; `data` is valid until idtx_buffer_destroy() is called.
IDTX_CORE_API const uint8_t* idtx_buffer_data(const idtx_buffer_t* buf);
IDTX_CORE_API size_t         idtx_buffer_size(const idtx_buffer_t* buf);
IDTX_CORE_API void           idtx_buffer_destroy(idtx_buffer_t* buf);

// ---------------------------------------------------------------------
// Content-addressable identity.
//
// Chunk IDs are SHA-512/256 (FIPS 180-4) — 32 bytes — NOT a truncation
// of SHA-512. Matches the upstream folbricht/desync default exactly.
// Godot's HashingContext doesn't expose SHA-512/256, which is why the
// existing FabricMMOGAsset class carries a standalone implementation.
// We carry the same implementation here, behind the C ABI.
// ---------------------------------------------------------------------

#define IDTX_CHUNKER_CHUNK_ID_BYTES 32

IDTX_CORE_API void idtx_chunker_sha512_256(
    const uint8_t* data,
    size_t         len,
    uint8_t        out_id[IDTX_CHUNKER_CHUNK_ID_BYTES]);

// Build the desync HTTP store URL for a compressed chunk. desync shards
// the store by the first 4 lowercase hex characters of the chunk ID,
// so the layout is `{store}/xxxx/<full-hex>.cacnk`. Any trailing slash
// on the store URL is stripped so both forms produce identical output.
//
// `out` must point to at least
//     strlen(store_url) + 1 /* "/" */ + 4 /* shard */ + 1 /* "/" */
//     + 64 /* hex */ + 6 /* ".cacnk" */ + 1 /* NUL */ = strlen+76
// bytes. `cap` is the actual capacity; the function writes a NUL-
// terminated string and returns silently if `cap` is insufficient.
IDTX_CORE_API void idtx_chunker_build_chunk_url(
    const char*   store_url,
    const uint8_t id[IDTX_CHUNKER_CHUNK_ID_BYTES],
    char*         out,
    size_t        cap);

// ---------------------------------------------------------------------
// Content-defined chunking — desync rolling hash, 16K..256K window.
//
// Variable-size chunks: the chunker scans the byte stream with a
// rolling hash; whenever the low N bits of the hash hit a magic value
// (controlled by avg_size), it cuts a chunk boundary. Stays within
// [min_size, max_size]. Identical inputs always produce identical
// boundaries — that's what makes the cache content-addressable across
// peers.
// ---------------------------------------------------------------------

#define IDTX_CHUNKER_DEFAULT_MIN  (16  * 1024)
#define IDTX_CHUNKER_DEFAULT_AVG  (64  * 1024)
#define IDTX_CHUNKER_DEFAULT_MAX  (256 * 1024)

// Run CDC over `data` and emit an idtx_chunklist_t with one entry per
// chunk. Caller frees with idtx_chunklist_destroy. Pass min=avg=max=0
// to use the desync defaults (16K/64K/256K).
//
// Returns 0 on success, non-zero on failure:
//   1 = invalid argument (data NULL, len 0, sizes inconsistent)
//   2 = out of memory
IDTX_CORE_API int32_t idtx_chunker_cdc(
    const uint8_t*       data,
    size_t               len,
    size_t               min_size,
    size_t               avg_size,
    size_t               max_size,
    idtx_chunklist_t**   out_chunks);

IDTX_CORE_API int32_t idtx_chunklist_count(const idtx_chunklist_t* cl);

// Returns the [offset, size, id] triplet for chunk `index`. The
// chunk's bytes are the substring `data[offset .. offset+size)` of
// the original input. Pass NULL for any out pointer to skip it.
IDTX_CORE_API int32_t idtx_chunklist_get(
    const idtx_chunklist_t* cl,
    int32_t                 index,
    uint64_t*               out_offset,
    uint64_t*               out_size,
    uint8_t                 out_id[IDTX_CHUNKER_CHUNK_ID_BYTES]);

IDTX_CORE_API void idtx_chunklist_destroy(idtx_chunklist_t* cl);

// ---------------------------------------------------------------------
// .caibx index parse + emit.
//
// Wire format (folbricht/desync, FormatIndex + FormatTable):
//
//   FormatIndex header     (48 B):
//     u64 size            = 48
//     u64 type            = 0x96824d9c7b129ff9
//     u64 feature_flags   (= 0 for our baseline; no chunker tuning carried)
//     u64 chunk_size_min
//     u64 chunk_size_avg
//     u64 chunk_size_max
//
//   FormatTable header     (16 B):
//     u64 size            = 0xffffffffffffffff (= MAX_UINT64)
//     u64 type            = 0xe75b9e112f17417d
//
//   Repeating entries:
//     u64 end_offset      (cumulative byte offset after this chunk)
//     u8[32] chunk_id     (SHA-512/256)
//
//   Tail marker:
//     u64                 = 0
//     u64                 = 0x4b4f050e5549ecd1
// ---------------------------------------------------------------------

#define IDTX_CAIBX_FORMAT_INDEX_TYPE 0x96824d9c7b129ff9ULL
#define IDTX_CAIBX_FORMAT_TABLE_TYPE 0xe75b9e112f17417dULL
#define IDTX_CAIBX_TAIL_MARKER       0x4b4f050e5549ecd1ULL

IDTX_CORE_API int32_t idtx_chunker_parse_caibx(
    const uint8_t*  data,
    size_t          len,
    idtx_caibx_t**  out_index);

IDTX_CORE_API int32_t idtx_caibx_chunk_count(const idtx_caibx_t* idx);
IDTX_CORE_API int32_t idtx_caibx_get_chunk(
    const idtx_caibx_t* idx,
    int32_t             index,
    uint64_t*           out_offset,
    uint64_t*           out_size,
    uint8_t             out_id[IDTX_CHUNKER_CHUNK_ID_BYTES]);
IDTX_CORE_API void    idtx_caibx_destroy(idtx_caibx_t* idx);

// Serialize a chunklist (typically the output of idtx_chunker_cdc) to a
// .caibx byte buffer. Caller owns the returned buffer.
IDTX_CORE_API int32_t idtx_chunker_emit_caibx(
    const idtx_chunklist_t* cl,
    size_t                  chunk_size_min,
    size_t                  chunk_size_avg,
    size_t                  chunk_size_max,
    idtx_buffer_t**         out_caibx);

// ---------------------------------------------------------------------
// .cacnk verify (zstd decompress + SHA-512/256 check).
//
// `expected_id` is the chunk's content-address from the .caibx. On
// success the decompressed bytes are written to `out_plain` and the
// function returns 0. Non-zero failure codes:
//   1 = invalid argument
//   2 = zstd decompress failed
//   3 = sha512_256 mismatch (chunk corrupted in transit or attacker
//       attempted a substitution)
// ---------------------------------------------------------------------

IDTX_CORE_API int32_t idtx_chunker_decompress_and_verify(
    const uint8_t*  cacnk_zstd,
    size_t          cacnk_len,
    const uint8_t   expected_id[IDTX_CHUNKER_CHUNK_ID_BYTES],
    idtx_buffer_t** out_plain);

// Compress a plaintext chunk into a .cacnk byte buffer (zstd level
// matches the desync default of 3).
IDTX_CORE_API int32_t idtx_chunker_compress(
    const uint8_t*  plain,
    size_t          plain_len,
    idtx_buffer_t** out_cacnk);

#ifdef __cplusplus
}
#endif

#endif // IDTX_CORE_IDTX_CHUNKER_H
