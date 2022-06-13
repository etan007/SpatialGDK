// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

using System;
using System.ComponentModel;
using System.Collections.Generic;
using System.Text;
using System.IO;
using System.Diagnostics;
using UnrealBuildTool;

public class SpatialGDK : ModuleRules
{
    public SpatialGDK(ReadOnlyTargetRules Target) : base(Target)
    {
        bLegacyPublicIncludePaths = false;
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false;

        PrivateIncludePaths.Add("SpatialGDK/Private");

        var WorkerSDKPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "Public", "WorkerSDK"));
        PublicIncludePaths.Add(WorkerSDKPath); // Worker SDK uses a different include format <improbable/x.h>
        PrivateIncludePaths.Add(WorkerSDKPath);
        
        PublicDefinitions.Add("GOOGLE_PROTOBUF_NO_RTTI=1");
        PublicDefinitions.Add("GOOGLE_PROTOBUF_CMAKE_BUILD");
        
        var ProtobufPath =  Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "ThirdParty","include"));
        PublicIncludePaths.Add(ProtobufPath); 
        PrivateIncludePaths.Add(ProtobufPath);
        
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "EngineSettings",
                "InputCore",
                "OnlineSubsystemUtils",
                "Projects",
                "ReplicationGraph",
                "Sockets",
                "Slate",
                "UMG",
                "NetCore"
            });

        if (Target.bBuildDeveloperTools || (Target.Configuration != UnrealTargetConfiguration.Shipping &&
                                            Target.Configuration != UnrealTargetConfiguration.Test))
        {
            PublicDependencyModuleNames.Add("GameplayDebugger");
            PublicDefinitions.Add("WITH_GAMEPLAY_DEBUGGER=1");
        }
        else
        {
            PublicDefinitions.Add("WITH_GAMEPLAY_DEBUGGER=0");
        }

        if (Target.bBuildEditor)
        {
            PublicDependencyModuleNames.Add("UnrealEd");
            PublicDependencyModuleNames.Add("SpatialGDKServices");
        }

        if (Target.bWithPerfCounters)
        {
            PublicDependencyModuleNames.Add("PerfCounters");
        }

        var WorkerLibraryDir = Path.Combine(ModuleDirectory, "..", "..", "Binaries", "ThirdParty", "Improbable", Target.Platform.ToString());

        string LibPrefix = "libimprobable_";
        string ImportLibSuffix = ".so";
        string SharedLibSuffix = ".so";
        bool bAddDelayLoad = false;

        if ( Target.Platform == UnrealTargetPlatform.Win64)
        {
            LibPrefix = "improbable_";
            ImportLibSuffix = ".lib";
            SharedLibSuffix = ".dll";
            bAddDelayLoad = true;
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            ImportLibSuffix = SharedLibSuffix = ".dylib";
        }
        else if (Target.Platform == UnrealTargetPlatform.IOS)
        {
            ImportLibSuffix = SharedLibSuffix = "_static.a";
        }
        else if(!(Target.Platform == UnrealTargetPlatform.Linux || Target.Platform == UnrealTargetPlatform.Android))
        {
            throw new System.Exception(System.String.Format("Unsupported platform {0}", Target.Platform.ToString()));
        }

        string WorkerImportLib = System.String.Format("{0}worker{1}", LibPrefix, ImportLibSuffix);
        string WorkerSharedLib = System.String.Format("{0}worker{1}", LibPrefix, SharedLibSuffix);

        if (Target.Platform != UnrealTargetPlatform.Android)
        {
            RuntimeDependencies.Add(Path.Combine(WorkerLibraryDir, WorkerSharedLib), StagedFileType.NonUFS);
            if (bAddDelayLoad)
            {
                PublicDelayLoadDLLs.Add(WorkerSharedLib);
            }

            WorkerImportLib = Path.Combine(WorkerLibraryDir, WorkerImportLib);
            PublicRuntimeLibraryPaths.Add(WorkerLibraryDir);

            //PublicAdditionalLibraries.Add(WorkerImportLib);
        }
        else
        {
            var WorkerLibraryPaths = new List<string>
            {
                Path.Combine(WorkerLibraryDir, "arm64-v8a"),
                Path.Combine(WorkerLibraryDir, "armeabi-v7a"),
                Path.Combine(WorkerLibraryDir, "x86_64"),
            };

            string PluginPath = Utils.MakePathRelativeTo(ModuleDirectory, Target.RelativeEnginePath);
            AdditionalPropertiesForReceipt.Add("AndroidPlugin", Path.Combine(PluginPath, "SpatialGDK_APL.xml"));

            PublicRuntimeLibraryPaths.AddRange(WorkerLibraryPaths);

            var WorkerLibraries = new List<string>
            {
                Path.Combine(WorkerLibraryDir, "arm64-v8a", WorkerSharedLib),
                Path.Combine(WorkerLibraryDir, "armeabi-v7a", WorkerSharedLib),
                Path.Combine(WorkerLibraryDir, "x86_64", WorkerSharedLib),
            };

            //PublicAdditionalLibraries.AddRange(WorkerLibraries);
        }

        // Detect existence of trace library, if present add preprocessor
        string TraceStaticLibPath = "";
        string TraceDynamicLib = "";
        string TraceDynamicLibPath = "";
        if ( Target.Platform == UnrealTargetPlatform.Win64)
        {
            TraceStaticLibPath = Path.Combine(WorkerLibraryDir, "legacy_trace_dynamic.lib");
            TraceDynamicLib = "legacy_trace_dynamic.dll";
            TraceDynamicLibPath = Path.Combine(WorkerLibraryDir, TraceDynamicLib);
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            TraceStaticLibPath = Path.Combine(WorkerLibraryDir, "liblegacy_trace_dynamic.so");
            TraceDynamicLib = "liblegacy_trace_dynamic.so";
            TraceDynamicLibPath = Path.Combine(WorkerLibraryDir, TraceDynamicLib);
        }

        if (File.Exists(TraceStaticLibPath) && File.Exists(TraceDynamicLibPath))
        {
            //Log.TraceInformation("Detection of trace libraries found at {0} and {1}, enabling trace functionality.", TraceStaticLibPath, TraceDynamicLibPath);
            PublicDefinitions.Add("TRACE_LIB_ACTIVE=1");

            PublicAdditionalLibraries.Add(TraceStaticLibPath);

            RuntimeDependencies.Add(TraceDynamicLibPath, StagedFileType.NonUFS);
            if (bAddDelayLoad)
            {
                PublicDelayLoadDLLs.Add(TraceDynamicLib);
            }
        }
        else
        {
            //Log.TraceInformation("Didn't find trace libraries at {0} and {1}, disabling trace functionality.", TraceStaticLibPath, TraceDynamicLibPath);
            PublicDefinitions.Add("TRACE_LIB_ACTIVE=0");
        }
        
        // protobuf lib
        /*
        var ProtobufLibraryDir = Path.Combine(ModuleDirectory, "..",  "ThirdParty", "Lib", Target.Platform.ToString());
        var ProtobufStaticLibPath = "";
        if ( Target.Platform == UnrealTargetPlatform.Win64)
        {
            ProtobufStaticLibPath = Path.Combine(ProtobufLibraryDir, "libprotobuf.lib");
          
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            ProtobufStaticLibPath = Path.Combine(WorkerLibraryDir, "libprotobuf.a");
             
        }
        PublicAdditionalLibraries.Add(ProtobufStaticLibPath);*/
        var protobufLibraryDir = Path.Combine(ModuleDirectory, "..",  "ThirdParty");
        if (Target.Platform == UnrealTargetPlatform.Win64) {
            PublicAdditionalLibraries.Add(Path.Combine(protobufLibraryDir, "lib", "Windows", "libprotobuf.lib"));
        } else if (Target.Platform == UnrealTargetPlatform.IOS) {
            PublicAdditionalLibraries.Add(Path.Combine(protobufLibraryDir, "lib", "IOS", "libprotobuf.a"));
        } else if (Target.Platform == UnrealTargetPlatform.Android) {
            PublicAdditionalLibraries.Add(Path.Combine(protobufLibraryDir, "lib", "Android", "ARMv7", "libprotobuf.a"));
            PublicAdditionalLibraries.Add(Path.Combine(protobufLibraryDir, "lib", "Android", "ARM64", "libprotobuf.a"));
            PublicAdditionalLibraries.Add(Path.Combine(protobufLibraryDir, "lib", "Android", "x64", "libprotobuf.a"));
            PublicAdditionalLibraries.Add(Path.Combine(protobufLibraryDir, "lib", "Android", "x86", "libprotobuf.a"));
        }
    }
}
