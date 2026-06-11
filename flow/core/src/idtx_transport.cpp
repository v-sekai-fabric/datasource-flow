// Copyright 2026 V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// idtx_transport — aria-storage HTTP transport. Backed by ixwebsocket
// IXHttpClient in sync mode (per ART-47 plan: sync API on a worker
// thread, not async callbacks; async is for WebSocket frame
// lifetimes, not multi-MB HTTP requests).
//
// One C ABI surface, three host targets: same source compiles into the
// shared lib (GDExtension + CLI), the static archive (Godot engine
// module), and any future P/Invoke surface.

#include "idtx_core/idtx_transport.h"
#include "idtx_core/idtx_chunker.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXHttp.h>
#include <ixwebsocket/IXSocketTLSOptions.h>

namespace {

std::string strip_trailing_slashes(std::string s) {
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

std::string hex_id(const uint8_t id[IDTX_CHUNKER_CHUNK_ID_BYTES]) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.resize(IDTX_CHUNKER_CHUNK_ID_BYTES * 2);
    for (int i = 0; i < IDTX_CHUNKER_CHUNK_ID_BYTES; ++i) {
        out[i * 2 + 0] = digits[(id[i] >> 4) & 0xf];
        out[i * 2 + 1] = digits[ id[i]       & 0xf];
    }
    return out;
}

} // namespace

struct idtx_transport {
    std::string                    base_url;
    std::string                    auth_bearer;
    std::string                    cache_dir;
    bool                           insecure_tls = false;
    ix::HttpClient                 client{false};   // sync mode
    int32_t                        last_status = 0;
    std::string                    last_error;
};

namespace {

ix::HttpRequestArgsPtr make_args(idtx_transport_t* t) {
    auto args = t->client.createRequest();
    if (!t->auth_bearer.empty()) {
        args->extraHeaders["Authorization"] = "Bearer " + t->auth_bearer;
    }
    args->extraHeaders["User-Agent"] = "idtx-flow/0";
    return args;
}

void apply_tls_options(idtx_transport_t* t) {
    ix::SocketTLSOptions opts;
    if (t->insecure_tls) {
        opts.disable_hostname_validation = true;
        opts.caFile = "NONE";
    }
    t->client.setTLSOptions(opts);
}

std::string store_chunk_url(const std::string& base,
                            const uint8_t id[IDTX_CHUNKER_CHUNK_ID_BYTES]) {
    std::string hex = hex_id(id);
    return base + "/store/" + hex.substr(0, 4) + "/" + hex + ".cacnk";
}

idtx_buffer_t* make_buffer_from_string(const std::string& s) {
    extern struct idtx_buffer; // forward-decl from idtx_chunker.cpp
    // We can't construct opaque idtx_buffer here without sharing the
    // struct definition; expose a helper in idtx_chunker.cpp instead.
    // For now, construct via a vector and let idtx_chunker.cpp's make_buffer
    // do the allocation. We need a small bridge function.
    (void)s;
    return nullptr;
}

} // namespace

// Bridge: defined in idtx_chunker.cpp's anonymous namespace. We expose
// an internal extern "C++" helper through a thin function declared in
// the .h-less internal header. Inline here for simplicity.
struct idtx_buffer {
    uint8_t* data;
    size_t   size;
};

namespace {
idtx_buffer_t* buffer_from_vector(const std::vector<uint8_t>& v) {
    auto* b = new idtx_buffer();
    b->size = v.size();
    b->data = new uint8_t[v.size()];
    if (!v.empty()) std::memcpy(b->data, v.data(), v.size());
    return b;
}
idtx_buffer_t* buffer_from_string(const std::string& s) {
    auto* b = new idtx_buffer();
    b->size = s.size();
    b->data = new uint8_t[s.size()];
    if (!s.empty()) std::memcpy(b->data, s.data(), s.size());
    return b;
}
} // namespace

// ---------------------------------------------------------------------
// Lifecycle.
// ---------------------------------------------------------------------

extern "C" IDTX_CORE_API idtx_transport_t* idtx_transport_new(const char* aria_base_url)
{
    if (aria_base_url == nullptr) return nullptr;
    auto* t = new idtx_transport();
    t->base_url = strip_trailing_slashes(aria_base_url);
    apply_tls_options(t);
    return t;
}

extern "C" IDTX_CORE_API void idtx_transport_destroy(idtx_transport_t* t)
{
    delete t;
}

extern "C" IDTX_CORE_API void idtx_transport_set_auth(
    idtx_transport_t* t, const char* bearer_token)
{
    if (t == nullptr) return;
    t->auth_bearer = (bearer_token != nullptr) ? bearer_token : "";
}

extern "C" IDTX_CORE_API void idtx_transport_set_cache_dir(
    idtx_transport_t* t, const char* cache_dir)
{
    if (t == nullptr) return;
    t->cache_dir = (cache_dir != nullptr) ? cache_dir : "";
}

extern "C" IDTX_CORE_API int32_t idtx_transport_set_insecure_tls(
    idtx_transport_t* t, int32_t insecure)
{
    if (t == nullptr) return 0;
    int32_t prev = t->insecure_tls ? 1 : 0;
    t->insecure_tls = (insecure != 0);
    apply_tls_options(t);
    return prev;
}

// ---------------------------------------------------------------------
// Chunk-level ops.
// ---------------------------------------------------------------------

extern "C" IDTX_CORE_API int32_t idtx_transport_put_chunk(
    idtx_transport_t* t,
    const uint8_t     id[IDTX_CHUNKER_CHUNK_ID_BYTES],
    const uint8_t*    zstd_blob,
    size_t            len)
{
    if (t == nullptr || id == nullptr || (zstd_blob == nullptr && len > 0)) return 1;
    std::string url = store_chunk_url(t->base_url, id);
    std::string body(reinterpret_cast<const char*>(zstd_blob), len);

    auto args = make_args(t);
    args->extraHeaders["Content-Type"] = "application/octet-stream";

    auto resp = t->client.put(url, body, args);
    if (!resp) { t->last_error = "no response"; t->last_status = 0; return 2; }
    t->last_status = resp->statusCode;
    t->last_error  = resp->errorMsg;
    if (resp->statusCode < 200 || resp->statusCode >= 300) return 3;
    return 0;
}

extern "C" IDTX_CORE_API int32_t idtx_transport_head_chunk(
    idtx_transport_t* t,
    const uint8_t     id[IDTX_CHUNKER_CHUNK_ID_BYTES])
{
    if (t == nullptr || id == nullptr) return 1;
    std::string url = store_chunk_url(t->base_url, id);
    auto args = make_args(t);
    auto resp = t->client.head(url, args);
    if (!resp) { t->last_error = "no response"; t->last_status = 0; return 2; }
    t->last_status = resp->statusCode;
    t->last_error  = resp->errorMsg;
    if (resp->statusCode < 200 || resp->statusCode >= 300) return 3;
    return 0;
}

extern "C" IDTX_CORE_API int32_t idtx_transport_get_chunk(
    idtx_transport_t* t,
    const uint8_t     id[IDTX_CHUNKER_CHUNK_ID_BYTES],
    idtx_buffer_t**   out_zstd)
{
    if (t == nullptr || id == nullptr || out_zstd == nullptr) return 1;
    *out_zstd = nullptr;
    std::string url = store_chunk_url(t->base_url, id);
    auto args = make_args(t);
    auto resp = t->client.get(url, args);
    if (!resp) { t->last_error = "no response"; t->last_status = 0; return 2; }
    t->last_status = resp->statusCode;
    t->last_error  = resp->errorMsg;
    if (resp->statusCode < 200 || resp->statusCode >= 300) return 3;
    *out_zstd = buffer_from_string(resp->body);
    return 0;
}

// ---------------------------------------------------------------------
// Index ops.
// ---------------------------------------------------------------------

extern "C" IDTX_CORE_API int32_t idtx_transport_put_caibx(
    idtx_transport_t* t,
    const char*       name,
    const uint8_t*    caibx_bytes,
    size_t            len,
    char*             out_url,
    size_t            cap)
{
    if (t == nullptr || name == nullptr || caibx_bytes == nullptr) return 1;
    if (out_url == nullptr || cap == 0) return 1;

    std::string url = t->base_url + "/index/" + name + ".caibx";
    std::string body(reinterpret_cast<const char*>(caibx_bytes), len);

    auto args = make_args(t);
    args->extraHeaders["Content-Type"] = "application/octet-stream";

    auto resp = t->client.put(url, body, args);
    if (!resp) { t->last_error = "no response"; t->last_status = 0; return 2; }
    t->last_status = resp->statusCode;
    t->last_error  = resp->errorMsg;
    if (resp->statusCode < 200 || resp->statusCode >= 300) return 3;

    // Aria-storage may return a canonical URL in the body (or a Location
    // header). For now, return the PUT URL — caller can override if the
    // server publishes a different fetch path.
    std::snprintf(out_url, cap, "%s", url.c_str());
    return 0;
}

extern "C" IDTX_CORE_API int32_t idtx_transport_get_caibx(
    idtx_transport_t* t,
    const char*       url,
    idtx_buffer_t**   out_caibx)
{
    if (t == nullptr || url == nullptr || out_caibx == nullptr) return 1;
    *out_caibx = nullptr;

    // Accept both absolute URLs and base-relative paths.
    std::string full;
    if (std::strstr(url, "://") != nullptr) {
        full = url;
    } else {
        full = t->base_url;
        if (url[0] != '/') full.push_back('/');
        full += url;
    }

    auto args = make_args(t);
    auto resp = t->client.get(full, args);
    if (!resp) { t->last_error = "no response"; t->last_status = 0; return 2; }
    t->last_status = resp->statusCode;
    t->last_error  = resp->errorMsg;
    if (resp->statusCode < 200 || resp->statusCode >= 300) return 3;
    *out_caibx = buffer_from_string(resp->body);
    return 0;
}

// ---------------------------------------------------------------------
// High-level workflows. These compose the chunker C ABI with the
// transport above. Implementation: CDC the input, HEAD-or-PUT each
// chunk, emit a .caibx, PUT the .caibx, return the URL.
//
// Compression: chunks should be zstd-compressed before PUT to match the
// .cacnk convention. Since idtx_chunker_compress is still stubbed
// (zstd linkage pending), this function PUTs raw chunks for now and
// will be re-wired once compression lands.
// ---------------------------------------------------------------------

extern "C" IDTX_CORE_API int32_t idtx_chunker_upload_asset(
    idtx_transport_t* t,
    const char*       index_name,
    const uint8_t*    blob,
    size_t            len,
    char*             out_caibx_url,
    size_t            cap)
{
    if (t == nullptr || index_name == nullptr || blob == nullptr) return 1;
    if (out_caibx_url == nullptr || cap == 0) return 1;

    idtx_chunklist_t* cl = nullptr;
    int32_t rc = idtx_chunker_cdc(blob, len, 0, 0, 0, &cl);
    if (rc != 0) return rc;

    const int32_t n = idtx_chunklist_count(cl);
    for (int32_t i = 0; i < n; ++i) {
        uint64_t off = 0, sz = 0;
        uint8_t id[IDTX_CHUNKER_CHUNK_ID_BYTES];
        idtx_chunklist_get(cl, i, &off, &sz, id);

        int32_t head_rc = idtx_transport_head_chunk(t, id);
        if (head_rc == 0) continue;  // chunk already on server, skip
        if (head_rc != 3) {           // network error, not 404
            idtx_chunklist_destroy(cl);
            return head_rc;
        }
        // TODO: pass zstd-compressed chunk bytes once idtx_chunker_compress
        // lands. For now PUT the raw chunk so the parity smoke test runs
        // end-to-end against a localhost aria-storage that accepts raw.
        int32_t put_rc = idtx_transport_put_chunk(t, id, blob + off, sz);
        if (put_rc != 0) {
            idtx_chunklist_destroy(cl);
            return put_rc;
        }
    }

    idtx_buffer_t* caibx = nullptr;
    rc = idtx_chunker_emit_caibx(cl, 0, 0, 0, &caibx);
    idtx_chunklist_destroy(cl);
    if (rc != 0) return rc;

    rc = idtx_transport_put_caibx(t, index_name,
                                  idtx_buffer_data(caibx),
                                  idtx_buffer_size(caibx),
                                  out_caibx_url, cap);
    idtx_buffer_destroy(caibx);
    return rc;
}

extern "C" IDTX_CORE_API int32_t idtx_chunker_assemble(
    idtx_transport_t*  t,
    const idtx_caibx_t* idx,
    idtx_buffer_t**    out_blob)
{
    if (t == nullptr || idx == nullptr || out_blob == nullptr) return 1;
    *out_blob = nullptr;

    std::vector<uint8_t> assembled;
    const int32_t n = idtx_caibx_chunk_count(idx);
    for (int32_t i = 0; i < n; ++i) {
        uint64_t off = 0, sz = 0;
        uint8_t id[IDTX_CHUNKER_CHUNK_ID_BYTES];
        idtx_caibx_get_chunk(idx, i, &off, &sz, id);

        idtx_buffer_t* zstd_buf = nullptr;
        int32_t rc = idtx_transport_get_chunk(t, id, &zstd_buf);
        if (rc != 0) return rc;

        // TODO: decompress + verify via idtx_chunker_decompress_and_verify
        // once zstd linkage lands. For now treat the chunk body as raw
        // bytes (matches upload_asset's current raw-PUT behaviour).
        const uint8_t* p = idtx_buffer_data(zstd_buf);
        size_t         k = idtx_buffer_size(zstd_buf);
        assembled.insert(assembled.end(), p, p + k);
        idtx_buffer_destroy(zstd_buf);
    }

    *out_blob = buffer_from_vector(assembled);
    return 0;
}

// ---------------------------------------------------------------------
// Diagnostics.
// ---------------------------------------------------------------------

extern "C" IDTX_CORE_API int32_t idtx_transport_last_status(const idtx_transport_t* t)
{
    return t != nullptr ? t->last_status : 0;
}

extern "C" IDTX_CORE_API const char* idtx_transport_last_error(const idtx_transport_t* t)
{
    return t != nullptr ? t->last_error.c_str() : "";
}
