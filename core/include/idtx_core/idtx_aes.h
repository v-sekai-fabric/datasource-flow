// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// idtx_aes — AES-128-GCM encrypt/decrypt + script-key JSON parsing.
//
// Ported from multiplayer-fabric-godot/modules/multiplayer_fabric_asset/
// fabric_mmog_asset.{cpp,h}'s key-handling constants and the JSON
// helper. The Godot module delegates byte-level AES to Godot's
// CryptoServer; libidtx_core uses OpenSSL EVP_aes_128_gcm() directly
// (OpenSSL is already pulled in by ixwebsocket).
//
// All three deployment targets (engine module, GDExtension, CLI)
// compile from this same source so encrypted chunks have matching
// SHA-512/256 across hosts — required for content addressing to work
// when assets are uploaded encrypted.

#ifndef IDTX_CORE_IDTX_AES_H
#define IDTX_CORE_IDTX_AES_H

#include "idtx_core/idtx_chunker.h"   // idtx_buffer_t

#ifdef __cplusplus
extern "C" {
#endif

// Wire sizes — match VRMC_springBone-era V-Sekai key TTL constants.
#define IDTX_AES_KEY_BYTES 16   // AES-128
#define IDTX_AES_IV_BYTES  12   // GCM standard
#define IDTX_AES_TAG_BYTES 16   // GCM auth tag

// AES-128-GCM encrypt. The 12-byte IV must be unique per (key, message)
// triple; reusing an IV with the same key is catastrophic for
// confidentiality + authenticity. Caller is responsible for IV
// uniqueness (typically counter or CSPRNG-derived).
//
// Output layout: ciphertext (same length as plaintext) followed by the
// 16-byte GCM authentication tag.
//
// Returns 0 on success, non-zero on:
//   1 = invalid argument
//   2 = OpenSSL EVP failure (key/iv setup)
//   3 = encryption / finalize failure
IDTX_CORE_API int32_t idtx_aes_gcm_encrypt(
    const uint8_t   key[IDTX_AES_KEY_BYTES],
    const uint8_t   iv[IDTX_AES_IV_BYTES],
    const uint8_t*  plaintext,
    size_t          pt_len,
    idtx_buffer_t** out_ciphertext_with_tag);

// AES-128-GCM decrypt + tag verification. `in_ciphertext_with_tag` has
// the same layout as the encrypt output: ciphertext bytes followed by
// the 16-byte tag. Returns:
//   0 = success, *out_plaintext populated
//   1 = invalid argument
//   2 = OpenSSL EVP failure
//   3 = tag mismatch (attacker tampered the ciphertext or wrong key)
IDTX_CORE_API int32_t idtx_aes_gcm_decrypt(
    const uint8_t   key[IDTX_AES_KEY_BYTES],
    const uint8_t   iv[IDTX_AES_IV_BYTES],
    const uint8_t*  ciphertext_with_tag,
    size_t          ct_len,
    idtx_buffer_t** out_plaintext);

// ---------------------------------------------------------------------
// Script-key endpoint (aria-storage /auth/script_key).
//
// Wire format:
//   { "key": "<base64 16B>", "iv": "<base64 12B>", "ttl": <u64 seconds> }
//
// Parses the JSON body and writes raw key/iv into the caller-allocated
// buffers. Returns 0 on success, 1 on argument error, 2 on JSON parse
// failure, 3 on base64 length mismatch.
//
// Pure logic — no I/O. The HTTP round trip is the caller's
// responsibility (use idtx_transport for that).
// ---------------------------------------------------------------------

IDTX_CORE_API int32_t idtx_aes_parse_script_key_json(
    const char* json,
    size_t      json_len,
    uint8_t     out_key[IDTX_AES_KEY_BYTES],
    uint8_t     out_iv[IDTX_AES_IV_BYTES],
    uint64_t*   out_ttl_seconds);

#ifdef __cplusplus
}
#endif

#endif // IDTX_CORE_IDTX_AES_H
