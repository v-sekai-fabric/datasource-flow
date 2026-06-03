#include "idtx_core_loader.h"

#include <string>

#include <idtxflow/utils/Logger.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dlfcn.h>
#  include "idtx_core_stubs.h"  // generated (core/generated) — POSIX dlsym table
#endif

// Injected by the build (scons/gdextension.py) as e.g.
// "libidtx_core.windows.x86_64". Fallback keeps the file self-contained.
#ifndef IDTX_CORE_LIB_BASENAME
#  define IDTX_CORE_LIB_BASENAME "libidtx_core"
#endif

namespace idtxflow {

namespace {

// Directory containing THIS GDExtension module (where libidtx_core is bundled
// alongside it under addons/IDTXFlow/bin/<platform>/).
std::string this_module_dir() {
#ifdef _WIN32
	char buffer[MAX_PATH] = {0};
	HMODULE hm = nullptr;
	if (!GetModuleHandleExA(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
				GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCSTR>(&this_module_dir), &hm)) {
		return "";
	}
	GetModuleFileNameA(hm, buffer, MAX_PATH);
	std::string path(buffer);
	const auto slash = path.find_last_of("\\/");
	return slash == std::string::npos ? "" : path.substr(0, slash);
#else
	Dl_info info;
	if (dladdr(reinterpret_cast<void*>(&this_module_dir), &info) == 0 ||
			info.dli_fname == nullptr) {
		return "";
	}
	std::string path(info.dli_fname);
	const auto slash = path.find_last_of('/');
	return slash == std::string::npos ? "" : path.substr(0, slash);
#endif
}

bool g_loaded = false;

}  // namespace

bool load_idtx_core() {
	if (g_loaded) {
		return true;
	}

	const std::string dir = this_module_dir();
#ifdef _WIN32
	const std::string path = dir + "\\" IDTX_CORE_LIB_BASENAME ".dll";
	// LOAD_WITH_ALTERED_SEARCH_PATH so the lib's own runtime deps (usd_ms.dll,
	// tbb12.dll, libidtx_usd.dll — all bundled in the same dir) resolve from
	// here. The delay-load thunks then bind to this already-loaded module.
	HMODULE h = LoadLibraryExA(path.c_str(), nullptr,
			LOAD_WITH_ALTERED_SEARCH_PATH);
	g_loaded = (h != nullptr);
	if (!g_loaded) {
		IDTX_LOGF(IDTX_ERROR, "idtx_core: LoadLibraryEx failed for '{}' (GetLastError={})",
				path, static_cast<unsigned long>(GetLastError()));
	}
#else
#  ifdef __APPLE__
	const std::string path = dir + "/" IDTX_CORE_LIB_BASENAME ".dylib";
#  else
	const std::string path = dir + "/" IDTX_CORE_LIB_BASENAME ".so";
#  endif
	core::StubPathMap paths;
	paths[core::kModuleIdtx_core] = {path};
	g_loaded = core::InitializeStubs(paths);
	if (!g_loaded) {
		IDTX_LOGF(IDTX_ERROR, "idtx_core: InitializeStubs failed for '{}'", path);
	}
#endif

	if (g_loaded) {
		IDTX_LOGF(IDTX_INFO, "idtx_core: loaded via dlopen table from '{}'", dir);
	}
	return g_loaded;
}

}  // namespace idtxflow
