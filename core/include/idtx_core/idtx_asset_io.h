/**************************************************************************/
/*  idtx_asset_io.h                                                       */
/**************************************************************************/
/* Copyright 2026 The openusd-fabric authors / V-Sekai contributors.      */
/* SPDX-License-Identifier: Apache-2.0 OR MPL-2.0                         */
/**************************************************************************/

// Host asset I/O — lets libidtx_core resolve engine-specific URI schemes
// (res://, user://) when it opens USD, WITHOUT the core knowing about any
// engine. The host (Godot/Blender/viser/CLI) registers a callback that maps a
// scheme URI to an absolute filesystem path; the core's bundled ArResolver uses
// it for res/user-scheme assets referenced inside a stage. This is what lets the
// converter live in the core (CHI: one OpenUSD consumer) while res:// knowledge
// stays in the host — they meet at this flat C boundary.
//
// Scope: path globalization covers res:// / user:// (filesystem-backed) — the
// common Godot case, including absolute references inside a stage. Streamed
// schemes (http:// via the casync transport) read bytes rather than map a path
// and are a later addition.

#ifndef IDTX_CORE_IDTX_ASSET_IO_H
#define IDTX_CORE_IDTX_ASSET_IO_H

#include "idtx_core/idtx_core.h"   // IDTX_CORE_API + stdint

#ifdef __cplusplus
extern "C" {
#endif

// Host-provided asset I/O hooks. `user` is passed back to every callback.
typedef struct idtx_asset_io {
    void* user;

    // Map `uri` (e.g. "res://avatars/miroir.usda") to an absolute filesystem
    // path, written NUL-terminated into out_path (capacity `cap` bytes). Return
    // 1 if the URI was handled, 0 to let the core treat it as a plain path
    // (default-resolver behaviour). On truncation the host should still NUL-
    // terminate and may return 0.
    int32_t (*globalize_path)(void* user, const char* uri, char* out_path, int32_t cap);
} idtx_asset_io_t;

// Register the host asset-IO hooks (the struct is copied; the function pointers
// must stay valid for the process lifetime). Pass NULL to clear and fall back to
// plain filesystem resolution. Not thread-safe with concurrent stage opens.
IDTX_CORE_API void idtx_core_set_asset_io(const idtx_asset_io_t* io);

#ifdef __cplusplus
}
#endif

#endif // IDTX_CORE_IDTX_ASSET_IO_H
