/**************************************************************************/
/*  IDTXFlowChunker.cpp                                                   */
/**************************************************************************/
/* Copyright 2026 The openusd-fabric authors / V-Sekai contributors.      */
/* SPDX-License-Identifier: Apache-2.0 OR MPL-2.0                         */
/**************************************************************************/

#include "IDTXFlowChunker.h"

#include <godot_cpp/core/class_db.hpp>

#include "idtx_core/idtx_chunker.h"
#include "idtx_core/idtx_transport.h"

#include <cstring>

using namespace godot;

IDTXFlowChunker::IDTXFlowChunker() = default;

IDTXFlowChunker::~IDTXFlowChunker() {
	if (transport != nullptr) {
		idtx_transport_destroy(transport);
		transport = nullptr;
	}
}

bool IDTXFlowChunker::open(const String &p_aria_base_url) {
	if (transport != nullptr) {
		idtx_transport_destroy(transport);
		transport = nullptr;
	}
	const CharString url = p_aria_base_url.utf8();
	transport = idtx_transport_new(url.get_data());
	return transport != nullptr;
}

void IDTXFlowChunker::set_auth(const String &p_bearer_token) {
	if (transport == nullptr) {
		return;
	}
	const CharString token = p_bearer_token.utf8();
	idtx_transport_set_auth(transport, token.get_data());
}

bool IDTXFlowChunker::set_insecure_tls(bool p_insecure) {
	if (transport == nullptr) {
		return false;
	}
	return idtx_transport_set_insecure_tls(transport, p_insecure ? 1 : 0) != 0;
}

String IDTXFlowChunker::bake(const String &p_index_name, const PackedByteArray &p_blob) {
	if (transport == nullptr) {
		return String();
	}
	const CharString name = p_index_name.utf8();
	char url[1024] = { 0 };
	const int32_t rc = idtx_chunker_upload_asset(
			transport,
			name.get_data(),
			p_blob.ptr(), size_t(p_blob.size()),
			url, sizeof(url));
	if (rc != 0) {
		return String();
	}
	return String(url);
}

PackedByteArray IDTXFlowChunker::fetch(const String &p_caibx_url) {
	if (transport == nullptr) {
		return PackedByteArray();
	}

	const CharString url = p_caibx_url.utf8();
	idtx_buffer_t *caibx_buf = nullptr;
	int32_t rc = idtx_transport_get_caibx(transport, url.get_data(), &caibx_buf);
	if (rc != 0) {
		return PackedByteArray();
	}

	idtx_caibx_t *idx = nullptr;
	rc = idtx_chunker_parse_caibx(idtx_buffer_data(caibx_buf), idtx_buffer_size(caibx_buf), &idx);
	idtx_buffer_destroy(caibx_buf);
	if (rc != 0) {
		return PackedByteArray();
	}

	idtx_buffer_t *blob = nullptr;
	rc = idtx_chunker_assemble(transport, idx, &blob);
	idtx_caibx_destroy(idx);
	if (rc != 0) {
		return PackedByteArray();
	}

	PackedByteArray out;
	out.resize(int64_t(idtx_buffer_size(blob)));
	if (!out.is_empty()) {
		memcpy(out.ptrw(), idtx_buffer_data(blob), idtx_buffer_size(blob));
	}
	idtx_buffer_destroy(blob);
	return out;
}

bool IDTXFlowChunker::verify(const String &p_caibx_url) {
	if (transport == nullptr) {
		return false;
	}

	const CharString url = p_caibx_url.utf8();
	idtx_buffer_t *caibx_buf = nullptr;
	int32_t rc = idtx_transport_get_caibx(transport, url.get_data(), &caibx_buf);
	if (rc != 0) {
		return false;
	}

	idtx_caibx_t *idx = nullptr;
	rc = idtx_chunker_parse_caibx(idtx_buffer_data(caibx_buf), idtx_buffer_size(caibx_buf), &idx);
	idtx_buffer_destroy(caibx_buf);
	if (rc != 0) {
		return false;
	}

	bool ok = true;
	const int32_t n = idtx_caibx_chunk_count(idx);
	for (int32_t i = 0; i < n; i++) {
		uint8_t id[IDTX_CHUNKER_CHUNK_ID_BYTES];
		idtx_caibx_get_chunk(idx, i, nullptr, nullptr, id);
		if (idtx_transport_head_chunk(transport, id) != 0) {
			ok = false;
			break;
		}
	}
	idtx_caibx_destroy(idx);
	return ok;
}

PackedByteArray IDTXFlowChunker::sha512_256(const PackedByteArray &p_data) {
	PackedByteArray out;
	out.resize(IDTX_CHUNKER_CHUNK_ID_BYTES);
	idtx_chunker_sha512_256(p_data.ptr(), size_t(p_data.size()), out.ptrw());
	return out;
}

String IDTXFlowChunker::build_chunk_url(const String &p_store_url, const PackedByteArray &p_chunk_id) {
	if (p_chunk_id.size() != IDTX_CHUNKER_CHUNK_ID_BYTES) {
		return String();
	}
	const CharString store = p_store_url.utf8();
	char url[512] = { 0 };
	idtx_chunker_build_chunk_url(store.get_data(), p_chunk_id.ptr(), url, sizeof(url));
	return String(url);
}

int IDTXFlowChunker::last_status() const {
	return transport != nullptr ? idtx_transport_last_status(transport) : 0;
}

String IDTXFlowChunker::last_error() const {
	return transport != nullptr ? String(idtx_transport_last_error(transport)) : String();
}

void IDTXFlowChunker::_bind_methods() {
	ClassDB::bind_method(D_METHOD("open", "aria_base_url"), &IDTXFlowChunker::open);
	ClassDB::bind_method(D_METHOD("set_auth", "bearer_token"), &IDTXFlowChunker::set_auth);
	ClassDB::bind_method(D_METHOD("set_insecure_tls", "insecure"), &IDTXFlowChunker::set_insecure_tls);
	ClassDB::bind_method(D_METHOD("bake", "index_name", "blob"), &IDTXFlowChunker::bake);
	ClassDB::bind_method(D_METHOD("fetch", "caibx_url"), &IDTXFlowChunker::fetch);
	ClassDB::bind_method(D_METHOD("verify", "caibx_url"), &IDTXFlowChunker::verify);
	ClassDB::bind_method(D_METHOD("sha512_256", "data"), &IDTXFlowChunker::sha512_256);
	ClassDB::bind_method(D_METHOD("build_chunk_url", "store_url", "chunk_id"), &IDTXFlowChunker::build_chunk_url);
	ClassDB::bind_method(D_METHOD("last_status"), &IDTXFlowChunker::last_status);
	ClassDB::bind_method(D_METHOD("last_error"), &IDTXFlowChunker::last_error);
}
