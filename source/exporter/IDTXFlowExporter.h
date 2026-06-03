// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// IDTXFlowExporter — GDScript-callable wrapper around
// idtxflow::exporter::ExportSceneToFile.
//
// Usage from GDScript:
//
//   var exporter := IDTXFlowExporter.new()
//   if exporter.export_scene(my_node3d, "res://exported.usda"):
//       print("ok")
//
// Returns true on success, false on failure (failure detail in the
// IDTX log output).

#pragma once

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/ref_counted.hpp>

namespace godot { class String; }

class IDTXFlowExporter : public godot::RefCounted
{
    GDCLASS(IDTXFlowExporter, godot::RefCounted)

public:
    IDTXFlowExporter() = default;
    ~IDTXFlowExporter() override = default;

    /// Export a Godot Node3D sub-tree to a .usda file.
    /// Returns true on success. The file path can be a `res://` /
    /// `user://` URI; it gets globalized before being passed to USD.
    bool export_scene(godot::Node3D* root, godot::String const& path);

    /// When true, after collecting all meshes the exporter runs
    /// idtx_mesh_reconstruct_quads on each tri-soup mesh (greedy
    /// mutual-best matching, planarity gate at
    /// `reconstruct_quads_planarity_max_degrees`). Quads land in
    /// faceVertexCounts on the USD side. Default off — flipping
    /// it on changes the topology of the resulting USDA in ways
    /// existing round-trip fixtures aren't tolerant of yet.
    void set_reconstruct_quads(bool enabled) { _reconstruct_quads = enabled; }
    bool is_reconstruct_quads() const        { return _reconstruct_quads; }

    void set_reconstruct_quads_planarity_max_degrees(float v) {
        _reconstruct_quads_planarity_max_degrees = v;
    }
    float get_reconstruct_quads_planarity_max_degrees() const {
        return _reconstruct_quads_planarity_max_degrees;
    }

protected:
    static void _bind_methods();

private:
    bool  _reconstruct_quads                          = false;
    float _reconstruct_quads_planarity_max_degrees    = 5.0f;
};
