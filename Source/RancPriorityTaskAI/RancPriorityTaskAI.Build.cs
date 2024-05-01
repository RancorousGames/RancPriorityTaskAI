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
				"AIModule",
				"NavigationSystem"
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"GameplayTags",
				"Engine"
				// ... add private dependencies that you statically link with here ...	
			}
			);
		
		PrivateIncludePaths.AddRange(
			new string[] {
				"RancPriorityTaskAI/Public/RancUtilities"
			}
		);
	}
}
