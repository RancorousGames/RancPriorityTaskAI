// Copyright Rancorous Games, 2026

using UnrealBuildTool;

/** Optional debug inspection of RAI minds. Excluded from Shipping through the plugin descriptor. */
public class RancPriorityTaskAIMindView : ModuleRules
{
	public RancPriorityTaskAIMindView(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"AIModule",
			"RancPriorityTaskAI",
		});
	}
}
