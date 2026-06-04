#include <idtxflow_godot/nodes/UsdStageNode3D.h>

#include <vector>

#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>

#include "idtx_core/idtx_scene.h"
#include "idtx_core/idtx_core.h"

#include "idtx_core_loader.h"
#include "converter/IdtxSceneGodotBuilder.h"

using namespace godot;

void UsdStageNode3D::_enter_tree()
{
    Node3D::_enter_tree();
    _reconstruct_node();
}

void UsdStageNode3D::_ready()
{
    Node3D::_ready();
    node_ready_ = true;
    _reconstruct_node();
}

void UsdStageNode3D::_exit_tree()
{
    _cleanup_nodes();
    Node3D::_exit_tree();
}

void UsdStageNode3D::set_stage_uri(const String& path)
{
    if (stage_uri_ == path) return;
    stage_uri_ = path;

    _cleanup_nodes();
    is_loading_ = false;

    if (stage_uri_.is_empty()) { cached_scene_name_ = ""; return; }

    // deserializing from a saved scene: defer to _ready
    if (!node_ready_) return;

    call_deferred("open_and_convert_stage");
}

void UsdStageNode3D::open_and_convert_stage()
{
    if (stage_uri_.is_empty() || is_loading_) return;

    is_loading_ = true;
    emit_signal("stage_loading_started");

    // The stage is opened + converted entirely inside libidtx_core; we receive an
    // engine-neutral idtx_scene and build Godot nodes from it (no OpenUSD here).
    idtxflow::load_idtx_core();

    // Resolve res://, user:// to a filesystem path host-side (the core opens a
    // plain path; USD then resolves relative sub-references against it too). A
    // full in-core ArResolver for absolute res:///http:// references is Phase 2.
    String fs_uri = stage_uri_;
    if (fs_uri.begins_with("res://") || fs_uri.begins_with("user://"))
        fs_uri = ProjectSettings::get_singleton()->globalize_path(fs_uri);

    idtx_scene_t* scene = idtx_core_import_scene_from_usd(fs_uri.utf8().get_data());
    is_loading_ = false;

    if (!scene) {
        print_error("Unable to open Stage: " + stage_uri_);
        emit_signal("stage_loading_finished", false);
        return;
    }

    std::vector<Node3D*> roots = idtxflow::BuildGodotNodesFromScene(scene);
    idtx_core_scene_destroy(scene);  // nodes are built; scene data no longer needed

    for (Node3D* node : roots) {
        _configure_nodes_recursive(node, this);
        add_child(node);
    }
    set_name(stage_uri_.get_file().get_basename());

    cached_scene_name_ = _generate_cached_scene_name(stage_uri_);
    call_deferred("_pack_and_save_cached_scene");

    emit_signal("stage_loading_finished", true);
}

void UsdStageNode3D::_reconstruct_node()
{
    if (!node_ready_ || stage_uri_.is_empty()) return;

    if (cached_scene_name_.is_empty() || !FileAccess::file_exists(cached_scene_name_)) {
        _cleanup_nodes();
        open_and_convert_stage();
        return;
    }

    // load the cached packed scene and graft its children under this node
    Ref<PackedScene> packed_scene = ResourceLoader::get_singleton()->load(cached_scene_name_);
    if (packed_scene.is_null()) { open_and_convert_stage(); return; }
    Node3D* cached_root = cast_to<Node3D>(packed_scene->instantiate());
    if (!cached_root) return;
    for (int i = 0; i < cached_root->get_child_count(); i++) {
        if (Node3D* child = cast_to<Node3D>(cached_root->get_child(i))) {
            Node3D* duplicated = cast_to<Node3D>(child->duplicate());
            _configure_nodes_recursive(duplicated, this);
            add_child(duplicated);
        }
    }
    cached_root->queue_free();
}

void UsdStageNode3D::_configure_nodes_recursive(godot::Node3D* node, godot::Node* owner)
{
    if (!node) return;

    if (owner && node != owner) node->call_deferred("set_owner", owner);

    if (IUsdNode3D* usd_node = IUsdNode3D::from_node(node)) {
        usd_node->set_stage_node(this);
        usd_node->set_stage_path(stage_uri_);
    }

    // a nested UsdStageNode3D manages its own subtree
    if (dynamic_cast<UsdStageNode3D*>(node)) return;

    for (int i = 0; i < node->get_child_count(); i++) {
        if (Node3D* child = Object::cast_to<Node3D>(node->get_child(i)))
            if (IUsdNode3D::from_node(child))
                _configure_nodes_recursive(child, owner);
    }
}

void UsdStageNode3D::_cleanup_nodes()
{
    for (int i = 0; i < get_child_count(); i++) {
        Node* child = get_child(i);
        if (child->has_meta("USD_NODE")) {
            remove_child(child);
            child->queue_free();
        }
    }
}

godot::String UsdStageNode3D::_generate_cached_scene_name(const godot::String& stage_uri, bool binary)
{
    if (stage_uri.is_empty()) return "";
    const String cache_dir = "user://usd_cache/";
    const String filename = stage_uri.get_file().get_basename();
    const String file_hash = String::num_int64(stage_uri.hash());
    const String extension = binary ? ".scn" : ".tscn";
    return cache_dir + filename + "_" + file_hash + extension;
}

void UsdStageNode3D::_pack_and_save_cached_scene()
{
    Ref<PackedScene> packed_scene;
    packed_scene.instantiate();
    Error err = packed_scene->pack(this);
    if (err != OK) { print_error("Unable to pack Scene.", err); }
    else {
        String cache_dir = cached_scene_name_.get_base_dir();
        if (!DirAccess::dir_exists_absolute(cache_dir)) DirAccess::make_dir_recursive_absolute(cache_dir);
        ResourceSaver::get_singleton()->save(packed_scene, cached_scene_name_);
    }
    packed_scene.unref();
}

void UsdStageNode3D::_bind_methods()
{
    IUSDNODE_IMPLEMENT_BINDINGS(UsdStageNode3D)

    ClassDB::bind_method(D_METHOD("set_stage_uri", "path"), &UsdStageNode3D::set_stage_uri);
    ClassDB::bind_method(D_METHOD("get_stage_uri"), &UsdStageNode3D::get_stage_uri);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "stage_uri", PROPERTY_HINT_FILE, "*.usd,*.usda,*.usdc,*.usdz"), "set_stage_uri", "get_stage_uri");

    ClassDB::bind_method(D_METHOD("set_cached_scene_name", "name"), &UsdStageNode3D::set_cached_scene_name);
    ClassDB::bind_method(D_METHOD("get_cached_scene_name"), &UsdStageNode3D::get_cached_scene_name);
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "cached_scene_name", PROPERTY_HINT_NONE, "",
            PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY),
        "set_cached_scene_name", "get_cached_scene_name");

    ClassDB::bind_method(D_METHOD("open_and_convert_stage"), &UsdStageNode3D::open_and_convert_stage);
    ClassDB::bind_method(D_METHOD("_pack_and_save_cached_scene"), &UsdStageNode3D::_pack_and_save_cached_scene);

    ADD_SIGNAL(MethodInfo("stage_loading_started"));
    ADD_SIGNAL(MethodInfo("stage_loading_finished", PropertyInfo(Variant::BOOL, "success")));
}
