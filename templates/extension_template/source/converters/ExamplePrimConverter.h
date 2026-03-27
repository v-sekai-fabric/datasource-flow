#pragma once
/**
 * @file ExamplePrimConverter.h
 * @brief Skeleton converter demonstrating how to implement IPrimConverter for a
 *        custom USD prim type.
 *
 * Replace "MyCustomPrim" with the actual USD prim type token you want to handle,
 * and implement the Convert() method with your conversion logic.
 */

#include <idtxflow/converter/IPrimConverter.h>
#include <idtxflow_godot/PrimConverterRegistryGodot.h>

#include <godot_cpp/classes/node3d.hpp>

class ExamplePrimConverter
    : public idtxflow::converter::IPrimConverter<idtxflow::types::TargetEngineGodot>
{
public:
    std::vector<pxr::TfToken> GetSupportedPrimTypes() const override
    {
        // Return the USD prim type token(s) this converter handles.
        // Multiple tokens are supported if one converter handles several types.
        return { pxr::TfToken("MyCustomPrim") };
    }

    int GetPriority() const override
    {
        // 100 = standard third-party priority.
        // Use higher values to override built-in or other third-party converters.
        return 100;
    }

    std::string GetConverterName() const override
    {
        return "ExamplePrimConverter";
    }

    godot::Node3D* Convert(const pxr::UsdPrim& prim) override
    {
        // TODO: Read USD attributes from `prim` and create the appropriate
        //       Godot node. For example:
        //
        //   auto* node = memnew(godot::MeshInstance3D);
        //   node->set_transform(transform);
        //   // ... set up mesh, materials, etc. from prim attributes ...
        //   return node;

        // Placeholder: return a plain Node3D with the prim's name
        auto* node = memnew(godot::Node3D);
        node->set_name(godot::String(prim.GetName().GetText()));
        return node;
    }

    godot::Node3D* PostProcess(
        const pxr::UsdPrim& prim,
        godot::Node3D* converted,
        godot::Node3D* parent) override
    {
        // Optional: perform setup that depends on the scene hierarchy.
        // Called after parent-child relationships have been established.
        return converted;
    }
};