// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "SpatialCommandUtils.h"

#include "Internationalization/Regex.h"
#include "Misc/MonitoredProcess.h"
#include "Serialization/JsonSerializer.h"
#include "SpatialGDKServicesConstants.h"
#include "SpatialGDKServicesModule.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif
DEFINE_LOG_CATEGORY(LogSpatialCommandUtils);

#define LOCTEXT_NAMESPACE "SpatialCommandUtils"

bool SpatialCommandUtils::SpatialVersion(bool bIsRunningInChina, const FString& DirectoryToRun, FString& OutResult, int32& OutExitCode)
{
	FString Command = TEXT("version");

	if (bIsRunningInChina)
	{
		Command += SpatialGDKServicesConstants::ChinaEnvironmentArgument;
	}

	FSpatialGDKServicesModule::ExecuteAndReadOutput(*SpatialGDKServicesConstants::SpatialExe, Command, DirectoryToRun, OutResult,
													OutExitCode);

	bool bSuccess = OutExitCode == 0;
	if (!bSuccess)
	{
		UE_LOG(LogSpatialCommandUtils, Warning, TEXT("Spatial version failed. Error Code: %d, Error Message: %s"), OutExitCode, *OutResult);
	}

	return bSuccess;
}

bool SpatialCommandUtils::AttemptSpatialAuth(bool bIsRunningInChina)
{
	FString Command = TEXT("auth login");

	if (bIsRunningInChina)
	{
		Command += SpatialGDKServicesConstants::ChinaEnvironmentArgument;
	}

	int32 OutExitCode;
	FString OutStdOut;
	FString OutStdErr;

	FPlatformProcess::ExecProcess(*SpatialGDKServicesConstants::SpatialExe, *Command, &OutExitCode, &OutStdOut, &OutStdErr);

	bool bSuccess = OutExitCode == 0;
	if (!bSuccess)
	{
		UE_LOG(LogSpatialCommandUtils, Warning, TEXT("Spatial auth login failed. Error Code: %d, StdOut Message: %s, StdErr Message: %s"),
			   OutExitCode, *OutStdOut, *OutStdErr);
	}

	return bSuccess;
}

bool SpatialCommandUtils::BuildWorkerConfig(bool bIsRunningInChina, const FString& DirectoryToRun, FString& OutResult, int32& OutExitCode)
{
	FString Command = TEXT("worker build build-config");

	if (bIsRunningInChina)
	{
		Command += SpatialGDKServicesConstants::ChinaEnvironmentArgument;
	}

	FSpatialGDKServicesModule::ExecuteAndReadOutput(*SpatialGDKServicesConstants::SpatialExe, Command, DirectoryToRun, OutResult,
													OutExitCode);

	bool bSuccess = OutExitCode == 0;
	if (!bSuccess)
	{
		UE_LOG(LogSpatialCommandUtils, Warning, TEXT("Spatial build worker config failed. Error Code: %d, Error Message: %s"), OutExitCode,
			   *OutResult);
	}

	return bSuccess;
}

bool SpatialCommandUtils::GenerateDevAuthToken(bool bIsRunningInChina, FString& OutTokenSecret, FText& OutErrorMessage)
{
	FString Arguments = TEXT("project auth dev-auth-token create --description=\"Unreal GDK Token\" --json_output");
	if (bIsRunningInChina)
	{
		Arguments += SpatialGDKServicesConstants::ChinaEnvironmentArgument;
	}

	FString CreateDevAuthTokenResult;
	int32 ExitCode;
	FSpatialGDKServicesModule::ExecuteAndReadOutput(SpatialGDKServicesConstants::SpatialExe, Arguments,
													SpatialGDKServicesConstants::SpatialOSDirectory, CreateDevAuthTokenResult, ExitCode);

	if (ExitCode != 0)
	{
		FString ErrorMessage = CreateDevAuthTokenResult;
		TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(CreateDevAuthTokenResult);
		TSharedPtr<FJsonObject> JsonRootObject;
		if (FJsonSerializer::Deserialize(JsonReader, JsonRootObject) && JsonRootObject.IsValid())
		{
			JsonRootObject->TryGetStringField("error", ErrorMessage);
		}
		OutErrorMessage = FText::Format(
			LOCTEXT("UnableToGenerateDevAuthToken_Error", "Unable to generate a development authentication token. Result: {0}"),
			FText::FromString(ErrorMessage));
		return false;
	};

	FString AuthResult;
	FString DevAuthTokenResult;
	bool bFoundNewline = CreateDevAuthTokenResult.TrimEnd().Split(TEXT("\n"), &AuthResult, &DevAuthTokenResult, ESearchCase::IgnoreCase,
																  ESearchDir::FromEnd);
	if (!bFoundNewline || DevAuthTokenResult.IsEmpty())
	{
		// This is necessary because spatial might return multiple json structs depending on whether you are already authenticated against
		// spatial and are on the latest version of it.
		DevAuthTokenResult = CreateDevAuthTokenResult;
	}

	TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(DevAuthTokenResult);
	TSharedPtr<FJsonObject> JsonRootObject;
	if (!(FJsonSerializer::Deserialize(JsonReader, JsonRootObject) && JsonRootObject.IsValid()))
	{
		OutErrorMessage = FText::Format(
			LOCTEXT("UnableToParseDevAuthToken_Error", "Unable to parse the received development authentication token. Result: {0}"),
			FText::FromString(DevAuthTokenResult));
		return false;
	}

	// We need a pointer to a shared pointer due to how the JSON API works.
	const TSharedPtr<FJsonObject>* JsonDataObject;
	if (!(JsonRootObject->TryGetObjectField("json_data", JsonDataObject)))
	{
		OutErrorMessage = FText::Format(LOCTEXT("UnableToParseJson_Error", "Unable to parse the received json data. Result: {0}"),
										FText::FromString(DevAuthTokenResult));
		return false;
	}

	FString TokenSecret;
	if (!(*JsonDataObject)->TryGetStringField("token_secret", TokenSecret))
	{
		OutErrorMessage = FText::Format(LOCTEXT("UnableToParseTokenSecretFromJson_Error",
												"Unable to parse the token_secret field inside the received json data. Result: {0}"),
										FText::FromString(DevAuthTokenResult));
		return false;
	}

	OutTokenSecret = TokenSecret;
	return true;
}

bool SpatialCommandUtils::HasDevLoginTag(const FString& DeploymentName, bool bIsRunningInChina, FText& OutErrorMessage)
{
	if (DeploymentName.IsEmpty())
	{
		OutErrorMessage = LOCTEXT("NoDeploymentName", "No deployment name has been specified.");
		return false;
	}

	FString TagsCommand = FString::Printf(TEXT("project deployment tags list %s --json_output"), *DeploymentName);
	if (bIsRunningInChina)
	{
		TagsCommand += SpatialGDKServicesConstants::ChinaEnvironmentArgument;
	}

	FString DeploymentCheckResult;
	int32 ExitCode;
	FSpatialGDKServicesModule::ExecuteAndReadOutput(*SpatialGDKServicesConstants::SpatialExe, TagsCommand,
													SpatialGDKServicesConstants::SpatialOSDirectory, DeploymentCheckResult, ExitCode);
	if (ExitCode != 0)
	{
		FString ErrorMessage = DeploymentCheckResult;
		TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(DeploymentCheckResult);
		TSharedPtr<FJsonObject> JsonRootObject;
		if (FJsonSerializer::Deserialize(JsonReader, JsonRootObject) && JsonRootObject.IsValid())
		{
			JsonRootObject->TryGetStringField("error", ErrorMessage);
		}
		OutErrorMessage = FText::Format(
			LOCTEXT("DeploymentTagsRetrievalFailed", "Unable to retrieve deployment tags. Is the deployment {0} running?\nResult: {1}"),
			FText::FromString(DeploymentName), FText::FromString(ErrorMessage));
		return false;
	};

	FString AuthResult;
	FString RetrieveTagsResult;
	bool bFoundNewline =
		DeploymentCheckResult.TrimEnd().Split(TEXT("\n"), &AuthResult, &RetrieveTagsResult, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	if (!bFoundNewline || RetrieveTagsResult.IsEmpty())
	{
		// This is necessary because spatial might return multiple json structs depending on whether you are already authenticated against
		// spatial and are on the latest version of it.
		RetrieveTagsResult = DeploymentCheckResult;
	}

	TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(RetrieveTagsResult);
	TSharedPtr<FJsonObject> JsonRootObject;
	if (!(FJsonSerializer::Deserialize(JsonReader, JsonRootObject) && JsonRootObject.IsValid()))
	{
		OutErrorMessage = FText::Format(LOCTEXT("DeploymentTagsJsonInvalid", "Unable to parse the received tags.\nResult: {0}"),
										FText::FromString(RetrieveTagsResult));
		return false;
	}

	FString JsonMessage;
	if (!JsonRootObject->TryGetStringField("msg", JsonMessage))
	{
		OutErrorMessage =
			FText::Format(LOCTEXT("DeploymentTagsMsgInvalid", "Unable to parse the msg field inside the received json data.\nResult: {0}"),
						  FText::FromString(RetrieveTagsResult));
		return false;
	}

	/*
	Output looks like this:
	Tags: [unreal_deployment_launcher,dev_login]
	We need to parse it a bit to be able to iterate through the tags
	*/
	if (JsonMessage[6] != '[' || JsonMessage[JsonMessage.Len() - 1] != ']')
	{
		OutErrorMessage =
			FText::Format(LOCTEXT("DeploymentTagsInvalid", "Could not parse the tags.\nMessage: {0}"), FText::FromString(JsonMessage));
		return false;
	}

	FString TagsString = JsonMessage.Mid(7, JsonMessage.Len() - 8);
	TArray<FString> Tags;
	TagsString.ParseIntoArray(Tags, TEXT(","), true);

	if (Tags.Contains(SpatialGDKServicesConstants::DevLoginDeploymentTag))
	{
		return true;
	}

	OutErrorMessage =
		FText::Format(LOCTEXT("DevLoginTagNotAvailable",
							  "The cloud deployment {0} does not have the {1} tag associated with it. The client won't be able to connect "
							  "to the deployment."),
					  FText::FromString(DeploymentName), FText::FromString(SpatialGDKServicesConstants::DevLoginDeploymentTag));
	return false;
}

FProcHandle SpatialCommandUtils::StartLocalReceptionistProxyServer(bool bIsRunningInChina, const FString& CloudDeploymentName,
																   const FString& ListeningAddress, const int32 Port, FString& OutResult,
																   int32& OutExitCode)
{
	FString Command = FString::Printf(TEXT("cloud connect external %s --listening_address %s --local_receptionist_port %i"),
									  *CloudDeploymentName, *ListeningAddress, Port);

	if (bIsRunningInChina)
	{
		Command += SpatialGDKServicesConstants::ChinaEnvironmentArgument;
	}

	FProcHandle ProcHandle;

	void* ReadPipe = nullptr;
	void* WritePipe = nullptr;
	ensure(FPlatformProcess::CreatePipe(ReadPipe, WritePipe));

	ProcHandle = FPlatformProcess::CreateProc(*SpatialGDKServicesConstants::SpatialExe, *Command, false, true, true, nullptr,
											  1 /*PriorityModifer*/, *SpatialGDKServicesConstants::SpatialOSDirectory, WritePipe);

	bool bProcessSucceeded = false;
	bool bProcessFinished = false;
	if (ProcHandle.IsValid())
	{
		while (!bProcessFinished && !bProcessSucceeded)
		{
			bProcessFinished = FPlatformProcess::GetProcReturnCode(ProcHandle, &OutExitCode);

			OutResult = OutResult.Append(FPlatformProcess::ReadPipe(ReadPipe));
			bProcessSucceeded = OutResult.Contains("The receptionist proxy is available");

			FPlatformProcess::Sleep(0.01f);
		}
	}
	else
	{
		UE_LOG(LogSpatialCommandUtils, Error, TEXT("Execution failed. '%s' with arguments '%s' in directory '%s'"),
			   *SpatialGDKServicesConstants::SpatialExe, *Command, *SpatialGDKServicesConstants::SpatialOSDirectory);
	}

	if (!bProcessSucceeded)
	{
		FPlatformProcess::TerminateProc(ProcHandle, true);
		ProcHandle.Reset();
	}

	FPlatformProcess::ClosePipe(0, ReadPipe);
	FPlatformProcess::ClosePipe(0, WritePipe);

	return ProcHandle;
}

void SpatialCommandUtils::StopLocalReceptionistProxyServer(FProcHandle& ProcHandle)
{
	if (ProcHandle.IsValid())
	{
		FPlatformProcess::TerminateProc(ProcHandle, true);
	}
}

bool SpatialCommandUtils::GetProcessName(const FString& PID, FString& OutProcessName)
{
#if PLATFORM_MAC
	UE_LOG(LogSpatialCommandUtils, Warning,
		   TEXT("Failed to get the name of the process that is blocking the required port. To get the name of the process in MacOS you "
				"need to use SpatialCommandUtils::GetProcessInfoFromPort."));
	return false;
#else
	bool bSuccess = false;
	OutProcessName = TEXT("");
	const FString TaskListCmd = TEXT("tasklist");

	// Get the task list line for the process with PID
	const FString TaskListArgs = FString::Printf(TEXT(" /fi \"PID eq %s\" /nh /fo:csv"), *PID);
	FString TaskListResult;
	int32 ExitCode;
	FString StdErr;
	bSuccess = FPlatformProcess::ExecProcess(*TaskListCmd, *TaskListArgs, &ExitCode, &TaskListResult, &StdErr);
	if (ExitCode == 0 && bSuccess)
	{
		FRegexPattern ProcessNamePattern(TEXT("\"(.+?)\""));
		FRegexMatcher ProcessNameMatcher(ProcessNamePattern, TaskListResult);
		if (ProcessNameMatcher.FindNext())
		{
			OutProcessName = ProcessNameMatcher.GetCaptureGroup(1 /* Get the Name of the process, which is the first group. */);
			return true;
		}
	}

	UE_LOG(LogSpatialCommandUtils, Warning, TEXT("Failed to get the name of the process that is blocking the required port."));

	return false;
#endif
}

bool SpatialCommandUtils::TryKillProcessWithPID(const FString& PID)
{
	int32 ExitCode;
	FString StdErr;

#if PLATFORM_WINDOWS
	const FString KillCmd = TEXT("taskkill");
	const FString KillArgs = FString::Printf(TEXT("/F /PID %s"), *PID);
#elif PLATFORM_MAC
	const FString KillCmd = FPaths::Combine(SpatialGDKServicesConstants::BinPath, TEXT("kill"));
	const FString KillArgs = FString::Printf(TEXT("%s"), *PID);
#endif

	FString KillResult;
	bool bSuccess = FPlatformProcess::ExecProcess(*KillCmd, *KillArgs, &ExitCode, &KillResult, &StdErr);
	bSuccess = bSuccess && ExitCode == 0;
	if (!bSuccess)
	{
		UE_LOG(LogSpatialCommandUtils, Error, TEXT("Failed to kill process with PID %s. Error: %s"), *PID, *StdErr);
	}

	return bSuccess;
}

void SpatialCommandUtils::TryKillProcessWithName(const FString& ProcessName)
{
	FPlatformProcess::FProcEnumerator ProcessIt;
	while (ProcessIt.MoveNext())
	{
		if (ProcessIt.GetCurrent().GetName().Equals(ProcessName))
		{
			UE_LOG(LogSpatialCommandUtils, Log, TEXT("Killing process: %s with process ID : %d"), *ProcessName,
				   ProcessIt.GetCurrent().GetPID());
			auto Handle = FPlatformProcess::OpenProcess(ProcessIt.GetCurrent().GetPID());
			FPlatformProcess::TerminateProc(Handle);
		}
	}
}

void SpatialCommandUtils::TryGracefullyKill(const FString& ProcName, const FProcHandle& ProcHandle)
{
#if PLATFORM_LINUX || PLATFORM_MAC
	// On Linux this sends a SIGTERM signal
	// TODO: does this work on Mac?
	FPlatformProcess::TerminateProc(ProcHandle);
#elif PLATFORM_WINDOWS
	// TerminateProc is too forceful on Windows
	TryGracefullyKillWindows(ProcName);
#endif
}

#if PLATFORM_WINDOWS
void SpatialCommandUtils::TryGracefullyKillWindows(const FString& ProcName)
{
	// Use WM_CLOSE signal on windows as TerminateProc forcefully kills on Windows

	// Find runtime window
	const HWND RuntimeWindowHandle = FindWindowA(nullptr, TCHAR_TO_ANSI(*ProcName));
	if (RuntimeWindowHandle != nullptr)
	{
		// Using SendMessageW to send message despite being 'supposed to' use SendMessage - as including "Windows/MinWindows.h" needed for
		// SendMessage overrides the TEXT macro (SendMessageW is also used elsewhere in Unreal)
		SendMessageW(RuntimeWindowHandle, WM_CLOSE, NULL, NULL);
	}
	else
	{
		UE_LOG(LogSpatialDeploymentManager, Error, TEXT("Tried to gracefully stop process '%s' but could not find runtime window."),
			   *ProcName);
	}
}
#endif

bool SpatialCommandUtils::GetProcessInfoFromPort(int32 Port, FString& OutPid, FString& OutState, FString& OutProcessName)
{
#if PLATFORM_WINDOWS
	const FString Command = FString::Printf(TEXT("netstat"));
	// -a display active tcp/udp connections, -o include PID for each connection, -n don't resolve hostnames
	const FString Args = TEXT("-n -o -a");
#elif PLATFORM_MAC
	const FString Command = FPaths::Combine(SpatialGDKServicesConstants::LsofCmdFilePath, TEXT("lsof"));
	// -i:Port list the processes that are running on Port
	const FString Args = FString::Printf(TEXT("-i:%i"), Port);
#endif

	FString Result;
	int32 ExitCode;
	FString StdErr;

	bool bSuccess = FPlatformProcess::ExecProcess(*Command, *Args, &ExitCode, &Result, &StdErr);

	if (ExitCode == 0 && bSuccess)
	{
#if PLATFORM_WINDOWS
		// Get the line of the netstat output that contains the port we're looking for.
		FRegexPattern PidMatcherPattern(FString::Printf(TEXT("(.*?:%i.)(.*)( [0-9]+)"), Port));
#elif PLATFORM_MAC
		// Get the line that contains the name, pid and state of the process.
		FRegexPattern PidMatcherPattern(TEXT("(\\S+)( *\\d+).*(\\(\\S+\\))"));
#endif
		FRegexMatcher PidMatcher(PidMatcherPattern, Result);
		if (PidMatcher.FindNext())
		{
#if PLATFORM_WINDOWS
			OutState = PidMatcher.GetCaptureGroup(2 /* Get the State of the process, which is the second group. */);
			OutPid = PidMatcher.GetCaptureGroup(3 /* Get the PID, which is the third group. */);
			if (!GetProcessName(OutPid, OutProcessName))
			{
				OutProcessName = TEXT("Unknown");
			}
#elif PLATFORM_MAC
			OutProcessName = PidMatcher.GetCaptureGroup(1 /* Get the Name of the process, which is the first group. */);
			OutPid = PidMatcher.GetCaptureGroup(2 /* Get the PID, which is the second group. */);
			OutState = PidMatcher.GetCaptureGroup(3 /* Get the State of the process, which is the third group. */);
#endif
			return true;
		}

#if PLATFORM_WINDOWS
		UE_LOG(LogSpatialCommandUtils, Log, TEXT("The required port %i is not blocked!"), Port);
		return false;
#endif
	}

#if PLATFORM_MAC
	if (bSuccess && ExitCode == 1 && StdErr.IsEmpty())
	{
		UE_LOG(LogSpatialCommandUtils, Log, TEXT("The required port %i is not blocked!"), Port);
		return false;
	}
#endif

	UE_LOG(LogSpatialCommandUtils, Error, TEXT("Failed to find the process that is blocking required port. Error: %s"), *StdErr);
	return false;
}

bool SpatialCommandUtils::FetchRuntimeBinary(const FString& RuntimeVersion, const bool bIsRunningInChina)
{
	FString RuntimePath =
		FPaths::Combine(SpatialGDKServicesConstants::GDKProgramPath, SpatialGDKServicesConstants::RuntimePackageName, RuntimeVersion);
	return FetchPackageBinaryWithRetries(RuntimeVersion, SpatialGDKServicesConstants::RuntimeExe,
										 SpatialGDKServicesConstants::RuntimePackageName, RuntimePath, bIsRunningInChina, true);
}

bool SpatialCommandUtils::FetchInspectorBinary(const FString& InspectorVersion, const bool bIsRunningInChina)
{
	FString InspectorPath = FPaths::Combine(SpatialGDKServicesConstants::GDKProgramPath, SpatialGDKServicesConstants::InspectorPackageName,
											InspectorVersion, SpatialGDKServicesConstants::InspectorExe);
	return FetchPackageBinaryWithRetries(InspectorVersion, SpatialGDKServicesConstants::InspectorExe,
										 SpatialGDKServicesConstants::InspectorPackageName, InspectorPath, bIsRunningInChina, false);
}

bool SpatialCommandUtils::FetchPackageBinaryWithRetries(const FString& PackageVersion, const FString& PackageExe,
														const FString& PackageName, const FString& SaveLocation,
														const bool bIsRunningInChina, const bool bUnzip, const int32 NumRetries /*= 3*/)
{
	int32 Attempt = 0;
	while (!FetchPackageBinary(PackageVersion, PackageExe, PackageName, SaveLocation, bIsRunningInChina, bUnzip))
	{
		Attempt++;
		if (Attempt <= NumRetries)
		{
			UE_LOG(LogSpatialCommandUtils, Log, TEXT("Failed to fetch %s binary. Attempting retry. Retry attempt number: %d"), *PackageName,
				   Attempt);
		}
		else
		{
			UE_LOG(LogSpatialCommandUtils, Error, TEXT("Giving up trying to fetch %s binary after %d retries"), *PackageName, NumRetries);
			break;
		}
	}

	return Attempt <= NumRetries;
}

bool SpatialCommandUtils::FetchPackageBinary(const FString& PackageVersion, const FString& PackageExe, const FString& PackageName,
											 const FString& SaveLocation, const bool bIsRunningInChina, const bool bUnzip)
{
	FPlatformMisc::SetEnvironmentVar(TEXT("IMPROBABLE_INTERNAL_CLI_WRAPPER_GRPC_TIMEOUT "),
									 *FString::Printf(TEXT("%d"), ProcessTimeoutTime));

	FString PackagePath = FPaths::Combine(SpatialGDKServicesConstants::GDKProgramPath, *PackageName, PackageVersion);

	// Check if the binary already exists for a given version
	if (FPaths::FileExists(FPaths::Combine(PackagePath, PackageExe)))
	{
		UE_LOG(LogSpatialCommandUtils, Verbose, TEXT("%s binary already exists."), *PackageName);
		return true;
	}

	// If it does not exist then fetch the binary using `spatial worker package retrieve`
	UE_LOG(LogSpatialCommandUtils, Log, TEXT("Trying to fetch %s version %s"), *PackageName, *PackageVersion);
	FString Params = FString::Printf(TEXT("package retrieve %s %s %s %s"), *PackageName, *SpatialGDKServicesConstants::PlatformVersion,
									 *PackageVersion, *SaveLocation);
	if (bUnzip)
	{
		Params += TEXT(" --unzip");
	}

	if (bIsRunningInChina)
	{
		Params += SpatialGDKServicesConstants::ChinaEnvironmentArgument;
	}

	TOptional<FMonitoredProcess> FetchingProcess;
	const FString& ExePath = SpatialGDKServicesConstants::SpatialExe;
	FetchingProcess = { ExePath, Params, true, true };
	FetchingProcess->OnOutput().BindLambda([](const FString& Output) {
		UE_LOG(LogSpatialCommandUtils, Display, TEXT("FetchingProcess: %s"), *Output);
	});
	FetchingProcess->Launch();

	while (FetchingProcess->Update())
	{
		if (FetchingProcess->GetDuration().GetTotalSeconds() > ProcessTimeoutTime)
		{
			UE_LOG(LogSpatialCommandUtils, Error, TEXT("Timed out waiting for the %s process fetching to start after %ds"), *PackageName,
				   ProcessTimeoutTime);

			FetchingProcess->Exit();
			return false;
		}
	}
	return true;
}
#undef LOCTEXT_NAMESPACE
