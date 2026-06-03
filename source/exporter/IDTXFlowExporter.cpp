// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

#include "IDTXFlowExporter.h"

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/string.hpp>

#include "UsdGodotStageExporter.h"

bool IDTXFlowExporter::export_scene(godot::Node3D* root, godot::String const& path)
{
    if (root == nullptr) return false;

    // Globalize res:// / user:// before passing to USD.
    godot::String real_path = path;
    if (godot::ProjectSettings* ps = godot::ProjectSettings::get_singleton()) {
        real_path = ps->globalize_path(path);
    }
    idtxflow::exporter::ExportOptions opts;
    opts.reconstruct_quads                       = _reconstruct_quads;
    opts.reconstruct_quads_planarity_max_degrees = _reconstruct_quads_planarity_max_degrees;
    return idtxflow::exporter::ExportSceneToFile(root, real_path, opts);
}

void IDTXFlowExporter::_bind_methods()
{
    godot::ClassDB::bind_method(
        godot::D_METHOD("export_scene", "root", "path"),
        &IDTXFlowExporter::export_scene);

    godot::ClassDB::bind_method(
        godot::D_METHOD("set_reconstruct_quads", "enabled"),
        &IDTXFlowExporter::set_reconstruct_quads);
    godot::ClassDB::bind_method(
        godot::D_METHOD("is_reconstruct_quads"),
        &IDTXFlowExporter::is_reconstruct_quads);

    godot::ClassDB::bind_method(
        godot::D_METHOD("set_reconstruct_quads_planarity_max_degrees", "degrees"),
        &IDTXFlowExporter::set_reconstruct_quads_planarity_max_degrees);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_reconstruct_quads_planarity_max_degrees"),
        &IDTXFlowExporter::get_reconstruct_quads_planarity_max_degrees);
}
