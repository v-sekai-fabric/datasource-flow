// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// idtx_aes — see idtx_aes.h. OpenSSL EVP-backed AES-128-GCM
// implementations. OpenSSL is already pulled in by ixwebsocket
// (scons/ixwebsocket.py), so this adds zero new dependencies.
//
// All entrypoints are real now:
//   - idtx_aes_gcm_encrypt: EVP_aes_128_gcm() with explicit 12-byte
//     IV. Output layout = ciphertext || 16-byte GCM tag.
//   - idtx_aes_gcm_decrypt: tag verification via EVP_DecryptFinal_ex
//     (returns 0 on mismatch — surfaces as code 3 = "tag mismatch").
//   - idtx_aes_parse_script_key_json: small JSON extractor + RFC 4648
//     base64 decoder.

#include "idtx_core/idtx_aes.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------
// idtx_buffer bridge — see idtx_chunker.cpp for the struct definition.
// ---------------------------------------------------------------------

struct idtx_buffer {
    uint8_t* data;
    size_t   size;
};

namespace {
idtx_buffer_t* make_buffer(const std::vector<uint8_t>& v) {
    auto* b = new idtx_buffer();
    b->size = v.size();
    b->data = new uint8_t[v.size()];
    if (!v.empty()) std::memcpy(b->data, v.data(), v.size());
    return b;
}
} // namespace

// ---------------------------------------------------------------------
// AES-128-GCM. Reuses OpenSSL libcrypto already pulled in by
// ixwebsocket (LIBS entry in scons/idtxcore.py).
// ---------------------------------------------------------------------

#include <openssl/evp.h>

namespace {

// RAII guard for EVP_CIPHER_CTX so the early-return paths don't leak.
struct EvpCtx {
    EVP_CIPHER_CTX* ctx;
    EvpCtx() : ctx(EVP_CIPHER_CTX_new()) {}
    ~EvpCtx() { if (ctx != nullptr) EVP_CIPHER_CTX_free(ctx); }
};

} // namespace

extern "C" IDTX_CORE_API int32_t idtx_aes_gcm_encrypt(
    const uint8_t   key[IDTX_AES_KEY_BYTES],
    const uint8_t   iv[IDTX_AES_IV_BYTES],
    const uint8_t*  plaintext,
    size_t          pt_len,
    idtx_buffer_t** out_ciphertext_with_tag)
{
    if (out_ciphertext_with_tag == nullptr) return 1;
    *out_ciphertext_with_tag = nullptr;
    if (key == nullptr || iv == nullptr) return 1;
    if (plaintext == nullptr && pt_len > 0) return 1;

    EvpCtx ctx;
    if (ctx.ctx == nullptr) return 2;

    if (EVP_EncryptInit_ex(ctx.ctx, EVP_aes_128_gcm(), nullptr, nullptr, nullptr) != 1) {
        return 2;
    }
    if (EVP_CIPHER_CTX_ctrl(ctx.ctx, EVP_CTRL_GCM_SET_IVLEN, IDTX_AES_IV_BYTES, nullptr) != 1) {
        return 2;
    }
    if (EVP_EncryptInit_ex(ctx.ctx, nullptr, nullptr, key, iv) != 1) {
        return 2;
    }

    std::vector<uint8_t> out(pt_len + IDTX_AES_TAG_BYTES);
    int out_len = 0;
    int total   = 0;
    if (pt_len > 0) {
        if (EVP_EncryptUpdate(ctx.ctx, out.data(), &out_len,
                              plaintext, int(pt_len)) != 1) {
            return 3;
        }
        total += out_len;
    }
    if (EVP_EncryptFinal_ex(ctx.ctx, out.data() + total, &out_len) != 1) {
        return 3;
    }
    total += out_len;

    if (EVP_CIPHER_CTX_ctrl(ctx.ctx, EVP_CTRL_GCM_GET_TAG,
                            IDTX_AES_TAG_BYTES,
                            out.data() + total) != 1) {
        return 3;
    }
    out.resize(size_t(total) + IDTX_AES_TAG_BYTES);

    *out_ciphertext_with_tag = make_buffer(out);
    return 0;
}

extern "C" IDTX_CORE_API int32_t idtx_aes_gcm_decrypt(
    const uint8_t   key[IDTX_AES_KEY_BYTES],
    const uint8_t   iv[IDTX_AES_IV_BYTES],
    const uint8_t*  ciphertext_with_tag,
    size_t          ct_len,
    idtx_buffer_t** out_plaintext)
{
    if (out_plaintext == nullptr) return 1;
    *out_plaintext = nullptr;
    if (key == nullptr || iv == nullptr || ciphertext_with_tag == nullptr) return 1;
    if (ct_len < IDTX_AES_TAG_BYTES) return 1;

    const size_t  body_len = ct_len - IDTX_AES_TAG_BYTES;
    const uint8_t* body    = ciphertext_with_tag;
    const uint8_t* tag     = ciphertext_with_tag + body_len;

    EvpCtx ctx;
    if (ctx.ctx == nullptr) return 2;

    if (EVP_DecryptInit_ex(ctx.ctx, EVP_aes_128_gcm(), nullptr, nullptr, nullptr) != 1) {
        return 2;
    }
    if (EVP_CIPHER_CTX_ctrl(ctx.ctx, EVP_CTRL_GCM_SET_IVLEN, IDTX_AES_IV_BYTES, nullptr) != 1) {
        return 2;
    }
    if (EVP_DecryptInit_ex(ctx.ctx, nullptr, nullptr, key, iv) != 1) {
        return 2;
    }

    std::vector<uint8_t> out(body_len);
    int out_len = 0;
    int total   = 0;
    if (body_len > 0) {
        if (EVP_DecryptUpdate(ctx.ctx, out.data(), &out_len,
                              body, int(body_len)) != 1) {
            return 2;
        }
        total += out_len;
    }

    // EVP_CTRL_GCM_SET_TAG must be set before Final; the Final call
    // is what actually verifies the tag and returns 0 on mismatch.
    if (EVP_CIPHER_CTX_ctrl(ctx.ctx, EVP_CTRL_GCM_SET_TAG,
                            IDTX_AES_TAG_BYTES,
                            const_cast<uint8_t*>(tag)) != 1) {
        return 2;
    }
    if (EVP_DecryptFinal_ex(ctx.ctx, out.data() + total, &out_len) != 1) {
        return 3;  // tag mismatch
    }
    total += out_len;
    out.resize(size_t(total));

    *out_plaintext = make_buffer(out);
    return 0;
}

// ---------------------------------------------------------------------
// Base64 decode (RFC 4648). Tiny, no dependency.
// ---------------------------------------------------------------------

namespace {

int b64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

bool b64_decode(const std::string& s, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve((s.size() * 3) / 4 + 4);
    int val = 0, bits = -8;
    for (char c : s) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        int v = b64_value(c);
        if (v < 0) return false;
        val = (val << 6) | v;
        bits += 6;
        if (bits >= 0) {
            out.push_back(uint8_t((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return true;
}

// Minimal JSON-string field extractor. Looks for `"<field>"` followed
// by `:` then a quoted string OR a number. Returns the value as text.
// Not a full JSON parser — intentional: avoids pulling in a dep, and
// the /auth/script_key payload is well-known.
bool extract_string(const std::string& json, const std::string& key, std::string& out) {
    std::string needle = "\"" + key + "\"";
    auto p = json.find(needle);
    if (p == std::string::npos) return false;
    p = json.find(':', p + needle.size());
    if (p == std::string::npos) return false;
    p = json.find('"', p);
    if (p == std::string::npos) return false;
    auto end = json.find('"', p + 1);
    if (end == std::string::npos) return false;
    out = json.substr(p + 1, end - p - 1);
    return true;
}

bool extract_number(const std::string& json, const std::string& key, uint64_t& out) {
    std::string needle = "\"" + key + "\"";
    auto p = json.find(needle);
    if (p == std::string::npos) return false;
    p = json.find(':', p + needle.size());
    if (p == std::string::npos) return false;
    ++p;
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t')) ++p;
    uint64_t v = 0;
    bool any = false;
    while (p < json.size() && json[p] >= '0' && json[p] <= '9') {
        v = v * 10 + uint64_t(json[p] - '0');
        ++p;
        any = true;
    }
    if (!any) return false;
    out = v;
    return true;
}

} // namespace

extern "C" IDTX_CORE_API int32_t idtx_aes_parse_script_key_json(
    const char* json,
    size_t      json_len,
    uint8_t     out_key[IDTX_AES_KEY_BYTES],
    uint8_t     out_iv[IDTX_AES_IV_BYTES],
    uint64_t*   out_ttl_seconds)
{
    if (json == nullptr || out_key == nullptr || out_iv == nullptr) return 1;

    std::string s(json, json_len);
    std::string key_b64, iv_b64;
    uint64_t    ttl = 0;
    if (!extract_string(s, "key", key_b64))    return 2;
    if (!extract_string(s, "iv",  iv_b64))     return 2;
    if (!extract_number(s, "ttl", ttl))        return 2;

    std::vector<uint8_t> key_bytes, iv_bytes;
    if (!b64_decode(key_b64, key_bytes)) return 2;
    if (!b64_decode(iv_b64,  iv_bytes))  return 2;
    if (key_bytes.size() != IDTX_AES_KEY_BYTES) return 3;
    if (iv_bytes.size()  != IDTX_AES_IV_BYTES)  return 3;

    std::memcpy(out_key, key_bytes.data(), IDTX_AES_KEY_BYTES);
    std::memcpy(out_iv,  iv_bytes.data(),  IDTX_AES_IV_BYTES);
    if (out_ttl_seconds != nullptr) *out_ttl_seconds = ttl;
    return 0;
}
