// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ProjectBH : ModuleRules
{
	public ProjectBH(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"ProjectBH",
			"ProjectBH/Variant_Platforming",
			"ProjectBH/Variant_Platforming/Animation",
			"ProjectBH/Variant_Combat",
			"ProjectBH/Variant_Combat/AI",
			"ProjectBH/Variant_Combat/Animation",
			"ProjectBH/Variant_Combat/Gameplay",
			"ProjectBH/Variant_Combat/Interfaces",
			"ProjectBH/Variant_Combat/UI",
			"ProjectBH/Variant_SideScrolling",
			"ProjectBH/Variant_SideScrolling/AI",
			"ProjectBH/Variant_SideScrolling/Gameplay",
			"ProjectBH/Variant_SideScrolling/Interfaces",
			"ProjectBH/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
