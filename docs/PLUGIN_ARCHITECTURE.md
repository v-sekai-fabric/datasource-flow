# IDTXFlow Godot Extensibility

## Overview

This document describes how to design Godot-CPP (GDExtension) plugins that **build on top of** the IDTXFlow Godot plugin,
extending its USD prim conversion capabilities without modifying IDTXFlow itself. This enables custom prim type conversion
for the ones that are not know to the current version of the extension (yet), or that has been introduced into usd using
OpenUSD plugins.

---

## Architecture Diagram

```
┌───────────────────────────────────────────────────────────────────────┐
│                         Godot Engine                                  │
│                                                                       │
│  ┌──────────────────────┐     ┌──────────────────────────────────┐    │
│  │   IDTXFlow           │     │   MyIDTXFlowPlugin (3rd-party)   │    │
│  │   (GDExtension)      │     │   (GDExtension)                  │    │
│  │                      │     │                                  │    │
│  │  ┌─────────────────┐ │     │  ┌────────────────────────────┐  │    │
│  │  │ Shared Headers  │◄├─────┤──│ #include <idtxflow/...>    │  │    │
│  │  │ (IPrimConverter │ │     │  │ #include <idtxflow_godot/> │  │    │
│  │  │  Registry, etc) │ │     │  │                            │  │    │
│  │  └─────────────────┘ │     │  │ MyPrimConverter            │  │    │
│  │                      │     │  │   : IPrimConverter<Godot>  │  │    │
│  │  ┌─────────────────┐ │     │  └────────────────────────────┘  │    │
│  │  │ PrimConverter   │ │     │                                  │    │
│  │  │ Registry        │◄├─────┤──  Registry::Instance()          │    │
│  │  │ (DLL-exported   │ │     │   .Register(myPrimConverter)     │    │
│  │  │  singleton)     │ │     │                                  │    │
│  │  └─────────────────┘ │     └──────────────────────────────────┘    │
│  │                      │                                             │
│  │  ConvertPrim()       │                                             │
│  │  ──► Registry.Get()  │                                             │
│  │  ──► converter->     │                                             │
│  │      Convert(prim)   │                                             │
│  └──────────────────────┘                                             │
└───────────────────────────────────────────────────────────────────────┘
```

---

## Two-Layer Public API Include Model

IDTXFlow exposes a **two-layer header architecture** for extension authors, that are availble as part of the *IDTXFlow Godot SDK*.

```
┌─────────────────────────────────────────────────────────────────┐
│  Layer 1: Engine-Agnostic (idtxflow-sdk/include/idtxflow/)      │
│                                                                 │
│  IPrimConverter.h          Abstract converter interface         │
│  PrimConverterRegistry.h   Template registry (raw template)     │
│  TargetTypes.h             Engine type tags                     │
│  MaterialTypes.h           Material type definitions            │
│  ..                        some more definitions                │
└─────────────────────────────────────────────────────────────────┘
                            ▲
                            │  includes
┌────────────────────────────────────────────────────────────────────┐
│  Layer 2: Godot-Specific (idtxflow-sdk/include/idtxflow_godot/)    │
│                                                                    │
│  PrimConverterRegistryGodot.h      DLL-safe extern template decl   │
│  idtxflow_godot_api.h              Export/import macro             │
│  version.h                         API version constants           │
│  converter/UsdGodotTypeConverter.h USD->Godot Type converter       │
│  nodes/*                           Exported Node classes           │
│  types/GodotTypes.h                Godot Type assignments          │
└────────────────────────────────────────────────────────────────────┘
```

### Why Two Layers?

- **Layer 1** (`shared/include/idtxflow/`) contains engine-agnostic C++ templates and interfaces that could be reused
for any target engine (Godot, Unreal, Unity, etc.). These are header-only.

- **Layer 2** (`source/include/idtxflow_godot/`) contains Godot-specific headers that solve the **DLL singleton problem**
— ensuring that the `PrimConverterRegistry<TargetEngineGodot>` singleton exists exactly once (in the IDTXFlow DLL) and is
shared across all extension DLLs.

### Rule for Extension Authors

> **Always** include `<idtxflow_godot/PrimConverterRegistryGodot.h>` instead of the raw `<idtxflow/converter/PrimConverterRegistry.h>`.
> This guarantees the extension links to IDTXFlow's singleton.

---

## Key Components

### 1. `IPrimConverter<TargetEngine>` — The Extension Point

**File:** `idtxflow-sdk/include/idtxflow/converter/IPrimConverter.h`

Abstract interface that third-party plugins implement. Each converter declares:

- **`GetSupportedPrimTypes()`** — Which USD prim type tokens it handles (e.g., `"Capsule"`, `"Camera"`)
- **`GetPriority()`** — Higher priority wins when multiple converters claim the same type
- **`GetConverterName()`** — Unique identifier for logging/debugging
- **`Convert(prim)`** — Creates an instance of the type defined for
`struct TargetEngineTypes<TargetEngine>::ConvertedEntity` from the USD prim. See `idtxflow_godot/types/GodotTypes.h` for
the type assignment.

### 2. `PrimConverterRegistry<TargetEngine>` — The Singleton Registry

**File:** `shared/include/idtxflow/converter/PrimConverterRegistry.h` (template)
**Godot specialization:** `shared/include/idtxflow_godot/PrimConverterRegistryGodot.h` (extern template)
**Explicit instantiation:** `source/converter/PrimConverterRegistryGodot.cpp`

A per-`TargetEngine` singleton that maps `TfToken` → `IPrimConverter*`. Features:

- **Priority sorting** — Multiple converters for the same prim type are sorted by descending priority
- **Late binding** — Converters can be registered at any time during initialization
- **Thread-safe reads** — After initialization, `Get()` is safe for concurrent use
- **DLL-safe** — Explicit template instantiation + export ensures a single instance across all modules

### 3. `ConvertPrim()` — The Integration Point

**File:** `shared/include/idtxflow/converter/StageConverter.h`

The existing `StageConverter` template calls `ConvertPrim()` for any prim type that does not author a payload to another
layer. The `PrimConverterRegistry` is used to check, if a custom converter is registered for this type and delegates the
conversion call to the highest priority one. The built-in conversion is skipped in this case.

### 4. ConvertPrimPostProcess()

**File:** `shared/include/idtxflow/converter/StageConverter.h`

The existing `StageConverter` template uses the `PrimConverterRegistry` to delegate the post processing of the prim conversion
to the custom converter. The custom post processing is executed after the built-in post processing, retrieving the possible
updated converted entity that has been adjusted by the built-in implementation.

### 5. `IUsdNode3D` — The Node Base Class

**File:** `shared/include/idtxflow_godot/nodes/IUsdNode3D.h`

Mixin class that provides common USD metadata (prim path, prim type, variant sets) to any Godot node. Extension authors whose converters create custom node types should:

1. Inherit from the appropriate Godot node class **and** from `IUsdNode3D`
2. Use the `IUSDNODE()` macro in the class body of the header file
3. Use the `IUSDNODE_IMPLEMENT_BINDINGS(ClassName)` macro in `_bind_methods()`

### 6. `IDTXFLOW_GODOT_API` — The Export Macro

**File:** `shared/include/idtxflow_godot/idtxflow_godot_api.h`

Controls symbol visibility:
- When **building IDTXFlow** (`IDTXFLOW_GODOT_EXPORTS` defined): expands to `__declspec(dllexport)` / `__attribute__((visibility("default")))`
- When **consumed by extensions** (no define): expands to `__declspec(dllimport)` / nothing

### 7. `version.h` — API Version Constants

**File:** `shared/include/idtxflow_godot/version.h`

Provides compile-time version checks:

```cpp
#if IDTXFLOW_GODOT_VERSION < IDTXFLOW_GODOT_MAKE_VERSION(0, 2, 0)
#error "This extension requires IDTXFlow Godot API >= 0.2.0"
#endif
```

---

## Detour: How Dependencies Work Between GDExtensions

### The Fundamental Challenge

Godot's GDExtension system loads each `.gdextension` as an independent shared library. There is **no built-in dependency mechanism**
between GDExtensions — unlike Godot modules which are compiled together.

### Our Solution: Three-Layer Dependency Model

```
Layer 1: COMPILE-TIME DEPENDENCY
  └─ Extension #includes IDTXFlow's Layer 1 + Layer 2 headers
  └─ Links against IDTXFlow's exported symbols (extern template)

Layer 2: LOAD-ORDER DEPENDENCY  
  └─ .gdextension [dependencies] section ensures Godot loads IDTXFlow first

Layer 3: RUNTIME DEPENDENCY
  └─ Extension registers converters into IDTXFlow's DLL-exported singleton
  └─ StageConverter queries the same singleton during USD-to-Godot conversion
```

### Compile-Time: Two-Layer Include + Linking

```python
# In your SConstruct:
IDTXFLOW_SDK = "thirdparty/idtxflow-sdk"
env.Append(CPPPATH=[
    # Layer 1 + 2: engine-agnostic + Godot-specific
    f"{IDTXFLOW_SDK}/include",    
])
env.Append(LIBPATH=[f"{IDTXFLOW_SDK}/lib"])
env.Append(LIBS=[f"libidtxflow.{platform}.{target}.{arch}"])
```

### Load-Order: `.gdextension` Dependencies

```ini
[dependencies]
; Ensure IDTXFlow is loaded first
windows.arm64.single.debug = { "res://addons/IDTXFlow/bin/windows/libidtxflow.windows.template_debug.arm64.dll": "" }
windows.arm64.single.release = { "res://addons/IDTXFlow/bin/windows/libidtxflow.windows.template_release.arm64.dll": "" }

windows.x86_64.single.debug = { "res://addons/IDTXFlow/bin/windows/libidtxflow.windows.template_debug.x86_64.dll": "" }
windows.x86_64.single.release = { "res://addons/IDTXFlow/bin/windows/libidtxflow.windows.template_release.x86_64.dll": "" }

macos.arm64.single.debug = { "res://addons/IDTXFlow/bin/macos/libidtxflow.macos.template_debug.arm64.dylib": "" }
macos.arm64.single.release = { "res://addons/IDTXFlow/bin/macos/libidtxflow.macos.template_release.arm64.dylib": "" }
```

This tells Godot to load IDTXFlow's DLL before loading the extension's DLL, guaranteeing the exported singleton is available.

---

### Extension Template Project

A complete starter template is available at `templates/extension_template/`.
Copy it as a starting point for new extensions. See `templates/extension_template/README.md` for detailed instructions.

---

## Best Practices

### 1. Check SCons Tool In The Template Directory For Initial Build Setup

Use `scons/godotcpp.py` and `scons/gdextension.py` scripts as starting point. Adjust the *gdextension* script as needed. 

A project can override a built-in converter by registering one for the same prim type with any priority.

### 2. Keep Converters Stateless

`IPrimConverter::Convert()` receives everything it needs as parameters. Avoid storing mutable state in converter
instances — this enables thread-safe parallel conversion in the future.

### 3. Return `IUsdNode3D`-Compatible Nodes

The built-in post processing expects the converted node to inherit from IUsdNode3D. Thus the returned entity has to
inherit from `IUsdNode3D` and use the `IUSDNODE_IMPLEMENT_*` macros.

### 4. Check API Version at Compile Time

```cpp
#include <idtxflow_godot/version.h>
#if IDTXFLOW_GODOT_VERSION < IDTXFLOW_GODOT_MAKE_VERSION(0, 1, 0)
#error "Requires IDTXFlow Godot API >= 0.1.0"
#endif
```

### 5. Match C++ Runtime on Windows

Ensure both IDTXFlow and your extension use the same C++ runtime linkage (`/MT`).

### 6. Version Your Plugin's Shared Headers

If your plugin itself becomes an extension point (other plugins depend on it), version your headers and document ABI
stability guarantees. Follow the same `extern template` + export pattern used by IDTXFlow.

---