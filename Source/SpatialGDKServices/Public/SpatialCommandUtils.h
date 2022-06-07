// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSpatialCommandUtils, Log, All);

class SpatialCommandUtils
{
public:
	SPATIALGDKSERVICES_API static bool SpatialVersion(bool bIsRunningInChina, const FString& DirectoryToRun, FString& OutResult,
													  int32& OutExitCode);
	SPATIALGDKSERVICES_API static bool AttemptSpatialAuth(bool bIsRunningInChina);
	SPATIALGDKSERVICES_API static bool BuildWorkerConfig(bool bIsRunningInChina, const FString& DirectoryToRun, FString& OutResult,
														 int32& OutExitCode);
	SPATIALGDKSERVICES_API static bool GenerateDevAuthToken(bool bIsRunningInChina, FString& OutTokenSecret, FText& OutErrorMessage);
	SPATIALGDKSERVICES_API static bool HasDevLoginTag(const FString& DeploymentName, bool bIsRunningInChina, FText& OutErrorMessage);
	SPATIALGDKSERVICES_API static FProcHandle StartLocalReceptionistProxyServer(bool bIsRunningInChina, const FString& CloudDeploymentName,
																				const FString& ListeningAddress,
																				const int32 ReceptionistPort, FString& OutResult,
																				int32& OutExitCode);
	SPATIALGDKSERVICES_API static void StopLocalReceptionistProxyServer(FProcHandle& ProcessHandle);
	SPATIALGDKSERVICES_API static bool GetProcessInfoFromPort(int32 Port, FString& OutPid, FString& OutState, FString& OutProcessName);
	SPATIALGDKSERVICES_API static bool GetProcessName(const FString& PID, FString& OutProcessName);
	SPATIALGDKSERVICES_API static bool TryKillProcessWithPID(const FString& PID);
	SPATIALGDKSERVICES_API static void TryKillProcessWithName(const FString& ProcessName);
	SPATIALGDKSERVICES_API static void TryGracefullyKill(const FString& ProcName, const FProcHandle& ProcHandle);
	SPATIALGDKSERVICES_API static bool FetchRuntimeBinary(const FString& RuntimeVersion, const bool bIsRunningInChina);
	SPATIALGDKSERVICES_API static bool FetchInspectorBinary(const FString& InspectorVersion, const bool bIsRunningInChina);
	SPATIALGDKSERVICES_API static bool FetchPackageBinary(const FString& PackageVersion, const FString& PackageExe,
														  const FString& PackageName, const FString& SaveLocation,
														  const bool bIsRunningInChina, const bool bUnzip);
	SPATIALGDKSERVICES_API static bool FetchPackageBinaryWithRetries(const FString& PackageVersion, const FString& PackageExe,
																	 const FString& PackageName, const FString& SaveLocation,
																	 const bool bIsRunningInChina, const bool bUnzip,
																	 const int32 NumRetries = 3);

private:
#if PLATFORM_WINDOWS
	static void TryGracefullyKillWindows(const FString& ProcName);
#endif

	// Timeout given in seconds.
	static constexpr double ProcessTimeoutTime = 120.0;
};
