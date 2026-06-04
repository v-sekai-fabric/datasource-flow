/**************************************************************************/
/*  IDTXFlowChunker.h                                                     */
/**************************************************************************/
/* Copyright 2026 The openusd-fabric authors / V-Sekai contributors.      */
/* SPDX-License-Identifier: Apache-2.0 OR MPL-2.0                         */
/**************************************************************************/

// IDTXFlowChunker — GDScript-facing wrapper around libidtx_core's casync
// chunker + aria-storage transport. Provides parity with the standalone
// CLI (`idtxcli bake / fetch / verify`) inside Godot, so the same
// asset → CDN → asset roundtrip can be exercised either from the editor
// / a headless tool script OR from a shell.
//
// The class is intentionally thin — bodies marshal PackedByteArray ↔
// const uint8_t* / size_t and call the C ABI. The byte-level algorithm
// (CDC, SHA-512/256, caibx parse/emit, HTTP transport) lives once in
// libidtx_core; this class CANNOT diverge from the CLI behaviour.
//
// Naming follows the existing IDTXFlow* convention used by
// IDTXFlowExporter for non-Node3D utility classes in this extension.

#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

// The C ABI transport handle. Must be included BEFORE `namespace godot` so the
// member below binds to the global `::idtx_transport_t`; otherwise the
// elaborated `struct idtx_transport` would declare a distinct godot::idtx_transport.
#include "idtx_core/idtx_transport.h"

namespace godot {

class IDTXFlowChunker : public RefCounted {
	GDCLASS(IDTXFlowChunker, RefCounted);

public:
	IDTXFlowChunker();
	~IDTXFlowChunker() override;

	// Configure the aria-storage endpoint. Required before any
	// bake/fetch/verify call. Returns true on success.
	bool open(const String &p_aria_base_url);

	// Optional auth bearer (Authorization: Bearer <token>).
	void set_auth(const String &p_bearer_token);

	// Insecure TLS toggle — localhost dev only. Returns the previous setting.
	bool set_insecure_tls(bool p_insecure);

	// CDN-side workflows. Empty/null return signals failure; check
	// last_status() and last_error() for diagnostics.

	String bake(const String &p_index_name, const PackedByteArray &p_blob);
	PackedByteArray fetch(const String &p_caibx_url);
	bool verify(const String &p_caibx_url);

	// Pure addressing helpers — useful for tests that validate parity
	// against the CLI without hitting the network.

	PackedByteArray sha512_256(const PackedByteArray &p_data);
	String build_chunk_url(const String &p_store_url, const PackedByteArray &p_chunk_id);

	// Diagnostics.
	int last_status() const;
	String last_error() const;

protected:
	static void _bind_methods();

private:
	::idtx_transport_t *transport = nullptr;
};

} // namespace godot
