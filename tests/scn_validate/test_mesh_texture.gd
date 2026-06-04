extends SceneTree
func _init() -> void:
    var root := Node3D.new()
    root.name = "Root"

    # Create mesh with actual surface data
    var mesh := ArrayMesh.new()
    var arrays := []
    arrays.resize(Mesh.ARRAY_MAX)

    # Simple quad (2 triangles)
    var verts := PackedVector3Array([
        Vector3(-1, 0, -1), Vector3(1, 0, -1),
        Vector3(1, 0, 1), Vector3(-1, 0, 1)
    ])
    var normals := PackedVector3Array([
        Vector3(0, 1, 0), Vector3(0, 1, 0),
        Vector3(0, 1, 0), Vector3(0, 1, 0)
    ])
    var uvs := PackedVector2Array([
        Vector2(0, 0), Vector2(1, 0),
        Vector2(1, 1), Vector2(0, 1)
    ])
    var indices := PackedInt32Array([0, 1, 2, 0, 2, 3])

    arrays[Mesh.ARRAY_VERTEX] = verts
    arrays[Mesh.ARRAY_NORMAL] = normals
    arrays[Mesh.ARRAY_TEX_UV] = uvs
    arrays[Mesh.ARRAY_INDEX] = indices
    mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)

    # Create material with a portable compressed texture (basis universal)
    var mat := StandardMaterial3D.new()
    mat.albedo_color = Color(1, 0.5, 0.2)

    # Create a small image and compress it as basis universal
    var img := Image.create(64, 64, false, Image.FORMAT_RGBA8)
    img.fill(Color(0.8, 0.2, 0.1))
    var tex := PortableCompressedTexture2D.new()
    tex.create_from_image(img, PortableCompressedTexture2D.COMPRESSION_MODE_BASIS_UNIVERSAL)
    mat.albedo_texture = tex

    mesh.surface_set_material(0, mat)

    var mi := MeshInstance3D.new()
    mi.name = "TexturedMesh"
    mi.mesh = mesh
    mi.transform = Transform3D(Basis(), Vector3(2, 0, 3))
    root.add_child(mi)
    mi.owner = root

    var scene := PackedScene.new()
    scene.pack(root)
    ResourceSaver.save(scene, "res://test_mesh_basisu.scn")
    ResourceSaver.save(scene, "res://test_mesh_basisu_compressed.scn", ResourceSaver.FLAG_COMPRESS)
    print("Saved test_mesh_basisu.scn and test_mesh_basisu_compressed.scn")
    root.queue_free()
    quit()
