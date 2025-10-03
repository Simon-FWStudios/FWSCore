// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class FWSCore : ModuleRules
{
	public FWSCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDefinitions.Add("WITH_EOS_SDK_MANAGER=0");
		PrivateIncludePaths.Add(Path.Combine(EngineDirectory, "Source", "ThirdParty", "EOSSDK", "SDK", "Include"));
		PublicIncludePaths.Add(Path.Combine(EngineDirectory, "Source", "ThirdParty", "EOSSDK", "SDK", "Include"));
		AddEngineThirdPartyPrivateStaticDependencies(Target, "EOSSDK");
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "UMG", "EnhancedInput"});
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"OnlineSubsystem", "OnlineSubsystemUtils", "EOSShared",
				"NetCore", "Json", "JsonUtilities"
			}
			);
		
	}
}
