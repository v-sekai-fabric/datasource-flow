extends SceneTree
func _init() -> void:
    var root := Node3D.new()
    root.name = "Root"
    root.transform = Transform3D(Basis(), Vector3(0, 0, 0))

    # Node3D (plain transform)
    var pivot := Node3D.new()
    pivot.name = "Pivot"
    pivot.transform = Transform3D(Basis.from_euler(Vector3(0, 0.5, 0)), Vector3(1, 2, 3))
    root.add_child(pivot)
    pivot.owner = root

    # MeshInstance3D with ArrayMesh + LOD-ready geometry
    var mi := MeshInstance3D.new()
    mi.name = "AvatarBody"
    var mesh := ArrayMesh.new()
    var arrays := []
    arrays.resize(Mesh.ARRAY_MAX)
    # Icosphere-ish (12 verts, 20 tris)
    var verts := PackedVector3Array()
    var normals := PackedVector3Array()
    var uvs := PackedVector2Array()
    var indices := PackedInt32Array()
    # Simple cube for testing (8 verts, 12 tris)
    var v := [Vector3(-1,-1,-1), Vector3(1,-1,-1), Vector3(1,1,-1), Vector3(-1,1,-1),
              Vector3(-1,-1,1), Vector3(1,-1,1), Vector3(1,1,1), Vector3(-1,1,1)]
    for vert in v:
        verts.append(vert)
        normals.append(vert.normalized())
        uvs.append(Vector2(vert.x * 0.5 + 0.5, vert.y * 0.5 + 0.5))
    # 12 triangles (cube faces)
    var idx := [0,1,2, 0,2,3, 4,6,5, 4,7,6, 0,4,5, 0,5,1, 2,6,7, 2,7,3, 0,3,7, 0,7,4, 1,5,6, 1,6,2]
    for i in idx:
        indices.append(i)
    arrays[Mesh.ARRAY_VERTEX] = verts
    arrays[Mesh.ARRAY_NORMAL] = normals
    arrays[Mesh.ARRAY_TEX_UV] = uvs
    arrays[Mesh.ARRAY_INDEX] = indices
    mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)

    # Material with Basis Universal texture
    var mat := StandardMaterial3D.new()
    var img := Image.create(128, 128, true, Image.FORMAT_RGBA8)
    img.fill(Color(0.2, 0.6, 0.9))
    # Generate mipmaps for proper basisu
    img.generate_mipmaps()
    var tex := PortableCompressedTexture2D.new()
    tex.create_from_image(img, PortableCompressedTexture2D.COMPRESSION_MODE_BASIS_UNIVERSAL)
    mat.albedo_texture = tex
    mat.metallic = 0.3
    mat.roughness = 0.7
    mesh.surface_set_material(0, mat)
    mi.mesh = mesh
    mi.transform = Transform3D(Basis(), Vector3(0, 1, 0))
    pivot.add_child(mi)
    mi.owner = root

    # Skeleton3D with bones
    var skel := Skeleton3D.new()
    skel.name = "Armature"
    skel.add_bone("Root")
    skel.add_bone("Spine")
    skel.add_bone("Head")
    skel.set_bone_parent(1, 0)  # Spine -> Root
    skel.set_bone_parent(2, 1)  # Head -> Spine
    skel.set_bone_rest(0, Transform3D(Basis(), Vector3(0, 0, 0)))
    skel.set_bone_rest(1, Transform3D(Basis(), Vector3(0, 0.5, 0)))
    skel.set_bone_rest(2, Transform3D(Basis(), Vector3(0, 0.3, 0)))
    root.add_child(skel)
    skel.owner = root

    # AnimationPlayer
    var anim_player := AnimationPlayer.new()
    anim_player.name = "AnimPlayer"
    var anim_lib := AnimationLibrary.new()
    var anim := Animation.new()
    anim.length = 1.0
    anim.add_track(Animation.TYPE_POSITION_3D)
    anim.track_set_path(0, "Pivot:position")
    anim.track_insert_key(0, 0.0, Vector3(0, 0, 0))
    anim.track_insert_key(0, 1.0, Vector3(0, 2, 0))
    anim_lib.add_animation("bounce", anim)
    anim_player.add_animation_library("", anim_lib)
    root.add_child(anim_player)
    anim_player.owner = root

    # Camera3D
    var cam := Camera3D.new()
    cam.name = "Camera"
    cam.transform = Transform3D(Basis(), Vector3(0, 1.5, 5))
    cam.fov = 75.0
    root.add_child(cam)
    cam.owner = root

    # DirectionalLight3D
    var light := DirectionalLight3D.new()
    light.name = "Sun"
    light.transform = Transform3D(Basis.from_euler(Vector3(-0.8, 0.3, 0)), Vector3(0, 10, 0))
    light.light_energy = 1.2
    root.add_child(light)
    light.owner = root

    # StaticBody3D + CollisionShape3D (physics)
    var body := StaticBody3D.new()
    body.name = "Floor"
    var col := CollisionShape3D.new()
    col.name = "FloorShape"
    var shape := BoxShape3D.new()
    shape.size = Vector3(10, 0.1, 10)
    col.shape = shape
    body.add_child(col)
    col.owner = root
    root.add_child(body)
    body.owner = root

    # Pack and save
    var scene := PackedScene.new()
    scene.pack(root)
    ResourceSaver.save(scene, "res://test_all_node3d.scn")
    ResourceSaver.save(scene, "res://test_all_node3d_compressed.scn", ResourceSaver.FLAG_COMPRESS)
    print("Saved test_all_node3d.scn (%d bytes)" % FileAccess.open("res://test_all_node3d.scn", FileAccess.READ).get_length())
    print("Saved test_all_node3d_compressed.scn")
    root.queue_free()
    quit()
