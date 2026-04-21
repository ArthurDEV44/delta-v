// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DeltaV : ModuleRules
{
	public DeltaV(ReadOnlyTargetRules Target) : base(Target)
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
			"Slate",
			"SlateCore",
			"Niagara",
			"Chaos",
			"GameplayTags"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"DeltaV",
			"DeltaV/Player",
			"DeltaV/Variant_Platforming",
			"DeltaV/Variant_Platforming/Animation",
			"DeltaV/Variant_Combat",
			"DeltaV/Variant_Combat/AI",
			"DeltaV/Variant_Combat/Animation",
			"DeltaV/Variant_Combat/Gameplay",
			"DeltaV/Variant_Combat/Interfaces",
			"DeltaV/Variant_Combat/UI",
			"DeltaV/Variant_SideScrolling",
			"DeltaV/Variant_SideScrolling/AI",
			"DeltaV/Variant_SideScrolling/Gameplay",
			"DeltaV/Variant_SideScrolling/Interfaces",
			"DeltaV/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
