#include "../include/idtxflow_godot/nodes/IUsdNode3D.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/editor_inspector.hpp>

#include <idtxflow_godot/idtxflow_godot_api.h>
#include <idtxflow_godot/nodes/UsdStageNode3D.h>

using namespace godot;

PackedStringArray IDTXFLOW_GODOT_API IUsdNode3D::get_variantset_variants(const String& name) const
{
    if (variant_sets_.has(name))
        return variant_sets_[name];

    return godot::PackedStringArray();
}

void IDTXFLOW_GODOT_API IUsdNode3D::set_variantset_variants(const String& name, const PackedStringArray& values)
{
    variant_sets_[name] = values;
}

IDTXFLOW_GODOT_API IUsdNode3D::~IUsdNode3D()
{
    stage_node_ = nullptr;
}

Dictionary IDTXFLOW_GODOT_API IUsdNode3D::get_variantsets() const
{
    return variant_sets_;
}

void IDTXFLOW_GODOT_API IUsdNode3D::set_variantsets(const Dictionary& variant_sets)
{
    variant_sets_ = variant_sets;
}

String IDTXFLOW_GODOT_API IUsdNode3D::get_variantset_selected_variant(const String& variant_set) const
{
    if (variant_sets_variant_.has(variant_set))
        return variant_sets_variant_[variant_set];

    return String();
}

void IDTXFLOW_GODOT_API IUsdNode3D::set_variantset_selected_variant(const String& variant_set, const String& value, bool is_converting)
{
    // when we are setting the selected variant from convertion mode we just set the value...
    if (is_converting)
    {
        variant_sets_variant_[variant_set] = value;
        return;
    }
    // Applying a variant selection re-composes the prim subtree. That now round-trips
    // through libidtx_core (re-import the stage with the variant selected) rather than
    // mutating a pxr UsdStage here — deferred to Phase 1b. For now record the selection
    // so it persists with the saved scene and can drive the re-import later.
    variant_sets_variant_[variant_set] = value;
}