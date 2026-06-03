// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// CHI-312 #4 — build rules for the IDTX Flow Unreal editor plugin.
//
// The plugin does NOT link libidtx_core (nor its statically-linked OpenUSD).
// It consumes the generated dlopen dispatch table, identically to the Godot
// host:
//   * Windows: link a delay-load import lib derived from the checked-in
//     core/generated/libidtx_core.windows.def, with the DLL as a
//     PublicDelayLoadDLL; the module LoadLibrary's it in StartupModule.
//   * POSIX: compile core/generated/idtx_core_stubs.cc (dlsym thunks);
//     core::InitializeStubs dlopen's the lib at StartupModule.
// Either way UE never link-depends on core or its OpenUSD — the same reason it
// is safe alongside UE's own UnrealUSDWrapper (we cross only the flat C ABI and
// core's OpenUSD is static + not re-exported).

using System;
using System.IO;
using UnrealBuildTool;

public class IdtxFlowUnreal : ModuleRules
{
	public IdtxFlowUnreal(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp17; // UE4.27 floor; UE5 accepts 17/20

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"UnrealEd",      // UExporter, editor integration
			"ToolMenus",     // menu entry (UE4.26+/UE5)
			"Projects",      // IPluginManager (locate bundled DLLs)
			"RenderCore",    // FSkeletalMeshLODRenderData, vertex buffers
			"RHI",
		});

		// --- libidtx_core C ABI header (engine-agnostic) -------------------
		// RepoRoot/core/include/idtx_core/idtx_core.h. The plugin lives at
		// RepoRoot/unreal/IdtxFlowUnreal; walk up two dirs to the repo root.
		string PluginDir = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", ".."));
		string RepoRoot = Path.GetFullPath(Path.Combine(PluginDir, "..", ".."));
		string CoreInclude = Path.Combine(RepoRoot, "core", "include");
		string CoreGenerated = Path.Combine(RepoRoot, "core", "generated");
		PublicIncludePaths.Add(CoreInclude);
		PublicIncludePaths.Add(CoreGenerated);

		// Logical lib basename the runtime loader dlopens.
		string CoreBaseName;
		string CoreDllName;

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			CoreBaseName = "libidtx_core.windows.x86_64";
			CoreDllName = CoreBaseName + ".dll";
			PublicDefinitions.Add("IDTX_CORE_LIB_BASENAME=\"" + CoreBaseName + "\"");

			// Delay-load import lib derived from the checked-in .def. Built by a
			// pre-build step (see GenerateDelayLib.bat / the README) into
			// IntermediateDir; if absent we fall back to core's build .lib.
			string DefFile = Path.Combine(CoreGenerated, "libidtx_core.windows.def");
			string ImportLib = Path.Combine(CoreGenerated, CoreBaseName + "_delayload.lib");
			if (File.Exists(ImportLib))
			{
				PublicAdditionalLibraries.Add(ImportLib);
			}
			else if (File.Exists(Path.Combine(RepoRoot, "build", "idtx_core", CoreBaseName + ".lib")))
			{
				// Fallback: core's own import lib (still delay-loaded below).
				PublicAdditionalLibraries.Add(Path.Combine(RepoRoot, "build", "idtx_core", CoreBaseName + ".lib"));
			}

			PublicDelayLoadDLLs.Add(CoreDllName);

			// Stage the core DLL + its runtime deps into packaged builds.
			string BinDir = Path.Combine(PluginDir, "Source", "ThirdParty", "idtx_core", "Win64");
			foreach (string Dep in new string[] { CoreDllName, "usd_ms.dll", "tbb12.dll", "libidtx_usd.dll" })
			{
				string DepPath = Path.Combine(BinDir, Dep);
				RuntimeDependencies.Add("$(BinaryOutputDir)/" + Dep, DepPath);
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			CoreBaseName = "libidtx_core.macos.arm64"; // adjust per-arch as needed
			CoreDllName = CoreBaseName + ".dylib";
			PublicDefinitions.Add("IDTX_CORE_LIB_BASENAME=\"" + CoreBaseName + "\"");
			AddStubs(CoreGenerated);
		}
		else // Linux
		{
			CoreBaseName = "libidtx_core.linux.x86_64";
			CoreDllName = CoreBaseName + ".so";
			PublicDefinitions.Add("IDTX_CORE_LIB_BASENAME=\"" + CoreBaseName + "\"");
			AddStubs(CoreGenerated);
		}
	}

	// POSIX: compile the generated dlsym forwarding thunks into the module so
	// there is no link dependency on core.
	private void AddStubs(string CoreGenerated)
	{
		string Stubs = Path.Combine(CoreGenerated, "idtx_core_stubs.cc");
		if (File.Exists(Stubs))
		{
			// UBT compiles module sources from ModuleDirectory; reference the
			// generated stub via an ExternalSource so it joins the unity build.
			ExternalDependencies.Add(Stubs);
			PublicDefinitions.Add("IDTX_CORE_USE_STUBS=1");
		}
	}
}
