// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// CHI-312 Unreal runtime-import adapter. Links RuntimeSkeletalMeshGenerator
// (vendored sibling plugin) for the runtime mesh build, and consumes
// libidtx_core via the generated dlopen table (NOT a static link) to read USD.

using System.IO;
using UnrealBuildTool;

public class IdtxRuntimeInstancer : ModuleRules
{
	public IdtxRuntimeInstancer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp17;

		PublicDependencyModuleNames.AddRange(new string[] { "Core" });
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"Projects",                       // IPluginManager for the core DLL dir
			"RuntimeSkeletalMeshGenerator",   // FRuntimeSkeletalMeshGenerator
		});

		// libidtx_core C ABI header (engine-agnostic). Plugin lives at
		// RepoRoot/unreal/plugins/IdtxRuntimeInstancer.
		string PluginDir = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", ".."));
		string RepoRoot = Path.GetFullPath(Path.Combine(PluginDir, "..", "..", ".."));
		PublicIncludePaths.Add(Path.Combine(RepoRoot, "core", "include"));
		PublicIncludePaths.Add(Path.Combine(RepoRoot, "core", "generated"));

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string Base = "libidtx_core.windows.x86_64";
			PublicDefinitions.Add("IDTX_CORE_LIB_BASENAME=\"" + Base + "\"");
			string ImportLib = Path.Combine(RepoRoot, "core", "generated", Base + "_delayload.lib");
			if (File.Exists(ImportLib)) PublicAdditionalLibraries.Add(ImportLib);
			else
			{
				string BuildLib = Path.Combine(RepoRoot, "build", "idtx_core", Base + ".lib");
				if (File.Exists(BuildLib)) PublicAdditionalLibraries.Add(BuildLib);
			}
			PublicDelayLoadDLLs.Add(Base + ".dll");
		}
		else
		{
			string Base = (Target.Platform == UnrealTargetPlatform.Mac)
				? "libidtx_core.macos.arm64" : "libidtx_core.linux.x86_64";
			PublicDefinitions.Add("IDTX_CORE_LIB_BASENAME=\"" + Base + "\"");
			string Stubs = Path.Combine(RepoRoot, "core", "generated", "idtx_core_stubs.cc");
			if (File.Exists(Stubs)) { ExternalDependencies.Add(Stubs); PublicDefinitions.Add("IDTX_CORE_USE_STUBS=1"); }
		}
	}
}
