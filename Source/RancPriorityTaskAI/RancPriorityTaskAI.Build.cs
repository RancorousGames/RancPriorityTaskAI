// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RancPriorityTaskAI : ModuleRules
{
	public RancPriorityTaskAI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"AIModule",
				"RancUtilities",
				"NavigationSystem",
				"GameplayTags",
				"CoreUObject",
				"Engine",
			}
			);


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
			}
			);
	}
}
