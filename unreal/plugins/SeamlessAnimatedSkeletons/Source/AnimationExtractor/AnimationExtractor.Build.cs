using UnrealBuildTool;

public class AnimationExtractor : ModuleRules
{
	public AnimationExtractor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Projects",
				"InputCore",
				"UnrealEd",
				"ToolMenus",
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"SeamlessAnimatedSkeletons",
				"NiagaraAnimNotifies"
			}
		);
	}
}
