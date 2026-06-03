// idtx_core_loader — runtime loader for libidtx_core (CHI-312).
//
// The GDExtension does NOT link libidtx_core (nor its statically-linked
// OpenUSD). Instead it consumes the generated dlopen dispatch table
// (core/idtx_core.sigs -> generate_stubs.py):
//
//   * Windows: link the import lib derived from the checked-in `.def` with
//     `/DELAYLOAD`; the first `idtx_core_*` call binds to whatever DLL is
//     already resolved. We LoadLibraryEx it from the addon bin dir up front
//     so that bind targets the bundled copy.
//   * POSIX: the generated `idtx_core_stubs.cc` provides forwarding thunks;
//     `core::InitializeStubs` dlopen's the lib and fills them.
//
// Call idtxflow_load_idtx_core() once, before any idtx_core_* call.

#ifndef IDTXFLOW_IDTX_CORE_LOADER_H
#define IDTXFLOW_IDTX_CORE_LOADER_H

namespace idtxflow {

// Loads libidtx_core from the directory of this GDExtension module.
// Returns true on success. Idempotent: a second call is a no-op once loaded.
bool load_idtx_core();

}  // namespace idtxflow

#endif  // IDTXFLOW_IDTX_CORE_LOADER_H
