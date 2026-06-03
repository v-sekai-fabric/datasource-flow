// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// idtxcli — minimal command-line driver for libidtx_core. Useful for
// smoke-testing the avatar pipeline without spinning up Godot / Unity.
//
// Subcommands:
//   idtxcli usd-to-vrm <in.usda>  <out.vrm>
//   idtxcli vrm-to-usd <in.vrm>   <out.usda>
//   idtxcli usd-to-usd <in.usda>  <out.usda>     (round-trip)
//   idtxcli vrm-to-vrm <in.vrm>   <out.vrm>      (round-trip)
//   idtxcli reconstruct-quads <in.usda> <out.usda> [planarity_deg]
//   idtxcli bake   <in.usda>  --aria <url> [--auth $TOKEN]    (USD -> caibx -> aria)
//   idtxcli fetch  <caibx-url> --output <out.bin> [--aria <url>] [--auth $TOKEN]
//   idtxcli verify <caibx-url> [--aria <url>] [--auth $TOKEN] (HEAD-only check)
//   idtxcli version
//
// Returns 0 on success, non-zero on failure.

#include "idtx_core/idtx_core.h"
#include "idtx_core/idtx_chunker.h"
#include "idtx_core/idtx_transport.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static int usage(const char* arg0)
{
    std::fprintf(stderr,
        "idtxcli — libidtx_core driver\n"
        "Usage:\n"
        "  %s usd-to-vrm <in.usda> <out.vrm>\n"
        "  %s vrm-to-usd <in.vrm>  <out.usda>\n"
        "  %s usd-to-usd <in.usda> <out.usda>   (round-trip)\n"
        "  %s vrm-to-vrm <in.vrm>  <out.vrm>    (round-trip)\n"
        "  %s usd-export <in.usda> <out.usda> --mode <flat|overlay|layer-only|flatten> [--source <src.usda>]\n"
        "  %s reconstruct-quads <in.usda> <out.usda> [planarity_deg]\n"
        "  %s bake   <in.bin>     --aria <url> [--auth TOKEN]\n"
        "  %s fetch  <caibx-url>  --output <out.bin> [--aria <url>] [--auth TOKEN]\n"
        "  %s verify <caibx-url>  [--aria <url>] [--auth TOKEN]\n"
        "  %s version\n",
        arg0, arg0, arg0, arg0, arg0, arg0, arg0, arg0, arg0, arg0);
    return 2;
}

// ---------------------------------------------------------------------
// Aria-storage subcommand helpers (bake / fetch / verify).
// ---------------------------------------------------------------------

namespace {

struct CdnArgs {
    const char* aria  = nullptr;
    const char* auth  = nullptr;
    const char* out   = nullptr;
};

CdnArgs parse_cdn_flags(int argc, char** argv, int start)
{
    CdnArgs a;
    for (int i = start; i + 1 < argc; i += 2) {
        if (std::strcmp(argv[i], "--aria") == 0)        a.aria = argv[i + 1];
        else if (std::strcmp(argv[i], "--auth") == 0)   a.auth = argv[i + 1];
        else if (std::strcmp(argv[i], "--output") == 0) a.out  = argv[i + 1];
    }
    return a;
}

bool read_file(const char* path, std::vector<uint8_t>& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    out.resize(size_t(f.tellg()));
    f.seekg(0, std::ios::beg);
    if (!out.empty()) f.read(reinterpret_cast<char*>(out.data()), out.size());
    return bool(f);
}

bool write_file(const char* path, const uint8_t* data, size_t len)
{
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    if (len > 0) f.write(reinterpret_cast<const char*>(data), len);
    return bool(f);
}

idtx_transport_t* open_transport(const CdnArgs& cdn)
{
    if (cdn.aria == nullptr) {
        std::fprintf(stderr, "missing --aria <url>\n");
        return nullptr;
    }
    idtx_transport_t* t = idtx_transport_new(cdn.aria);
    if (t == nullptr) {
        std::fprintf(stderr, "idtx_transport_new failed for %s\n", cdn.aria);
        return nullptr;
    }
    if (cdn.auth != nullptr) idtx_transport_set_auth(t, cdn.auth);
    return t;
}

int cmd_bake(int argc, char** argv)
{
    if (argc < 5) { std::fprintf(stderr, "bake: need <in.bin> --aria <url>\n"); return 2; }
    const char* in_path = argv[2];
    CdnArgs cdn = parse_cdn_flags(argc, argv, 3);

    std::vector<uint8_t> blob;
    if (!read_file(in_path, blob)) {
        std::fprintf(stderr, "bake: cannot read %s\n", in_path);
        return 3;
    }

    idtx_transport_t* t = open_transport(cdn);
    if (t == nullptr) return 4;

    // Derive an index name from the input filename stem.
    std::string name = in_path;
    auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    auto dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    if (name.empty()) name = "asset";

    char url[1024] = {0};
    int32_t rc = idtx_chunker_upload_asset(t, name.c_str(),
                                           blob.data(), blob.size(),
                                           url, sizeof(url));
    if (rc != 0) {
        std::fprintf(stderr, "bake: upload failed rc=%d status=%d err=%s\n",
                     rc, idtx_transport_last_status(t),
                     idtx_transport_last_error(t));
        idtx_transport_destroy(t);
        return 5;
    }
    std::fprintf(stdout, "%s\n", url);
    idtx_transport_destroy(t);
    return 0;
}

int cmd_fetch(int argc, char** argv)
{
    if (argc < 5) { std::fprintf(stderr, "fetch: need <caibx-url> --output <out> [--aria <url>]\n"); return 2; }
    const char* caibx_url = argv[2];
    CdnArgs cdn = parse_cdn_flags(argc, argv, 3);
    if (cdn.out == nullptr) { std::fprintf(stderr, "fetch: --output required\n"); return 2; }

    // If --aria not given, derive base from the caibx URL prefix.
    std::string derived_base;
    if (cdn.aria == nullptr) {
        std::string u(caibx_url);
        auto slash = u.find("/index/");
        if (slash != std::string::npos) {
            derived_base = u.substr(0, slash);
            cdn.aria = derived_base.c_str();
        } else {
            std::fprintf(stderr, "fetch: cannot derive --aria from %s; pass --aria explicitly\n", caibx_url);
            return 2;
        }
    }

    idtx_transport_t* t = open_transport(cdn);
    if (t == nullptr) return 4;

    idtx_buffer_t* caibx_buf = nullptr;
    int32_t rc = idtx_transport_get_caibx(t, caibx_url, &caibx_buf);
    if (rc != 0) {
        std::fprintf(stderr, "fetch: GET caibx failed rc=%d status=%d\n",
                     rc, idtx_transport_last_status(t));
        idtx_transport_destroy(t);
        return 5;
    }

    idtx_caibx_t* idx = nullptr;
    rc = idtx_chunker_parse_caibx(idtx_buffer_data(caibx_buf),
                                  idtx_buffer_size(caibx_buf), &idx);
    idtx_buffer_destroy(caibx_buf);
    if (rc != 0) {
        std::fprintf(stderr, "fetch: caibx parse failed rc=%d\n", rc);
        idtx_transport_destroy(t);
        return 6;
    }

    idtx_buffer_t* blob = nullptr;
    rc = idtx_chunker_assemble(t, idx, &blob);
    idtx_caibx_destroy(idx);
    if (rc != 0) {
        std::fprintf(stderr, "fetch: assemble failed rc=%d\n", rc);
        idtx_transport_destroy(t);
        return 7;
    }

    bool ok = write_file(cdn.out, idtx_buffer_data(blob), idtx_buffer_size(blob));
    size_t n = idtx_buffer_size(blob);
    idtx_buffer_destroy(blob);
    idtx_transport_destroy(t);

    if (!ok) { std::fprintf(stderr, "fetch: write %s failed\n", cdn.out); return 8; }
    std::fprintf(stdout, "fetch: %s -> %s ok (%zu bytes)\n", caibx_url, cdn.out, n);
    return 0;
}

int cmd_verify(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "verify: need <caibx-url> [--aria <url>]\n"); return 2; }
    const char* caibx_url = argv[2];
    CdnArgs cdn = parse_cdn_flags(argc, argv, 3);

    std::string derived_base;
    if (cdn.aria == nullptr) {
        std::string u(caibx_url);
        auto slash = u.find("/index/");
        if (slash != std::string::npos) {
            derived_base = u.substr(0, slash);
            cdn.aria = derived_base.c_str();
        } else {
            std::fprintf(stderr, "verify: cannot derive --aria from %s\n", caibx_url);
            return 2;
        }
    }

    idtx_transport_t* t = open_transport(cdn);
    if (t == nullptr) return 4;

    idtx_buffer_t* caibx_buf = nullptr;
    int32_t rc = idtx_transport_get_caibx(t, caibx_url, &caibx_buf);
    if (rc != 0) {
        std::fprintf(stderr, "verify: GET caibx failed rc=%d status=%d\n",
                     rc, idtx_transport_last_status(t));
        idtx_transport_destroy(t);
        return 5;
    }

    idtx_caibx_t* idx = nullptr;
    rc = idtx_chunker_parse_caibx(idtx_buffer_data(caibx_buf),
                                  idtx_buffer_size(caibx_buf), &idx);
    idtx_buffer_destroy(caibx_buf);
    if (rc != 0) {
        std::fprintf(stderr, "verify: caibx parse failed rc=%d\n", rc);
        idtx_transport_destroy(t);
        return 6;
    }

    const int32_t n = idtx_caibx_chunk_count(idx);
    int32_t missing = 0;
    for (int32_t i = 0; i < n; ++i) {
        uint8_t id[IDTX_CHUNKER_CHUNK_ID_BYTES];
        idtx_caibx_get_chunk(idx, i, nullptr, nullptr, id);
        if (idtx_transport_head_chunk(t, id) != 0) ++missing;
    }
    idtx_caibx_destroy(idx);
    idtx_transport_destroy(t);

    std::fprintf(stdout, "verify: %d/%d chunks present (%d missing)\n",
                 n - missing, n, missing);
    return missing == 0 ? 0 : 9;
}

} // namespace

static int run_convert(
    idtx_avatar_t* (*import_fn)(const char*),
    int32_t (*export_fn)(const idtx_avatar_t*, const char*),
    const char* in_path,
    const char* out_path,
    const char* op_name)
{
    idtx_avatar_t* a = import_fn(in_path);
    if (a == nullptr) {
        std::fprintf(stderr, "%s: failed to import %s\n", op_name, in_path);
        return 3;
    }
    int32_t rc = export_fn(a, out_path);
    idtx_avatar_destroy(a);
    if (rc != 0) {
        std::fprintf(stderr, "%s: export to %s failed (rc=%d)\n", op_name, out_path, rc);
        return 4;
    }
    std::fprintf(stdout, "%s: %s -> %s ok\n", op_name, in_path, out_path);
    return 0;
}

// usd-export — exercise the layer-aware exporter idtx_core_export_avatar_to_usd_ex.
// Imports <in.usda> (which stamps the avatar's source-USD provenance to
// <in.usda>), then re-exports under the chosen mode. --source overrides
// the stage the deltas are layered against; without it the import
// provenance is used, so `usd-export in.usda out.usda --mode flatten`
// round-trips in.usda through the flattening path.
static int cmd_usd_export(int argc, char** argv)
{
    const char* in_path  = argv[2];
    const char* out_path = argv[3];

    idtx_usd_export_opts_t opts;
    idtx_usd_export_opts_init(&opts);
    const char* mode_str = "flat";

    for (int i = 4; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--mode" && i + 1 < argc) {
            mode_str = argv[++i];
            if      (std::string(mode_str) == "flat")       opts.mode = IDTX_USD_NEW_FLAT;
            else if (std::string(mode_str) == "overlay")    opts.mode = IDTX_USD_OVERLAY;
            else if (std::string(mode_str) == "layer-only") opts.mode = IDTX_USD_LAYER_ONLY;
            else if (std::string(mode_str) == "flatten")    opts.mode = IDTX_USD_FLATTEN;
            else {
                std::fprintf(stderr, "usd-export: unknown --mode '%s'\n", mode_str);
                return 2;
            }
        } else if (a == "--source" && i + 1 < argc) {
            opts.source_path = argv[++i];
        } else {
            std::fprintf(stderr, "usd-export: unexpected arg '%s'\n", argv[i]);
            return 2;
        }
    }

    idtx_avatar_t* a = idtx_core_import_avatar_from_usd(in_path);
    if (a == nullptr) {
        std::fprintf(stderr, "usd-export: failed to import %s\n", in_path);
        return 3;
    }
    int32_t rc = idtx_core_export_avatar_to_usd_ex(a, out_path, &opts);
    idtx_avatar_destroy(a);
    if (rc != 0) {
        std::fprintf(stderr, "usd-export: export to %s failed (mode=%s, rc=%d)\n",
                     out_path, mode_str, rc);
        return 4;
    }
    std::fprintf(stdout, "usd-export: %s -> %s ok (mode=%s)\n", in_path, out_path, mode_str);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc < 2) return usage(argv[0]);

    std::string cmd = argv[1];
    if (cmd == "version") {
        std::fprintf(stdout, "idtxcli libidtx_core %s\n", idtx_core_version());
        return 0;
    }
    if (argc < 4) return usage(argv[0]);

    if (cmd == "usd-to-vrm") {
        return run_convert(
            &idtx_core_import_avatar_from_usd,
            &idtx_core_export_avatar_to_vrm,
            argv[2], argv[3], "usd-to-vrm");
    }
    if (cmd == "vrm-to-usd") {
        return run_convert(
            &idtx_core_import_avatar_from_vrm,
            &idtx_core_export_avatar_to_usd,
            argv[2], argv[3], "vrm-to-usd");
    }
    if (cmd == "usd-to-usd") {
        return run_convert(
            &idtx_core_import_avatar_from_usd,
            &idtx_core_export_avatar_to_usd,
            argv[2], argv[3], "usd-to-usd");
    }
    if (cmd == "vrm-to-vrm") {
        return run_convert(
            &idtx_core_import_avatar_from_vrm,
            &idtx_core_export_avatar_to_vrm,
            argv[2], argv[3], "vrm-to-vrm");
    }
    if (cmd == "usd-export") return cmd_usd_export(argc, argv);
    if (cmd == "bake")    return cmd_bake(argc, argv);
    if (cmd == "fetch")   return cmd_fetch(argc, argv);
    if (cmd == "verify")  return cmd_verify(argc, argv);
    if (cmd == "reconstruct-quads") {
        // USD -> idtx_avatar -> run tris-to-quads per mesh -> USD.
        float planarity = (argc >= 5) ? std::atof(argv[4]) : 5.0f;
        idtx_avatar_t* a = idtx_core_import_avatar_from_usd(argv[2]);
        if (a == nullptr) {
            std::fprintf(stderr, "reconstruct-quads: failed to import %s\n", argv[2]);
            return 3;
        }
        int32_t total_quads = 0;
        int32_t mesh_count  = idtx_avatar_get_mesh_count(a);
        for (int32_t i = 0; i < mesh_count; ++i) {
            idtx_mesh_t* m = idtx_avatar_get_mesh(a, i);
            int32_t n = idtx_mesh_reconstruct_quads(m, planarity);
            if (n > 0) total_quads += n;
        }
        int32_t rc = idtx_core_export_avatar_to_usd(a, argv[3]);
        idtx_avatar_destroy(a);
        if (rc != 0) {
            std::fprintf(stderr,
                "reconstruct-quads: export to %s failed (rc=%d)\n",
                argv[3], rc);
            return 4;
        }
        std::fprintf(stdout,
            "reconstruct-quads: %s -> %s ok (%d quad(s) formed across %d mesh(es))\n",
            argv[2], argv[3], total_quads, mesh_count);
        return 0;
    }
    return usage(argv[0]);
}
