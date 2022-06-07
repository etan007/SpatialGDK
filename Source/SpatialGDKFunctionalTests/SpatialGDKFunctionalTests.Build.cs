// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

using UnrealBuildTool;

public class SpatialGDKFunctionalTests : ModuleRules
{
    public SpatialGDKFunctionalTests(ReadOnlyTargetRules Target) : base(Target)
    {
        bLegacyPublicIncludePaths = false;
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false;

        PrivateIncludePaths.Add(ModuleDirectory);
        
        PrivateDependencyModuleNames.AddRange(
            new string[] {
                "SpatialGDK",
                "SpatialGDKEditor",
                "SpatialGDKServices",
                "Core",
                "CoreUObject",
                "Engine",
                "FunctionalTesting",
                "HTTP",
                "UnrealEd",
                "EngineSettings",
                "InputCore",
                "Slate"
            });
    }
}
