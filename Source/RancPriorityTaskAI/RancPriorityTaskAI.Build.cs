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
				"StructUtils",
			}
			);


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
			}
			);
	}
}
