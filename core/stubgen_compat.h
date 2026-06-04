// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// stubgen_compat.h — inserted (via --macro-include) after the system
// includes in the generate_stubs.py-emitted idtx_core_stubs.cc. Two jobs:
//
//   1. Make the C ABI types visible to the generated dispatch table.
//      Every static function pointer and weak forwarder in the stub .cc
//      is typed in terms of idtx_avatar_t*, idtx_usd_export_opts_t*,
//      idtx_progress_fn, etc. — so the one header that declares them all
//      must be in scope. We have it: idtx_core.h. (native_media used a
//      hand-written gst_decls.h here only because GStreamer ships no
//      single umbrella header; libidtx_core does.)
//
//   2. Provide the DISABLE_CFI_ICALL macro the generator emits ahead of
//      every dispatched indirect call. It opts a call out of Chromium's
//      Control-Flow-Integrity indirect-call check. idtx-flow doesn't ship
//      CFI on this path, so the macro is intentionally empty.

#pragma once

#include "idtx_core/idtx_core.h"
// The CDN/crypto ABI lives in sibling headers; the dispatch table is typed in
// terms of their handles (idtx_buffer_t, idtx_caibx_t, idtx_transport_t) and
// size macros (IDTX_CHUNKER_CHUNK_ID_BYTES, IDTX_AES_KEY_BYTES, ...), so they
// must be in scope for the generated POSIX thunks too.
#include "idtx_core/idtx_chunker.h"
#include "idtx_core/idtx_transport.h"
#include "idtx_core/idtx_aes.h"
#include "idtx_core/idtx_scene.h"

#ifndef DISABLE_CFI_ICALL
#define DISABLE_CFI_ICALL
#endif
