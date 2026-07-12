# Material Conversion

The initial version will create `StandardMaterial3D` instances based on the material data authored within the USD layer. One of the challenges by doing so, is that the shader nodes authored within the USD layer might not be able to map to the Godot material 1:1. Further more
the `StandardMaterial3D` supports one UV set accross the different texture channels for the material, while the shader nodes of openUSD might author multiple and different ones. Thus, the conversion logic uses the UV sets named `st` or `st0`, only. If a shader input links to a shader node that references a texture file, this file will be loaded as `Image` and used as `Texture2D` for the respective material channel. The plugin maintains an internal cache of those files to allow re-use of the same images and textures accross different materials.

The following table shows how the USD shader inputs map the the corresponding material channels/properties of the `StandardMaterial3D`.

| USD Shader Input | Godot Material Channel | Remarks |
|---|---|---|
| diffuseColor | Albedo | When linked to a shader that provides a texture, it will bind this texture to UV1 coordinates of the mesh. |
| metallic | Metallic | When linked to a shader that provides a texture, it will bind this texture to UV1 coordinates of the mesh. |
| roughness | Roughness | When linked to a shader that provides a texture, it will bind this texture to UV1 coordinates of the mesh. |
| specular | Specular | When linked to a shader that provides a texture, this can't be reflected in Godot. |
| ior | Specular | The Specular value will be calculated as ((ior - 1)/(ior + 1)²) / 0.08, if the IOR value is > 5.0 the Metallic channel of the material will be set to 1.0 |
| emissiveColor | Emission | This will activate the material feature *EMISSION*. When linked to a shader that provides a texture, it will bind this texture to UV1 coordinates of the mesh. |
| normal | Normal | When linked to a shader that provides a texture, it will bind this texture to UV1 coordinates of the mesh. |
| occlusion | AmbientOcclusion | This will activate the material feature *AMBIENT_OCCLUSION*. When linked to a shader that provides a texture, it will bind this texture to UV1 coordinated of the mesh. |
| opacityThreshold | AlphaScissor | This will set the material transparency mode to *ALPHA_SCISSOR*. The threshold will be checked against the albedo alpha channel. If albedo is retrieved from a texture with alpha channel, then the regions with alpha > threshold appear as cut-outs. |
| opacity | Transparency | The opacity value will be combined with the albedo alpha channel. When linked to a shader that provides a texture, this can't be reflected in Godot. |
