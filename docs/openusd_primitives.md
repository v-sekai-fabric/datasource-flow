# Converted openUSD Prim Types

The following list provides a complete overview of the actual prim types that will be converted into Godot entities. It may repeat some of the nodes mentioned above.

| USD Prim Type                                                                 | Godot Entity          | Remarks                                                                                                                                                                                                                                                                                                                                                                                    |
|-------------------------------------------------------------------------------|-----------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Xform                                                                         | UsdXFormNode3D        |                                                                                                                                                                                                                                                                                                                                                                                            |
| Cube                                                                          | UsdMeshInstanceNode3D | Uses a `BoxMesh`                                                                                                                                                                                                                                                                                                                                                                           |
| Cylinder                                                                      | UsdMeshInstanceNode3D | Uses a `CylinderMesh`. The Prim's Axis attribute is used to rotate the cylinder in Godot to match the expected orientation defined by the axis attribute value.                                                                                                                                                                                                                            |
| Cone                                                                          | UsdMeshInstanceNode3D | Uses a `CylinderMesh` with top radius set to 0.0. The Prim's Axis attribute is used to rotate the cylinder in Godot to match the expected orientation defined by the axis attribute value.                                                                                                                                                                                                 |
| Sphere                                                                        | UsdMeshInstanceNode3D | Uses a `SphereMesh`.                                                                                                                                                                                                                                                                                                                                                                       |
| Mesh                                                                          | UsdMeshInstanceNode3D | Uses an `ArrayMesh`. If the prim contains `GeomSubset`'s they will be converted into surfaces added to the ArrayMesh.                                                                                                                                                                                                                                                                      |
| Primitive with `CollisionAPI` applied <br/> (Cube, Cylinder, Capsule, Sphere) | UsdStaticBodyNode3D   | Uses an `StaticBody3D` with the primitve shape applied as collider shape. At the moment we only support simple collision primitives and no mesh collision shapes.                                                                                                                                                                                                                          |
| SkelRoot                                                                      | -                     | This is the openUSD anchor Prim for any skeleton contained within. It would be an error if a Skeleton Prim exists without this one as parent. However, it will not convert into a Godot entity as such.                                                                                                                                                                                    |
| Skeleton                                                                      | UsdSkeletonNode3D     | The Godot skeleton will maintain the same bone hierarchy as provided by openUSD. However, there might be no limitations on how many skinning weights can be assigned to a single bone in usd. As Godot only supports either 4 or 8 bone weights, only the first 4 or 8 values will be taken from usd. If more than 4 bone weights are given, Godot will use 8 bone weights, setting the remainder to 0.0 weight. The same applies if less than 4 bone weights are provided in the USD content, the remainder to fill up to 4 bones will be set to 0.0 weight. The used bone weights (4 or 8) are normalized to sum up to 1.0 for each vertex. |
| Material                                                                      | StandardMaterial3D    | Creating a Godot StandardMaterial3D from the USD Prim type Material might require to follow references to Shader Prim types in the USD files as those shader might define varying values for specific material values like albedo color, normals, roughness or the like. Those sources can be references to texture images, that will create `Image` and `Texture2D` entities.             |
| Shader                                                                        | -                     | The shader nodes will be used to extract the required information to create a `StandardMaterial3D`, but will convert into different Godot entity types, based on their usage.                                                                                                                                                                                                              |

### Custom openUSD Prim Types

This plugin introduces custom openUSD Prim Types to support specific use-cases. Those custom schemas are compiled into a seperate openUSD plugin library and linked into the final Godot extension.

| Custom IsA-Schema (PrimType) | Godot Entity                 | Remarks                                                                                               |
|------------------------------|------------------------------|-------------------------------------------------------------------------------------------------------|
| Compute_ValueFromJson        | none                         | This Prim is used to register a computation that extracts a field value from a JSON string.           |
| Compute_ColorFromFloat       | none                         | This Prim is used to register a computation that computes a color from a float value                  |
| Datasource                   | none                         | This is an abstract Prim Type that is used as base type for all specific datasource prim types        |
| MockDatasource_RandomFloat   | UsdMockDatasourceFloatNode3D | An excample datasource that simulates data retreival and provisioning as JSON output with random data |


### Pseudo Instancing Example

The following example demonstrates the pseudo-instancing.

```usda
#usda 1.0
(
    defaultPrim = "Bolts"
)

#########################################
# 1. Prototype definition (over)
#########################################

over "BoltPrototype"
{
    def Mesh "Body"
    {
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, 1, 2]
        point3f[] points = [(0,0,0), (1,0,0), (0,1,0)]
    }
}

#########################################
# 2. “Instances” created via references
#########################################

def Xform "Bolts"
{
    def Xform "Bolt1" (
        prepend references = </BoltPrototype>
    )
    {
        
        double3 xformOp:translate = (0, 0, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]

        # optional: override the prototype's child prim
        over "Body" { }
    }

    def Xform "Bolt2" (
        prepend references = </BoltPrototype>
    )
    {
        double3 xformOp:translate = (2, 0, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }

    def Xform "Bolt3" (
        prepend references = </BoltPrototype>
    )
    {        
        double3 xformOp:translate = (4, 0, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }
}
```

### Handling of Payloads

If a prim in a composed stage contains an authored payload like the following, this prim is not immediately loaded during stage composition. Instead the conversation logic will create a UsdStageNode3D entity passing the URI of the payload to trigger loading and converting the referenced stage.

```usda
def "PayloadPrim" (
  prepend payload = @./external.usd@
)
{
}
```

The challenge in this setup is, that the actual stage, that authored the payload, may also author its own opinion on properties or contained prims of the referenced layer.

```usda
def "PayloadPrim" (
  prepend payload = @./external.usd@
)
{
  double3 xformOp:translate = (100.0, 0.0, 0.0)
  uniform token[] xformOpOrder = ["xformOp:translate"]

  over "ChildPrim" {
    color3f[] primvars:displayColor = [(1, 0, 0)]
  }
}
```

To ensure, that, those opinions will not get lost, they will be transferred into a `SessionLayer` that is used when composing the stage of the referenced USD file. This session layer is anchored at the same "location" as the stage the reference was authored in. This ensures, that relative paths can be successfully resolved as expected. This means, the UsdStageNode3D will be created as child node containing the URI to the USD file and the session layer contents.
