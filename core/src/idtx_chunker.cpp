// Copyright 2026 V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// idtx_chunker — see idtx_chunker.h for the public C ABI contract.
//
// Ported from multiplayer-fabric-godot/modules/multiplayer_fabric_asset/
// fabric_mmog_asset.cpp (commit 98248e0251). Replaces Godot types with
// stdlib so the same algorithm runs in three deployment targets: the
// Godot engine module, the GDExtension, and the standalone CLI.
//
// The Buzhash CDC, .caibx parser/emitter, and SHA-512/256 hasher are
// deterministic — identical input yields identical chunk-id output
// across all three hosts. This is what makes the parity smoke test
// (ART-47) meaningful.
//
// zstd compress/verify (compress + decompress_and_verify) remains
// stubbed pending the zstd linkage decision (vendor via SCons vs rely
// on the OpenUSD-bundled zstd).

#include "idtx_core/idtx_chunker.h"

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

// ---------------------------------------------------------------------
// SHA-512/256 (FIPS 180-4, Section 5.3.6.2).
// ---------------------------------------------------------------------

namespace {

constexpr uint64_t SHA512_256_IV[8] = {
    0x22312194FC2BF72CULL, 0x9F555FA3C84C64C2ULL,
    0x2393B86B6F53B151ULL, 0x963877195940EABDULL,
    0x96283EE2A88EFFE3ULL, 0xBE5E1E2553863992ULL,
    0x2B0199FC2C85B8AAULL, 0x0EB72DDC81C52CA2ULL,
};

constexpr uint64_t SHA512_K[80] = {
    0x428A2F98D728AE22ULL, 0x7137449123EF65CDULL, 0xB5C0FBCFEC4D3B2FULL, 0xE9B5DBA58189DBBCULL,
    0x3956C25BF348B538ULL, 0x59F111F1B605D019ULL, 0x923F82A4AF194F9BULL, 0xAB1C5ED5DA6D8118ULL,
    0xD807AA98A3030242ULL, 0x12835B0145706FBEULL, 0x243185BE4EE4B28CULL, 0x550C7DC3D5FFB4E2ULL,
    0x72BE5D74F27B896FULL, 0x80DEB1FE3B1696B1ULL, 0x9BDC06A725C71235ULL, 0xC19BF174CF692694ULL,
    0xE49B69C19EF14AD2ULL, 0xEFBE4786384F25E3ULL, 0x0FC19DC68B8CD5B5ULL, 0x240CA1CC77AC9C65ULL,
    0x2DE92C6F592B0275ULL, 0x4A7484AA6EA6E483ULL, 0x5CB0A9DCBD41FBD4ULL, 0x76F988DA831153B5ULL,
    0x983E5152EE66DFABULL, 0xA831C66D2DB43210ULL, 0xB00327C898FB213FULL, 0xBF597FC7BEEF0EE4ULL,
    0xC6E00BF33DA88FC2ULL, 0xD5A79147930AA725ULL, 0x06CA6351E003826FULL, 0x142929670A0E6E70ULL,
    0x27B70A8546D22FFCULL, 0x2E1B21385C26C926ULL, 0x4D2C6DFC5AC42AEDULL, 0x53380D139D95B3DFULL,
    0x650A73548BAF63DEULL, 0x766A0ABB3C77B2A8ULL, 0x81C2C92E47EDAEE6ULL, 0x92722C851482353BULL,
    0xA2BFE8A14CF10364ULL, 0xA81A664BBC423001ULL, 0xC24B8B70D0F89791ULL, 0xC76C51A30654BE30ULL,
    0xD192E819D6EF5218ULL, 0xD69906245565A910ULL, 0xF40E35855771202AULL, 0x106AA07032BBD1B8ULL,
    0x19A4C116B8D2D0C8ULL, 0x1E376C085141AB53ULL, 0x2748774CDF8EEB99ULL, 0x34B0BCB5E19B48A8ULL,
    0x391C0CB3C5C95A63ULL, 0x4ED8AA4AE3418ACBULL, 0x5B9CCA4F7763E373ULL, 0x682E6FF3D6B2B8A3ULL,
    0x748F82EE5DEFB2FCULL, 0x78A5636F43172F60ULL, 0x84C87814A1F0AB72ULL, 0x8CC702081A6439ECULL,
    0x90BEFFFA23631E28ULL, 0xA4506CEBDE82BDE9ULL, 0xBEF9A3F7B2C67915ULL, 0xC67178F2E372532BULL,
    0xCA273ECEEA26619CULL, 0xD186B8C721C0C207ULL, 0xEADA7DD6CDE0EB1EULL, 0xF57D4F7FEE6ED178ULL,
    0x06F067AA72176FBAULL, 0x0A637DC5A2C898A6ULL, 0x113F9804BEF90DAEULL, 0x1B710B35131C471BULL,
    0x28DB77F523047D84ULL, 0x32CAAB7B40C72493ULL, 0x3C9EBE0A15C9BEBCULL, 0x431D67C49C100D4CULL,
    0x4CC5D4BECB3E42B6ULL, 0x597F299CFC657E2AULL, 0x5FCB6FAB3AD6FAECULL, 0x6C44198C4A475817ULL,
};

inline uint64_t rotr64(uint64_t x, unsigned n) { return (x >> n) | (x << (64 - n)); }

void sha512_compress(uint64_t h[8], const uint8_t block[128])
{
    uint64_t w[80];
    for (int i = 0; i < 16; ++i) {
        w[i] = (uint64_t(block[i*8 + 0]) << 56) | (uint64_t(block[i*8 + 1]) << 48) |
               (uint64_t(block[i*8 + 2]) << 40) | (uint64_t(block[i*8 + 3]) << 32) |
               (uint64_t(block[i*8 + 4]) << 24) | (uint64_t(block[i*8 + 5]) << 16) |
               (uint64_t(block[i*8 + 6]) <<  8) | (uint64_t(block[i*8 + 7]));
    }
    for (int i = 16; i < 80; ++i) {
        uint64_t s0 = rotr64(w[i-15], 1)  ^ rotr64(w[i-15], 8)  ^ (w[i-15] >> 7);
        uint64_t s1 = rotr64(w[i-2], 19)  ^ rotr64(w[i-2], 61)  ^ (w[i-2]  >> 6);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }

    uint64_t a = h[0], b = h[1], c = h[2], d = h[3];
    uint64_t e = h[4], f = h[5], g = h[6], hh = h[7];

    for (int i = 0; i < 80; ++i) {
        uint64_t S1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);
        uint64_t ch = (e & f) ^ (~e & g);
        uint64_t t1 = hh + S1 + ch + SHA512_K[i] + w[i];
        uint64_t S0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);
        uint64_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint64_t t2 = S0 + mj;

        hh = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

} // namespace

extern "C" IDTX_CORE_API void idtx_chunker_sha512_256(
    const uint8_t* data,
    size_t         len,
    uint8_t        out_id[IDTX_CHUNKER_CHUNK_ID_BYTES])
{
    if (out_id == nullptr) return;

    uint64_t h[8];
    std::memcpy(h, SHA512_256_IV, sizeof(h));

    size_t full_blocks = len / 128;
    size_t remainder   = len % 128;

    if (data != nullptr) {
        for (size_t i = 0; i < full_blocks; ++i) {
            sha512_compress(h, data + i * 128);
        }
    }

    uint8_t tail[256];
    std::memset(tail, 0, sizeof(tail));
    if (remainder > 0 && data != nullptr) {
        std::memcpy(tail, data + full_blocks * 128, remainder);
    }
    tail[remainder] = 0x80;

    size_t pad_len = (remainder < 112) ? 128 : 256;

    uint64_t bit_len = uint64_t(len) * 8;
    for (int i = 0; i < 8; ++i) {
        tail[pad_len - 1 - i] = uint8_t(bit_len >> (i * 8));
    }

    sha512_compress(h, tail);
    if (pad_len == 256) {
        sha512_compress(h, tail + 128);
    }

    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 8; ++j) {
            out_id[i * 8 + j] = uint8_t(h[i] >> ((7 - j) * 8));
        }
    }
}

extern "C" IDTX_CORE_API void idtx_chunker_build_chunk_url(
    const char*   store_url,
    const uint8_t id[IDTX_CHUNKER_CHUNK_ID_BYTES],
    char*         out,
    size_t        cap)
{
    if (store_url == nullptr || id == nullptr || out == nullptr || cap == 0) {
        if (out != nullptr && cap > 0) out[0] = '\0';
        return;
    }

    std::string base(store_url);
    while (!base.empty() && base.back() == '/') base.pop_back();

    char hex[65];
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < IDTX_CHUNKER_CHUNK_ID_BYTES; ++i) {
        hex[i * 2 + 0] = digits[(id[i] >> 4) & 0xf];
        hex[i * 2 + 1] = digits[ id[i]       & 0xf];
    }
    hex[64] = '\0';

    int written = std::snprintf(out, cap, "%s/%c%c%c%c/%s.cacnk",
                                base.c_str(),
                                hex[0], hex[1], hex[2], hex[3],
                                hex);
    if (written < 0 || size_t(written) >= cap) {
        out[cap - 1] = '\0';
    }
}

// ---------------------------------------------------------------------
// Buzhash CDC. 48-byte window, table from desync's chunker.go
// (identical to AriaStorage.Chunks.RollingHash @hash_table). The
// discriminator formula matches desync: discriminator = avg / (avg *
// -1.42888852e-7 + 1.33237515), and a cut happens whenever the rolling
// hash modulo the discriminator equals discriminator-1.
// ---------------------------------------------------------------------

namespace {

constexpr int kBuzWindow = 48;

constexpr uint32_t kBuzTable[256] = {
    0x458be752, 0xc10748cc, 0xfbbcdbb8, 0x6ded5b68, 0xb10a82b5, 0x20d75648,
    0xdfc5665f, 0xa8428801, 0x7ebf5191, 0x841135c7, 0x65cc53b3, 0x280a597c,
    0x16f60255, 0xc78cbc3e, 0x294415f5, 0xb938d494, 0xec85c4e6, 0xb7d33edc,
    0xe549b544, 0xfdeda5aa, 0x882bf287, 0x3116737c, 0x05569956, 0xe8cc1f68,
    0x0806ac5e, 0x22a14443, 0x15297e10, 0x50d090e7, 0x4ba60f6f, 0xefd9f1a7,
    0x5c5c885c, 0x82482f93, 0x9bfd7c64, 0x0b3e7276, 0xf2688e77, 0x8fad8abc,
    0xb0509568, 0xf1ada29f, 0xa53efdfe, 0xcb2b1d00, 0xf2a9e986, 0x6463432b,
    0x95094051, 0x5a223ad2, 0x9be8401b, 0x61e579cb, 0x1a556a14, 0x5840fdc2,
    0x9261ddf6, 0xcde002bb, 0x52432bb0, 0xbf17373e, 0x7b7c222f, 0x2955ed16,
    0x9f10ca59, 0xe840c4c9, 0xccabd806, 0x14543f34, 0x1462417a, 0x0d4a1f9c,
    0x087ed925, 0xd7f8f24c, 0x7338c425, 0xcf86c8f5, 0xb19165cd, 0x9891c393,
    0x325384ac, 0x0308459d, 0x86141d7e, 0xc922116a, 0xe2ffa6b6, 0x53f52aed,
    0x2cd86197, 0xf5b9f498, 0xbf319c8f, 0xe0411fae, 0x977eb18c, 0xd8770976,
    0x9833466a, 0xc674df7f, 0x8c297d45, 0x8ca48d26, 0xc49ed8e2, 0x7344f874,
    0x556f79c7, 0x6b25eaed, 0xa03e2b42, 0xf68f66a4, 0x8e8b09a2, 0xf2e0e62a,
    0x0d3a9806, 0x9729e493, 0x8c72b0fc, 0x160b94f6, 0x450e4d3d, 0x7a320e85,
    0xbef8f0e1, 0x21d73653, 0x4e3d977a, 0x1e7b3929, 0x1cc6c719, 0xbe478d53,
    0x8d752809, 0xe6d8c2c6, 0x275f0892, 0xc8acc273, 0x4cc21580, 0xecc4a617,
    0xf5f7be70, 0xe795248a, 0x375a2fe9, 0x425570b6, 0x8898dcf8, 0xdc2d97c4,
    0x0106114b, 0x364dc22f, 0x1e0cad1f, 0xbe63803c, 0x5f69fac2, 0x4d5afa6f,
    0x1bc0dfb5, 0xfb273589, 0x0ea47f7b, 0x3c1c2b50, 0x21b2a932, 0x6b1223fd,
    0x2fe706a8, 0xf9bd6ce2, 0xa268e64e, 0xe987f486, 0x3eacf563, 0x1ca2018c,
    0x65e18228, 0x2207360a, 0x57cf1715, 0x34c37d2b, 0x1f8f3cde, 0x93b657cf,
    0x31a019fd, 0xe69eb729, 0x8bca7b9b, 0x4c9d5bed, 0x277ebeaf, 0xe0d8f8ae,
    0xd150821c, 0x31381871, 0xafc3f1b0, 0x927db328, 0xe95effac, 0x305a47bd,
    0x426ba35b, 0x1233af3f, 0x686a5b83, 0x50e072e5, 0xd9d3bb2a, 0x8befc475,
    0x487f0de6, 0xc88dff89, 0xbd664d5e, 0x971b5d18, 0x63b14847, 0xd7d3c1ce,
    0x7f583cf3, 0x72cbcb09, 0xc0d0a81c, 0x7fa3429b, 0xe9158a1b, 0x225ea19a,
    0xd8ca9ea3, 0xc763b282, 0xbb0c6341, 0x020b8293, 0xd4cd299d, 0x58cfa7f8,
    0x91b4ee53, 0x37e4d140, 0x95ec764c, 0x30f76b06, 0x5ee68d24, 0x679c8661,
    0xa41979c2, 0xf2b61284, 0x4fac1475, 0x0adb49f9, 0x19727a23, 0x15a7e374,
    0xc43a18d5, 0x3fb1aa73, 0x342fc615, 0x924c0793, 0xbee2d7f0, 0x8a279de9,
    0x4aa2d70c, 0xe24dd37f, 0xbe862c0b, 0x177c22c2, 0x5388e5ee, 0xcd8a7510,
    0xf901b4fd, 0xdbc13dbc, 0x6c0bae5b, 0x64efe8c7, 0x48b02079, 0x80331a49,
    0xca3d8ae6, 0xf3546190, 0xfed7108b, 0xc49b941b, 0x32baf4a9, 0xeb833a4a,
    0x88a3f1a5, 0x3a91ce0a, 0x3cc27da1, 0x7112e684, 0x4a3096b1, 0x3794574c,
    0xa3c8b6f3, 0x1d213941, 0x6e0a2e00, 0x233479f1, 0x0f4cd82f, 0x6093edd2,
    0x5d7d209e, 0x464fe319, 0xd4dcac9e, 0x0db845cb, 0xfb5e4bc3, 0xe0256ce1,
    0x09fb4ed1, 0x0914be1e, 0xa5bdb2c3, 0xc6eb57bb, 0x30320350, 0x3f397e91,
    0xa67791bc, 0x86bc0e2c, 0xefa0a7e2, 0xe9ff7543, 0xe733612c, 0xd185897b,
    0x329e5388, 0x91dd236b, 0x2ecb0d93, 0xf4d82a3d, 0x35b5c03f, 0xe4e606f0,
    0x05b21843, 0x37b45964, 0x5eff22f4, 0x6027f4cc, 0x77178b3c, 0xae507131,
    0x7bf7cabc, 0xf9c18d66, 0x593ade65, 0xd95ddf11,
};

inline uint32_t rol32(uint32_t v, unsigned s) {
    s &= 31;
    return (v << s) | (v >> (32 - s));
}

struct ChunkRecord {
    uint64_t offset;
    uint64_t size;
    uint8_t  id[IDTX_CHUNKER_CHUNK_ID_BYTES];
};

} // namespace

struct idtx_chunklist {
    std::vector<ChunkRecord> chunks;
    uint64_t                 size_min;
    uint64_t                 size_avg;
    uint64_t                 size_max;
};

extern "C" IDTX_CORE_API int32_t idtx_chunker_cdc(
    const uint8_t*     data,
    size_t             len,
    size_t             min_size,
    size_t             avg_size,
    size_t             max_size,
    idtx_chunklist_t** out_chunks)
{
    if (out_chunks == nullptr) return 1;
    *out_chunks = nullptr;
    if (data == nullptr && len > 0) return 1;

    if (min_size == 0 && avg_size == 0 && max_size == 0) {
        min_size = IDTX_CHUNKER_DEFAULT_MIN;
        avg_size = IDTX_CHUNKER_DEFAULT_AVG;
        max_size = IDTX_CHUNKER_DEFAULT_MAX;
    }
    if (min_size == 0 || avg_size < min_size || max_size < avg_size) return 1;

    // Discriminator: matches desync's chunker.go.
    const double denom = -1.42888852e-7 * double(avg_size) + 1.33237515;
    const uint32_t discriminator = uint32_t(double(avg_size) / denom);

    auto cl = new idtx_chunklist();
    cl->size_min = min_size;
    cl->size_avg = avg_size;
    cl->size_max = max_size;

    uint32_t h = 0;
    uint8_t window[kBuzWindow];
    std::memset(window, 0, sizeof(window));
    int widx = 0;

    size_t chunk_start = 0;
    for (size_t i = 0; i < len; ++i) {
        const uint8_t out_byte = window[widx];
        const uint8_t in_byte  = data[i];
        window[widx] = in_byte;
        widx = (widx + 1) % kBuzWindow;
        h = rol32(h, 1) ^ rol32(kBuzTable[out_byte], kBuzWindow) ^ kBuzTable[in_byte];

        const size_t chunk_len = i - chunk_start + 1;
        if (chunk_len < min_size) continue;

        const bool cut_size = chunk_len >= max_size;
        const bool cut_hash = (discriminator != 0)
                              && (h % discriminator == discriminator - 1);
        if (cut_size || cut_hash) {
            ChunkRecord rec;
            rec.offset = uint64_t(chunk_start);
            rec.size   = uint64_t(chunk_len);
            idtx_chunker_sha512_256(data + chunk_start, chunk_len, rec.id);
            cl->chunks.push_back(rec);

            chunk_start = i + 1;
            h = 0;
            std::memset(window, 0, sizeof(window));
            widx = 0;
        }
    }
    if (chunk_start < len) {
        ChunkRecord rec;
        rec.offset = uint64_t(chunk_start);
        rec.size   = uint64_t(len - chunk_start);
        idtx_chunker_sha512_256(data + chunk_start, rec.size, rec.id);
        cl->chunks.push_back(rec);
    }

    *out_chunks = cl;
    return 0;
}

extern "C" IDTX_CORE_API int32_t idtx_chunklist_count(const idtx_chunklist_t* cl)
{
    return cl ? int32_t(cl->chunks.size()) : 0;
}

extern "C" IDTX_CORE_API int32_t idtx_chunklist_get(
    const idtx_chunklist_t* cl,
    int32_t                 index,
    uint64_t*               out_offset,
    uint64_t*               out_size,
    uint8_t                 out_id[IDTX_CHUNKER_CHUNK_ID_BYTES])
{
    if (cl == nullptr || index < 0 || size_t(index) >= cl->chunks.size()) return 1;
    const auto& r = cl->chunks[index];
    if (out_offset) *out_offset = r.offset;
    if (out_size)   *out_size   = r.size;
    if (out_id)     std::memcpy(out_id, r.id, IDTX_CHUNKER_CHUNK_ID_BYTES);
    return 0;
}

extern "C" IDTX_CORE_API void idtx_chunklist_destroy(idtx_chunklist_t* cl)
{
    delete cl;
}

// ---------------------------------------------------------------------
// .caibx parse + emit.
// ---------------------------------------------------------------------

struct idtx_caibx {
    std::vector<ChunkRecord> chunks;
    uint64_t                 size_min;
    uint64_t                 size_avg;
    uint64_t                 size_max;
};

namespace {

constexpr uint64_t CA_FORMAT_INDEX_SIZE = 48;
constexpr uint64_t CA_FORMAT_SHA512_256 = 0xA000000000000000ULL;
constexpr uint64_t CA_MAX_UINT64        = 0xFFFFFFFFFFFFFFFFULL;

struct LECursor {
    const uint8_t* data;
    size_t         size;
    size_t         pos;
    bool read_u64(uint64_t& out) {
        if (pos + 8 > size) return false;
        out = 0;
        for (int i = 0; i < 8; ++i) {
            out |= uint64_t(data[pos + i]) << (i * 8);
        }
        pos += 8;
        return true;
    }
    bool read_bytes(uint8_t* dst, size_t n) {
        if (pos + n > size) return false;
        std::memcpy(dst, data + pos, n);
        pos += n;
        return true;
    }
};

} // namespace

extern "C" IDTX_CORE_API int32_t idtx_chunker_parse_caibx(
    const uint8_t*  data,
    size_t          len,
    idtx_caibx_t**  out_index)
{
    if (out_index == nullptr) return 1;
    *out_index = nullptr;
    if (data == nullptr) return 1;

    LECursor cur{ data, len, 0 };

    uint64_t index_size = 0, index_type = 0;
    if (!cur.read_u64(index_size) || !cur.read_u64(index_type)) return 2;
    if (index_size != CA_FORMAT_INDEX_SIZE) return 2;
    if (index_type != IDTX_CAIBX_FORMAT_INDEX_TYPE) return 2;

    uint64_t feature_flags = 0, smin = 0, savg = 0, smax = 0;
    if (!cur.read_u64(feature_flags) || !cur.read_u64(smin) ||
        !cur.read_u64(savg) || !cur.read_u64(smax)) return 2;
    if ((feature_flags & CA_FORMAT_SHA512_256) == 0) return 3;

    uint64_t table_size = 0, table_type = 0;
    if (!cur.read_u64(table_size) || !cur.read_u64(table_type)) return 2;
    if (table_size != CA_MAX_UINT64) return 2;
    if (table_type != IDTX_CAIBX_FORMAT_TABLE_TYPE) return 2;

    auto idx = new idtx_caibx();
    idx->size_min = smin;
    idx->size_avg = savg;
    idx->size_max = smax;

    uint64_t last_offset = 0;
    for (;;) {
        uint64_t offset = 0;
        if (!cur.read_u64(offset)) { delete idx; return 2; }
        if (offset == 0) break;
        ChunkRecord rec;
        if (!cur.read_bytes(rec.id, IDTX_CHUNKER_CHUNK_ID_BYTES)) { delete idx; return 2; }
        if (offset < last_offset) { delete idx; return 2; }
        rec.offset = last_offset;
        rec.size   = offset - last_offset;
        if (rec.size > smax) { delete idx; return 2; }
        idx->chunks.push_back(rec);
        last_offset = offset;
    }

    uint64_t zero_fill = 0, index_offset = 0, tail_table_size = 0, tail_marker = 0;
    if (!cur.read_u64(zero_fill) || !cur.read_u64(index_offset) ||
        !cur.read_u64(tail_table_size) || !cur.read_u64(tail_marker)) {
        delete idx;
        return 2;
    }
    if (zero_fill != 0) { delete idx; return 2; }
    if (tail_marker != IDTX_CAIBX_TAIL_MARKER) { delete idx; return 2; }

    *out_index = idx;
    return 0;
}

extern "C" IDTX_CORE_API int32_t idtx_caibx_chunk_count(const idtx_caibx_t* idx)
{
    return idx ? int32_t(idx->chunks.size()) : 0;
}

extern "C" IDTX_CORE_API int32_t idtx_caibx_get_chunk(
    const idtx_caibx_t* idx,
    int32_t             index,
    uint64_t*           out_offset,
    uint64_t*           out_size,
    uint8_t             out_id[IDTX_CHUNKER_CHUNK_ID_BYTES])
{
    if (idx == nullptr || index < 0 || size_t(index) >= idx->chunks.size()) return 1;
    const auto& r = idx->chunks[index];
    if (out_offset) *out_offset = r.offset;
    if (out_size)   *out_size   = r.size;
    if (out_id)     std::memcpy(out_id, r.id, IDTX_CHUNKER_CHUNK_ID_BYTES);
    return 0;
}

extern "C" IDTX_CORE_API void idtx_caibx_destroy(idtx_caibx_t* idx)
{
    delete idx;
}

// ---------------------------------------------------------------------
// idtx_buffer.
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

extern "C" IDTX_CORE_API const uint8_t* idtx_buffer_data(const idtx_buffer_t* buf)
{
    return buf != nullptr ? buf->data : nullptr;
}

extern "C" IDTX_CORE_API size_t idtx_buffer_size(const idtx_buffer_t* buf)
{
    return buf != nullptr ? buf->size : 0;
}

extern "C" IDTX_CORE_API void idtx_buffer_destroy(idtx_buffer_t* buf)
{
    if (buf == nullptr) return;
    delete[] buf->data;
    delete buf;
}

// ---------------------------------------------------------------------
// caibx emit. Wire format identical to FabricMMOGAsset::upload_asset's
// emit section (commit 98248e0251 of multiplayer-fabric-godot).
// ---------------------------------------------------------------------

extern "C" IDTX_CORE_API int32_t idtx_chunker_emit_caibx(
    const idtx_chunklist_t* cl,
    size_t                  chunk_size_min,
    size_t                  chunk_size_avg,
    size_t                  chunk_size_max,
    idtx_buffer_t**         out_caibx)
{
    if (out_caibx == nullptr) return 1;
    *out_caibx = nullptr;
    if (cl == nullptr) return 1;

    if (chunk_size_min == 0 && chunk_size_avg == 0 && chunk_size_max == 0) {
        chunk_size_min = cl->size_min;
        chunk_size_avg = cl->size_avg;
        chunk_size_max = cl->size_max;
    }

    std::vector<uint8_t> idx;
    auto push_u64 = [&](uint64_t v) {
        for (int b = 0; b < 8; ++b) idx.push_back(uint8_t((v >> (b * 8)) & 0xFF));
    };

    // FormatIndex header (48 bytes).
    push_u64(CA_FORMAT_INDEX_SIZE);
    push_u64(IDTX_CAIBX_FORMAT_INDEX_TYPE);
    push_u64(CA_FORMAT_SHA512_256);
    push_u64(uint64_t(chunk_size_min));
    push_u64(uint64_t(chunk_size_avg));
    push_u64(uint64_t(chunk_size_max));

    // FormatTable header.
    push_u64(CA_MAX_UINT64);
    push_u64(IDTX_CAIBX_FORMAT_TABLE_TYPE);

    // Chunk items: each item is [end_offset u64][chunk_id 32B].
    // end_offset is cumulative.
    uint64_t cumulative = 0;
    size_t   table_item_bytes = 0;
    for (const auto& c : cl->chunks) {
        cumulative += c.size;
        push_u64(cumulative);
        for (int b = 0; b < IDTX_CHUNKER_CHUNK_ID_BYTES; ++b) idx.push_back(c.id[b]);
        table_item_bytes += 8 + IDTX_CHUNKER_CHUNK_ID_BYTES;
    }

    // Zero-offset terminator.
    push_u64(0ULL);

    // Tail.
    push_u64(0ULL);                                 // zero fill
    push_u64(CA_FORMAT_INDEX_SIZE);                 // index offset
    push_u64(uint64_t(16 + table_item_bytes + 40)); // table size (16-byte header + items + 40-byte tail)
    push_u64(IDTX_CAIBX_TAIL_MARKER);

    *out_caibx = make_buffer(idx);
    return 0;
}

// ---------------------------------------------------------------------
// .cacnk zstd compress + verify.
//
// Reuses the system zstd via <zstd.h> + linked `zstd` lib (LIBS entry
// in scons/idtxcore.py). Default compression level matches desync
// (level 3) — picks a chunk-by-chunk trade-off that keeps re-encoding
// cost low while staying within ~5% of zstd-19 ratios for typical
// asset payloads.
// ---------------------------------------------------------------------

#include <zstd.h>

extern "C" IDTX_CORE_API int32_t idtx_chunker_compress(
    const uint8_t*  plain,
    size_t          plain_len,
    idtx_buffer_t** out_cacnk)
{
    if (out_cacnk == nullptr) return 1;
    *out_cacnk = nullptr;
    if (plain == nullptr && plain_len > 0) return 1;

    const size_t bound = ZSTD_compressBound(plain_len);
    std::vector<uint8_t> buf(bound);
    const size_t written = ZSTD_compress(
        buf.data(), buf.size(),
        plain,      plain_len,
        3 /* desync default level */);
    if (ZSTD_isError(written)) return 2;
    buf.resize(written);

    *out_cacnk = make_buffer(buf);
    return 0;
}

extern "C" IDTX_CORE_API int32_t idtx_chunker_decompress_and_verify(
    const uint8_t*  cacnk_zstd,
    size_t          cacnk_len,
    const uint8_t   expected_id[IDTX_CHUNKER_CHUNK_ID_BYTES],
    idtx_buffer_t** out_plain)
{
    if (out_plain == nullptr) return 1;
    *out_plain = nullptr;
    if (cacnk_zstd == nullptr || expected_id == nullptr) return 1;

    // Frame-content-size lookup first so we can allocate exactly.
    const unsigned long long fcs = ZSTD_getFrameContentSize(cacnk_zstd, cacnk_len);
    if (fcs == ZSTD_CONTENTSIZE_ERROR || fcs == ZSTD_CONTENTSIZE_UNKNOWN) {
        // Streaming decompress fallback. Bound the output by 16x the
        // input size — chunks are by spec <= 256K decompressed.
        return 2;
    }
    if (fcs > (256ULL * 1024ULL * 16ULL)) return 2;  // sanity

    // static_cast (not size_t(fcs)): `std::vector<uint8_t> plain(size_t(fcs))`
    // is the most vexing parse — size_t(fcs) reads as a parameter
    // declaration, making `plain` a FUNCTION returning vector<uint8_t>
    // rather than a sized vector. static_cast forces an expression.
    std::vector<uint8_t> plain(static_cast<size_t>(fcs));
    const size_t written = ZSTD_decompress(
        plain.data(), plain.size(),
        cacnk_zstd,   cacnk_len);
    if (ZSTD_isError(written)) return 2;
    if (written != fcs)        return 2;

    // Verify SHA-512/256 matches the expected chunk-id.
    uint8_t actual_id[IDTX_CHUNKER_CHUNK_ID_BYTES];
    idtx_chunker_sha512_256(plain.data(), plain.size(), actual_id);
    if (std::memcmp(actual_id, expected_id, IDTX_CHUNKER_CHUNK_ID_BYTES) != 0) {
        return 3;  // tag mismatch — corrupted in transit or wrong chunk
    }

    *out_plain = make_buffer(plain);
    return 0;
}
