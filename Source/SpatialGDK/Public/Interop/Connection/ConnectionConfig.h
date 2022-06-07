// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "Containers/UnrealString.h"
#include "Engine/EngineBaseTypes.h"
#include "Internationalization/Regex.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include "SpatialConstants.h"
#include "SpatialGDKSettings.h"

#include <WorkerSDK/improbable/c_worker.h>

DECLARE_LOG_CATEGORY_EXTERN(LogConnectionConfig, Log, All);

struct FConnectionConfig
{
	enum EWorkerType
	{
		Client,
		Server
	};

	FConnectionConfig()
		: UseExternalIp(false)
		, EnableWorkerSDKProtocolLogging(false)
		, EnableWorkerSDKOpLogging(false)
		, WorkerSDKLogFileSize(10 * 1024 * 1024)
		, WorkerSDKLogLevel(WORKER_LOG_LEVEL_INFO)
		, LinkProtocol(WORKER_NETWORK_CONNECTION_TYPE_TCP)
		, TcpMultiplexLevel(2) // This is a "finger-in-the-air" number.
		// These settings will be overridden by Spatial GDK settings before connection applied (see PreConnectInit)
		, TcpNoDelay(0)
		, UdpUpstreamIntervalMS(0)
		, UdpDownstreamIntervalMS(0)
	{
		const TCHAR* CommandLine = FCommandLine::Get();

		FParse::Value(CommandLine, TEXT("workerId"), WorkerId);
		FParse::Bool(CommandLine, TEXT("enableWorkerSDKProtocolLogging"), EnableWorkerSDKProtocolLogging);
		FParse::Bool(CommandLine, TEXT("enableWorkerSDKOpLogging"), EnableWorkerSDKOpLogging);
		FParse::Value(CommandLine, TEXT("workerSDKLogPrefix"), WorkerSDKLogPrefix);
		// TODO: When upgrading to Worker SDK 14.6.2, remove this parameter and set it to 0 for infinite file size
		FParse::Value(CommandLine, TEXT("workerSDKLogFileSize"), WorkerSDKLogFileSize);

		GetWorkerSDKLogLevel(CommandLine);
		GetLinkProtocol(CommandLine);
	}

	void PreConnectInit(const bool bConnectAsClient)
	{
		const USpatialGDKSettings* SpatialGDKSettings = GetDefault<USpatialGDKSettings>();

		if (WorkerType.IsEmpty())
		{
			WorkerType = bConnectAsClient ? SpatialConstants::DefaultClientWorkerType.ToString()
										  : SpatialConstants::DefaultServerWorkerType.ToString();
			UE_LOG(LogConnectionConfig, Warning, TEXT("No worker type specified through commandline, defaulting to %s"), *WorkerType);
		}

		if (WorkerId.IsEmpty())
		{
			WorkerId = WorkerType + FGuid::NewGuid().ToString();
		}

		TcpNoDelay = (SpatialGDKSettings->bTcpNoDelay ? 1 : 0);

		static_assert(EWorkerType::Client == 0 && EWorkerType::Server == 1, "Assuming indexes of enum for client and server");

		UdpUpstreamIntervalMS =
			10; // Despite flushing on the worker ops thread, WorkerSDK still needs to send periodic data (like ACK, resends and ping).
		UdpDownstreamIntervalMS = (bConnectAsClient ? SpatialGDKSettings->UdpClientDownstreamUpdateIntervalMS
													: SpatialGDKSettings->UdpServerDownstreamUpdateIntervalMS);

		LinkProtocol = ConnectionTypeMap[bConnectAsClient ? EWorkerType::Client : EWorkerType::Server];

		uint32 DownstreamWindowSizes[2] = { SpatialGDKSettings->ClientDownstreamWindowSizeBytes,
											SpatialGDKSettings->ServerDownstreamWindowSizeBytes };
		uint32 UpstreamWindowSizes[2] = { SpatialGDKSettings->ClientUpstreamWindowSizeBytes,
										  SpatialGDKSettings->ServerUpstreamWindowSizeBytes };

		DownstreamWindowSizeBytes = DownstreamWindowSizes[bConnectAsClient ? EWorkerType::Client : EWorkerType::Server];
		UpstreamWindowSizeBytes = UpstreamWindowSizes[bConnectAsClient ? EWorkerType::Client : EWorkerType::Server];
	}

private:
	void GetWorkerSDKLogLevel(const TCHAR* CommandLine)
	{
		FString LogLevelString;
		FParse::Value(CommandLine, TEXT("workerSDKLogLevel"), LogLevelString);
		if (LogLevelString.Compare(TEXT("debug"), ESearchCase::IgnoreCase) == 0)
		{
			WorkerSDKLogLevel = WORKER_LOG_LEVEL_DEBUG;
		}
		else if (LogLevelString.Compare(TEXT("info"), ESearchCase::IgnoreCase) == 0)
		{
			WorkerSDKLogLevel = WORKER_LOG_LEVEL_INFO;
		}
		else if (LogLevelString.Compare(TEXT("warning"), ESearchCase::IgnoreCase) == 0)
		{
			WorkerSDKLogLevel = WORKER_LOG_LEVEL_WARN;
		}
		else if (LogLevelString.Compare(TEXT("error"), ESearchCase::IgnoreCase) == 0)
		{
			WorkerSDKLogLevel = WORKER_LOG_LEVEL_ERROR;
		}
		else if (!LogLevelString.IsEmpty())
		{
			UE_LOG(LogConnectionConfig, Warning, TEXT("Unknown worker SDK log verbosity %s specified. Defaulting to Info."),
				   *LogLevelString);
		}
	}

	void GetLinkProtocol(const TCHAR* CommandLine)
	{
		FString LinkProtocolString;
		FParse::Value(CommandLine, TEXT("linkProtocol"), LinkProtocolString);
		if (LinkProtocolString.Compare(TEXT("Tcp"), ESearchCase::IgnoreCase) == 0)
		{
			ConnectionTypeMap[EWorkerType::Client] = WORKER_NETWORK_CONNECTION_TYPE_TCP;
			ConnectionTypeMap[EWorkerType::Server] = WORKER_NETWORK_CONNECTION_TYPE_TCP;
			return;
		}
		else if (LinkProtocolString.Compare(TEXT("Kcp"), ESearchCase::IgnoreCase) == 0)
		{
			ConnectionTypeMap[EWorkerType::Client] = WORKER_NETWORK_CONNECTION_TYPE_KCP;
			ConnectionTypeMap[EWorkerType::Server] = WORKER_NETWORK_CONNECTION_TYPE_KCP;
			return;
		}

		if (!LinkProtocolString.IsEmpty())
		{
			UE_LOG(LogConnectionConfig, Warning, TEXT("Unknown network protocol '%s' specified for connecting to SpatialOS."),
				   *LinkProtocolString);
		}

		ConnectionTypeMap[EWorkerType::Client] = WORKER_NETWORK_CONNECTION_TYPE_KCP;
		ConnectionTypeMap[EWorkerType::Server] = WORKER_NETWORK_CONNECTION_TYPE_TCP;

		UE_LOG(LogConnectionConfig, Verbose, TEXT("No link protocol set. Defaulting to TCP for server workers, KCP for client workers."));
	}

public:
	FString WorkerId;
	FString WorkerType;
	bool UseExternalIp;
	bool EnableWorkerSDKProtocolLogging;
	bool EnableWorkerSDKOpLogging;
	FString WorkerSDKLogPrefix;
	uint32 WorkerSDKLogFileSize;
	Worker_LogLevel WorkerSDKLogLevel;
	Worker_NetworkConnectionType LinkProtocol;
	Worker_NetworkConnectionType ConnectionTypeMap[2];
	Worker_ConnectionParameters ConnectionParams = {};
	uint8 TcpMultiplexLevel;
	uint8 TcpNoDelay;
	uint8 UdpUpstreamIntervalMS;
	uint8 UdpDownstreamIntervalMS;
	uint32 DownstreamWindowSizeBytes;
	uint32 UpstreamWindowSizeBytes;
};

class FLocatorConfig : public FConnectionConfig
{
public:
	FLocatorConfig() { LoadDefaults(); }

	void LoadDefaults()
	{
		UseExternalIp = true;

		if (GetDefault<USpatialGDKSettings>()->IsRunningInChina())
		{
			LocatorHost = SpatialConstants::LOCATOR_HOST_CN;
		}
		else
		{
			LocatorHost = SpatialConstants::LOCATOR_HOST;
		}

		LocatorPort = SpatialConstants::LOCATOR_PORT;
	}

	bool TryLoadCommandLineArgs()
	{
		bool bSuccess = true;
		const TCHAR* CommandLine = FCommandLine::Get();
		FParse::Value(CommandLine, TEXT("locatorHost"), LocatorHost);
		bSuccess &= FParse::Value(CommandLine, TEXT("playerIdentityToken"), PlayerIdentityToken);
		bSuccess &= FParse::Value(CommandLine, TEXT("loginToken"), LoginToken);
		return bSuccess;
	}

	FString LocatorHost;
	int32 LocatorPort;
	FString PlayerIdentityToken;
	FString LoginToken;
};

class FDevAuthConfig : public FLocatorConfig
{
public:
	FDevAuthConfig() { LoadDefaults(); }

	void LoadDefaults()
	{
		UseExternalIp = true;
		PlayerId = SpatialConstants::DEVELOPMENT_AUTH_PLAYER_ID;

		if (GetDefault<USpatialGDKSettings>()->IsRunningInChina())
		{
			LocatorHost = SpatialConstants::LOCATOR_HOST_CN;
		}
		else
		{
			LocatorHost = SpatialConstants::LOCATOR_HOST;
		}

		LocatorPort = SpatialConstants::LOCATOR_PORT;
	}

	bool TryLoadCommandLineArgs()
	{
		const TCHAR* CommandLine = FCommandLine::Get();
		FParse::Value(CommandLine, TEXT("locatorHost"), LocatorHost);
		FParse::Value(CommandLine, TEXT("deployment"), Deployment);
		FParse::Value(CommandLine, TEXT("playerId"), PlayerId);
		FParse::Value(CommandLine, TEXT("displayName"), DisplayName);
		FParse::Value(CommandLine, TEXT("metaData"), MetaData);
		const bool bSuccess = FParse::Value(CommandLine, TEXT("devAuthToken"), DevelopmentAuthToken);
		return bSuccess;
	}

	FString DevelopmentAuthToken;
	FString Deployment;
	FString PlayerId;
	FString DisplayName;
	FString MetaData;
};

class FReceptionistConfig : public FConnectionConfig
{
public:
	FReceptionistConfig() { LoadDefaults(); }

	void LoadDefaults()
	{
		UseExternalIp = false;
		ReceptionistPort = SpatialConstants::DEFAULT_PORT;
		SetReceptionistHost(GetDefault<USpatialGDKSettings>()->DefaultReceptionistHost);
	}

	bool TryLoadCommandLineArgs()
	{
		const TCHAR* CommandLine = FCommandLine::Get();

		// Get command line options first since the URL handling will modify the CommandLine string
		uint16 Port;
		bool bReceptionistPortParsed = FParse::Value(CommandLine, TEXT("receptionistPort"), Port);
		FParse::Bool(CommandLine, *SpatialConstants::URL_USE_EXTERNAL_IP_FOR_BRIDGE_OPTION, UseExternalIp);

		// Parse the command line for receptionistHost, if it exists then use this as the host IP.
		FString Host;
		if (!FParse::Value(CommandLine, TEXT("receptionistHost"), Host))
		{
			// If a receptionistHost is not specified then parse for an IP address as the first argument and use this instead.
			// This is how native Unreal handles connecting to other IPs, a map name can also be specified, in this case we use the default
			// IP.
			FString URLAddress;
			FParse::Token(CommandLine, URLAddress, false /* UseEscape */);
			const FURL URL(nullptr /* Base */, *URLAddress, TRAVEL_Absolute);
			if (URL.Valid && !URLAddress.IsEmpty())
			{
				SetupFromURL(URL);
			}
			else if (!bReceptionistPortParsed)
			{
				return false;
			}
		}
		else
		{
			SetReceptionistHost(Host);
		}
		// If the ReceptionistPort was parsed in the command-line arguments, it would be overwritten by the URL setup above.
		// So we restore/set it here.
		if (bReceptionistPortParsed)
		{
			SetReceptionistPort(Port);
		}

		return true;
	}

	void SetupFromURL(const FURL& URL)
	{
		if (!URL.Host.IsEmpty())
		{
			SetReceptionistHost(URL.Host);
			SetReceptionistPort(URL.Port);
		}
		if (URL.HasOption(*SpatialConstants::URL_USE_EXTERNAL_IP_FOR_BRIDGE_OPTION))
		{
			UseExternalIp = true;
		}
	}

	FString GetReceptionistHost() const { return ReceptionistHost; }

	uint16 GetReceptionistPort() const { return ReceptionistPort; }

private:
	void SetReceptionistHost(const FString& Host)
	{
		if (!Host.IsEmpty())
		{
			ReceptionistHost = Host;
		}
	}

	void SetReceptionistPort(const uint16 Port) { ReceptionistPort = Port; }

	FString ReceptionistHost;

	uint16 ReceptionistPort;
};
