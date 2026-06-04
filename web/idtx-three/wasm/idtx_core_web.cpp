// Emscripten glue for the idtx-flow web spoke (CHI-312).
//
// Exposes libidtx_core's USD *reader* to JavaScript: the browser writes a .usda
// into Emscripten's MEMFS, calls idtxweb_usd_to_json(path), and gets back a JSON
// string of geometry (one entry per mesh: name, flat vertex positions, indices)
// — the same data the Godot/Blender/Unity spokes pull through this core. The web
// app (React-Three-Fiber) turns it into THREE.BufferGeometry.
//
// This is the SAME core every other host dlopens; here it is statically linked
// into the WASM module (libidtx_core + its OpenUSD), so there is no .sigs
// delay-load on the web — the whole core is the wasm binary.
//
// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

#include <emscripten.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "idtx_core/idtx_core.h"

extern "C" {

// Returns a malloc'd JSON C-string; JS reads it with UTF8ToString and must
// free it via idtxweb_free. Shape:
//   {"name":"...","parts":[{"name":"Body","verts":[...],"indices":[...]}]}
EMSCRIPTEN_KEEPALIVE
char* idtxweb_usd_to_json(const char* usd_path) {
	idtx_core_init(nullptr);
	idtx_avatar_t* av = idtx_core_import_avatar_from_usd(usd_path);
	if (av == nullptr) {
		char* err = static_cast<char*>(std::malloc(32));
		std::strcpy(err, "{\"error\":\"import failed\"}");
		return err;
	}

	std::string out = "{\"name\":\"";
	const char* name = idtx_avatar_get_name ? idtx_avatar_get_name(av) : "avatar";
	out += (name ? name : "avatar");
	out += "\",\"parts\":[";

	const int mesh_count = idtx_avatar_get_mesh_count(av);
	for (int i = 0; i < mesh_count; ++i) {
		idtx_mesh_t* mesh = idtx_avatar_get_mesh(av, i);
		if (mesh == nullptr) continue;
		const int vc = idtx_mesh_get_vertex_count(mesh);
		const int ic = idtx_mesh_get_index_count(mesh);

		std::vector<float> pos(static_cast<size_t>(vc) * 3);
		std::vector<int32_t> idx(static_cast<size_t>(ic));
		idtx_mesh_get_positions(mesh, pos.data());
		idtx_mesh_get_indices(mesh, idx.data());

		if (i) out += ",";
		out += "{\"name\":\"";
		const char* mn = idtx_mesh_get_name(mesh);
		out += (mn ? mn : "mesh");
		out += "\",\"verts\":[";
		char buf[40];
		for (size_t v = 0; v < pos.size(); ++v) {
			std::snprintf(buf, sizeof(buf), "%g", pos[v]);
			out += buf;
			if (v + 1 < pos.size()) out += ",";
		}
		out += "],\"indices\":[";
		for (size_t k = 0; k < idx.size(); ++k) {
			std::snprintf(buf, sizeof(buf), "%d", idx[k]);
			out += buf;
			if (k + 1 < idx.size()) out += ",";
		}
		out += "]}";
	}
	out += "]}";

	idtx_avatar_destroy(av);

	char* result = static_cast<char*>(std::malloc(out.size() + 1));
	std::memcpy(result, out.c_str(), out.size() + 1);
	return result;
}

EMSCRIPTEN_KEEPALIVE
void idtxweb_free(char* p) { std::free(p); }

EMSCRIPTEN_KEEPALIVE
const char* idtxweb_version() { return idtx_core_version(); }

}  // extern "C"
