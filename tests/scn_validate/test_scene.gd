extends SceneTree
func _init() -> void:
    var root := Node3D.new()
    root.name = "Root"
    var child := MeshInstance3D.new()
    child.name = "Cube"
    var mesh := BoxMesh.new()
    mesh.size = Vector3(1, 1, 1)
    child.mesh = mesh
    root.add_child(child)
    child.owner = root
    var scene := PackedScene.new()
    scene.pack(root)
    ResourceSaver.save(scene, "res://test_output.scn")
    ResourceSaver.save(scene, "res://test_output_compressed.scn", ResourceSaver.FLAG_COMPRESS)
    print("Saved test_output.scn and test_output_compressed.scn")
    root.queue_free()
    quit()
