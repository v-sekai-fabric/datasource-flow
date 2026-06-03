#!/usr/bin/env -S godot --headless --script
# Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
# SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
#
# Headless USD export driver. Loads a .tscn / .scn, walks it via
# IDTXFlowExporter, writes a .usda to the requested path.
#
# Usage (from a Godot project that has the IDTXFlow plugin enabled):
#
#   godot --headless --path <project> \
#         --script addons/IDTXFlow/cli/export_scene.gd \
#         -- <scene_path.tscn> <out_path.usda>
#
# The `--` separator passes the trailing args to the script via
# OS.get_cmdline_user_args(). Exit code 0 on success, non-zero on
# failure (3 = bad args, 4 = load failed, 5 = export failed).

extends SceneTree

func _init() -> void:
	var args: PackedStringArray = OS.get_cmdline_user_args()
	if args.size() < 2:
		printerr("usage: godot --headless --script %s -- <scene.tscn> <out.usda>" % (get_script() as Script).resource_path)
		quit(3)
		return

	var scene_path: String = args[0]
	var out_path:   String = args[1]

	var packed: PackedScene = load(scene_path)
	if packed == null:
		printerr("[idtx-flow cli] failed to load %s" % scene_path)
		quit(4)
		return

	var root: Node = packed.instantiate()
	if not (root is Node3D):
		printerr("[idtx-flow cli] scene root must be a Node3D (got %s)" % root.get_class())
		quit(4)
		return

	var exporter := IDTXFlowExporter.new()
	var ok: bool = exporter.export_scene(root, out_path)
	root.queue_free()

	if not ok:
		printerr("[idtx-flow cli] export failed for %s" % out_path)
		quit(5)
		return

	print("[idtx-flow cli] %s -> %s ok" % [scene_path, out_path])
	quit(0)
