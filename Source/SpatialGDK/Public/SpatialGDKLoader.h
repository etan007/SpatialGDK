// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"

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
		/*
		FString WorkerFilePath = Path / TEXT("improbable_worker.dll");
		WorkerLibraryHandle = FPlatformProcess::GetDllHandle(*WorkerFilePath);
		if (WorkerLibraryHandle == nullptr)
		{
			UE_LOG(LogTemp, Fatal, TEXT("Failed to load %s. Have you run `UnrealGDK/Setup.bat`?"), *WorkerFilePath);
		}*/

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
