#include "register_types.h"

#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/classes/project_settings.hpp>

#include <idtxflow_godot/nodes/UsdStageNode3D.h>

#include "nodes/UsdStaticBodyNode3D.h"
#include "nodes/UsdMeshInstanceNode3D.h"
#include "nodes/UsdMultiMeshInstanceNode3D.h"
#include "nodes/UsdXFormNode3D.h"
#include "nodes/IDTXFlowChunker.h"
#include "utils/IDTXFlowGodotLogger.h"

#include "exporter/IDTXFlowExporter.h"

#include "idtx_core_loader.h"
#include "idtx_core/idtx_asset_io.h"

#include <cstring>

using namespace godot;

// Static logger instance — lives for the lifetime of this dll
static idtxflow::utils::IDTXFlowGodotLogger g_logger;

// Host asset-IO: map res://, user:// to a filesystem path via Godot's
// ProjectSettings, so libidtx_core's in-core ArResolver can resolve those
// schemes when they appear INSIDE a USD stage (the core has no engine knowledge).
static int32_t idtxflow_globalize_path(void* /*user*/, const char* uri, char* out_path, int32_t cap) {
    ProjectSettings* ps = ProjectSettings::get_singleton();
    if (!ps || !out_path || cap <= 0) return 0;
    const CharString abs = ps->globalize_path(String(uri)).utf8();
    const int32_t n = static_cast<int32_t>(abs.length());
    if (n + 1 > cap) { out_path[0] = 0; return 0; }
    std::memcpy(out_path, abs.get_data(), n + 1);
    return 1;
}

#ifdef IDTXFLOW_MDL_ENABLED
#include <idtxflow/converter/MdlMaterialConverter.h>

inline std::string get_gdextension_dir()
{
#ifdef MI_PLATFORM_WINDOWS
    char buffer[MAX_PATH];
    HMODULE hm = nullptr;
    // Get handle of the current DLL (this GDExtension)
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)&get_gdextension_dir,
        &hm
    );
    GetModuleFileNameA(hm, buffer, MAX_PATH);
    std::string path(buffer);
    return path.substr(0, path.find_last_of("\\/"));
#else
    Dl_info info;
    if (dladdr((void*)&get_gdextension_dir, &info) == 0)
    {
        return "";
    }
    std::string path(info.dli_fname);
    return path.substr(0, path.find_last_of("/"));
#endif
}
#endif

void initialize_idtxflow_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    // Initialize logger
    idtxflow::utils::Log::set_logger(&g_logger);

    // CHI-312: load libidtx_core via the generated dlopen table (delay-load on
    // Windows, dlsym stubs on POSIX) before any idtx_core_* call. The extension
    // no longer link-depends on core or its static OpenUSD.
    idtxflow::load_idtx_core();

    // Register Godot-backed asset I/O so the core's res://, user:// ArResolver
    // works for references inside a stage (top-level URIs are globalized in
    // UsdStageNode3D before the open call).
    static idtx_asset_io_t s_asset_io = {};
    s_asset_io.globalize_path = idtxflow_globalize_path;
    idtx_core_set_asset_io(&s_asset_io);

    GDREGISTER_CLASS(UsdStageNode3D)
    GDREGISTER_CLASS(UsdXformNode3D)
    GDREGISTER_CLASS(UsdMeshInstanceNode3D)
    GDREGISTER_CLASS(UsdMultiMeshInstanceNode3D)
    GDREGISTER_CLASS(UsdSkeletonNode3D)
    GDREGISTER_CLASS(UsdStaticBodyNode3D)
    GDREGISTER_CLASS(IDTXFlowExporter)
    GDREGISTER_CLASS(IDTXFlowChunker)
    
#ifdef IDTXFLOW_MDL_ENABLED
    // activate the mdl material conversion
    std::string extension_dir = get_gdextension_dir();
    std::vector<std::string> additionalModulPaths;
    if (ProjectSettings *project_settings = godot::ProjectSettings::get_singleton())
    {
        // ensure that the projects resource and user directories can be used as mdl module search paths
        additionalModulPaths.emplace_back(project_settings->globalize_path("res://").utf8().get_data());
        additionalModulPaths.emplace_back(project_settings->globalize_path("user://").utf8().get_data());
    }
    idtxflow::converter::StartupMdlMaterialConverter(extension_dir, additionalModulPaths);
#endif
    
    // NOTE: USD asset resolution (res://, http://) now belongs inside libidtx_core
    // (the single OpenUSD consumer) and will be driven by a host asset-IO callback
    // — Phase 2. Until then the core opens filesystem-path stages directly; this
    // extension links zero OpenUSD, so it no longer registers a pxr ArResolver.
    IDTX_LOGF(IDTX_INFO, "GDExtension initialized");
}

void uninitialize_idtxflow_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    
#ifdef IDTXFLOW_MDL_ENABLED
    // shutdown the mdl material conversion
    idtxflow::converter::ShutdownMdlMaterialConverter();
#endif
    
    IDTX_LOGF(IDTX_INFO, "GDExtension uninitialized");
    
    // Clear logger reference
    idtxflow::utils::Log::set_logger(nullptr);
}

extern "C" {
    GDExtensionBool GDE_EXPORT idtxflow_library_init(
        GDExtensionInterfaceGetProcAddress p_get_proc_address,
        const GDExtensionClassLibraryPtr p_library,
        GDExtensionInitialization *r_initialization) {
        
        GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

        init_obj.register_initializer(initialize_idtxflow_module);
        init_obj.register_terminator(uninitialize_idtxflow_module);
        init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

        return init_obj.init();
    }
}
