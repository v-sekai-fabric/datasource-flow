# Datasources and Dataflow

The custom openUSD prim types allows author attributes of prims which actual value is provided by a datasource and possible transformations.
Example usd content:
```usda
#usda 1.0
(
    defaultPrim = "World"
)
def Xform "World" {

    # Author a simple mock data source that creates new random float values every second
    def MockDatasource_RandomFloat "MockDataSource" {
        float inputs:interval = 1.0
        string outputs:data = "{}"
    }
    
    # Author a 'compute' node that extracts the float value from the JSON response of the data source provding it as double
    def Compute_ValueFromJson "GetDoubleFromJson" {
        string inputs:jsonData = "{}"
        string inputs:jsonData.connect = </World/MockDataSource.outputs:data>
        string inputs:jsonPath = "/data/value"
        token inputs:jsonValueType = "double"
        double outputs:jsonValue:double = 0.0
    }
    
    # Author a Cube that changes it's size based on the float/double value provided by the 'MockDataSource'
    def Cube "Cube" ()
    {
        float3[] extent = [(-50, -50, -50), (50, 50, 50)]
        double size = 100
        double size.connect = </World/GetDoubleFromJson.outputs:jsonValue:double>        
        double3 xformOp:translate = (0, 50, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
        color3f[] primvars:displayColor = [(1, 1, 1)]                         
    }
}
```
