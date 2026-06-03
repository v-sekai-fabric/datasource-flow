#!/usr/bin/env -S godot --headless --script
# Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
# SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
#
# Build a small CSG-shape scene in-script, export via IDTXFlowExporter.
# Demonstrates the "SceneTree script trick" (no .tscn / .blend needed):
# spin up a headless Godot, construct shapes programmatically, dispatch
# the GDExtension's USD writer, exit. Used for round-trip smoke tests
# that don't depend on a pre-authored scene asset.
#
# Usage:
#
#   godot --headless --path <project_with_IDTXFlow_plugin> \
#         --script addons/IDTXFlow/cli/csg_demo_export.gd \
#         -- <out.usda>
#
# Exits 0 on success, 3 on missing args, 5 on export failure.
#
# The demo scene is a Node3D root with five CSG primitives laid out
# along the X axis: box, sphere, cylinder, torus, and a CSG-combined
# subtraction (box minus sphere). Each has a deterministic local
# transform so the resulting .usda is comparable across CI runs.

extends SceneTree


func _init() -> void:
	var args: PackedStringArray = OS.get_cmdline_user_args()
	if args.size() < 1:
		printerr("usage: godot --headless --script csg_demo_export.gd -- <out.usda>")
		quit(3)
		return
	var out_path: String = args[0]

	var root := Node3D.new()
	root.name = "CSGDemo"

	# Five shapes spaced 2.5 units apart along X. Y=1 so they sit on
	# top of the world floor; identity rotation for the box, slight
	# tilts for the others so the exporter has interesting matrices
	# to capture.
	var box := CSGBox3D.new()
	box.name = "Box"
	box.size = Vector3(1.0, 1.0, 1.0)
	box.position = Vector3(-5.0, 1.0, 0.0)
	root.add_child(box)

	var sphere := CSGSphere3D.new()
	sphere.name = "Sphere"
	sphere.radius = 0.7
	sphere.radial_segments = 24
	sphere.rings = 12
	sphere.position = Vector3(-2.5, 1.0, 0.0)
	root.add_child(sphere)

	var cyl := CSGCylinder3D.new()
	cyl.name = "Cylinder"
	cyl.radius = 0.5
	cyl.height = 1.5
	cyl.sides = 24
	cyl.position = Vector3(0.0, 1.0, 0.0)
	cyl.rotation_degrees = Vector3(0.0, 0.0, 15.0)
	root.add_child(cyl)

	var torus := CSGTorus3D.new()
	torus.name = "Torus"
	torus.inner_radius = 0.5
	torus.outer_radius = 0.9
	torus.sides = 24
	torus.ring_sides = 12
	torus.position = Vector3(2.5, 1.0, 0.0)
	torus.rotation_degrees = Vector3(45.0, 0.0, 0.0)
	root.add_child(torus)

	# CSG composition: a box with a sphere subtracted. The OUTER box
	# is the CSG root (mode = UNION by default); the sphere child
	# carries CSGShape3D.OPERATION_SUBTRACTION so it punches a hole.
	var combined := CSGBox3D.new()
	combined.name = "BoxMinusSphere"
	combined.size = Vector3(1.4, 1.4, 1.4)
	combined.position = Vector3(5.0, 1.0, 0.0)
	var hole := CSGSphere3D.new()
	hole.name = "Hole"
	hole.radius = 0.8
	hole.operation = CSGShape3D.OPERATION_SUBTRACTION
	combined.add_child(hole)
	root.add_child(combined)

	# Mark the combined root as the resolver so CSG bakes once.
	combined.set("use_collision", false)

	# CSGShape3D's bake_static_mesh() returns an invalid Ref when the
	# node isn't inside an active SceneTree (Godot 4.5+: the bake
	# pulls runtime services from CSGShape3D::_get_shape which
	# requires NOTIFICATION_ENTER_TREE to have fired). Park the
	# root under SceneTree.root so the CSG nodes can bake, dispatch
	# the export, then tear down.
	get_root().add_child(root)
	var exporter := IDTXFlowExporter.new()
	# Tris-to-quads pass is OFF by default in the exporter (round-
	# trip fixtures expect tri-soup). CSG's bake is pure triangle
	# soup — turn it on explicitly for this demo so adjacent
	# coplanar triangles fold into quads on the USD side.
	exporter.set_reconstruct_quads(true)
	exporter.set_reconstruct_quads_planarity_max_degrees(5.0)
	var ok: bool = exporter.export_scene(root, out_path)
	get_root().remove_child(root)
	root.queue_free()

	if not ok:
		printerr("[csg-demo] export failed for %s" % out_path)
		quit(5)
		return
	print("[csg-demo] wrote %s" % out_path)
	quit(0)
