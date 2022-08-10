// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "SpatialGDKServices/Public/SpatialGDKServicesConstants.h"
#include  "WorkerSDK/improbable/c_worker.h"
/**
 * This class ensures that the C API worker library is loaded before it is needed by code.
 * This is only required when a platform uses PublicDelayLoadDLLs in SpatialGDK.Build.cs.
 */
class FSpatialGDKLoader
{
public:
	FSpatialGDKLoader()
	{
#if PLATFORM_WINDOWS
		FString Path = IPluginManager::Get().FindPlugin(TEXT("SpatialGDK"))->GetBaseDir() / TEXT("Binaries/ThirdParty/Improbable");

#if PLATFORM_64BITS
		Path = Path / TEXT("Win64");
#else
		Path = Path / TEXT("Win32");
#endif // PLATFORM_64BITS
		
		FString WorkerFilePath = Path / TEXT("WorkerSDK.dll");
		WorkerLibraryHandle = FPlatformProcess::GetDllHandle(*WorkerFilePath);
		if (WorkerLibraryHandle == nullptr)
		{
			UE_LOG(LogTemp, Fatal, TEXT("Failed to load %s. Have you run `UnrealGDK/Setup.bat`?"), *WorkerFilePath);
		}
		//先加载所有协议
		FString BuildDir;
		if (BuildDir == "")
		{
			BuildDir = FPaths::Combine(SpatialGDKServicesConstants::SpatialOSDirectory, TEXT("build"));
		}
		FString CompiledSchemaDir = FPaths::Combine(BuildDir, TEXT("assembly/schema/"));
		 
		FString proto_root = FPaths::Combine(SpatialGDKServicesConstants::SpatialOSDirectory, TEXT("schema/"));

		FString g3log_path = FPaths::Combine(SpatialGDKServicesConstants::SpatialOSDirectory, TEXT("g3log/"));
		
		std::string path(TCHAR_TO_UTF8(*proto_root));
		std::string json_path(TCHAR_TO_UTF8(*CompiledSchemaDir));
		std::string log_path(TCHAR_TO_UTF8(*g3log_path));
		if(!LoadAllSchema(path,json_path,log_path))
		{
			UE_LOG(LogTemp, Fatal, TEXT("FSpatialGDKLoader::FSpatialGDKLoader LoadAllSchema path=%s,json_path=%s"),*proto_root,*CompiledSchemaDir);
			 
		}

#if TRACE_LIB_ACTIVE

		FString TraceFilePath = Path / TEXT("legacy_trace_dynamic.dll");
		TraceLibraryHandle = FPlatformProcess::GetDllHandle(*TraceFilePath);
		if (TraceLibraryHandle == nullptr)
		{
			UE_LOG(LogTemp, Fatal, TEXT("Failed to load %s. Have you run `UnrealGDK/SetupIncTraceLibs.bat`?"), *TraceFilePath);
		}

#endif // TRACE_LIB_ACTIVE

#elif PLATFORM_PS4
		WorkerLibraryHandle = FPlatformProcess::GetDllHandle(TEXT("libimprobable_worker.prx"));
		if (WorkerLibraryHandle == nullptr)
		{
			UE_LOG(LogTemp, Fatal, TEXT("Failed to load libimprobable_worker.prx"));
		}
#endif
	}

	~FSpatialGDKLoader()
	{
		if (WorkerLibraryHandle != nullptr)
		{
			FPlatformProcess::FreeDllHandle(WorkerLibraryHandle);
			WorkerLibraryHandle = nullptr;
		}

		if (TraceLibraryHandle != nullptr)
		{
			FPlatformProcess::FreeDllHandle(TraceLibraryHandle);
			TraceLibraryHandle = nullptr;
		}
	}

	FSpatialGDKLoader(const FSpatialGDKLoader& rhs) = delete;
	FSpatialGDKLoader& operator=(const FSpatialGDKLoader& rhs) = delete;

private:
	void* WorkerLibraryHandle = nullptr;
	void* TraceLibraryHandle = nullptr;
};
