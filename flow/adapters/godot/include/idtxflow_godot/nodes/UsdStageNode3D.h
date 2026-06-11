#pragma once

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>

#include <idtxflow_godot/nodes/IUsdNode3D.h>

/**
 * This node represents an USD Stage or an USD Layer defined by an *.usd[a|c|z] file.
 * All converted prims that belong to the usd layer will be assigned as children to this node.
 *
 * The stage is opened + converted entirely inside libidtx_core (idtx_scene.h C ABI); this node
 * holds NO OpenUSD types — it just walks the engine-neutral idtx_scene and builds Godot nodes
 * (see IdtxSceneGodotBuilder). Conversion is synchronous in this build; async streaming returns
 * with the deferred-payload work (Phase 1b).
 *
 * Signals:
 * - `stage_loading_started`
 * - `stage_loading_finished(success: bool)`
 */
class UsdStageNode3D : public godot::Node3D, public IUsdNode3D
{
    GDCLASS(UsdStageNode3D, Node3D)
    IUSDNODE(UsdStageNode3D)

public:
    /*********************** Godot Lifecycle Methods **********************************/
    void _enter_tree() override;
    void _ready() override;
    void _exit_tree() override;

    /** Set the URI of the stage that shall be opened and converted. */
    void set_stage_uri(const godot::String& path);
    /** Get the URI of the stage that was opened and converted. */
    godot::String get_stage_uri() const { return stage_uri_; }

    /** Cached scene filename, persisted after conversion. */
    void set_cached_scene_name(const godot::String& name) { cached_scene_name_ = name; }
    godot::String get_cached_scene_name() const { return cached_scene_name_; }

    /** Open the stage at stage_uri_ through libidtx_core and convert it to Godot nodes. */
    void open_and_convert_stage();

    bool is_loading() const { return is_loading_; }

protected:
    void _reconstruct_node();
    void _configure_nodes_recursive(godot::Node3D* node, godot::Node* owner);
    void _cleanup_nodes();
    godot::String _generate_cached_scene_name(const godot::String& stage_uri, bool binary = true);
    void _pack_and_save_cached_scene();

    static void _bind_methods();

    bool node_ready_ = false;
    godot::String stage_uri_;
    godot::String cached_scene_name_;
    bool is_loading_ = false;
};
