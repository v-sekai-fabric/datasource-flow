using UnrealBuildTool;

public class SeamlessAnimatedSkeletons : ModuleRules
{
	public SeamlessAnimatedSkeletons(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"AssetRegistry"
			});
	}
}
