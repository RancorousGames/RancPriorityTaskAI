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
				"RancUtilities"
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"GameplayTags"
				// ... add private dependencies that you statically link with here ...	
			}
			);
	}
}
