// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "EngineClasses/SpatialGameInstance.h"

#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "GeneralProjectSettings.h"
#include "Misc/Guid.h"

#if WITH_EDITOR
#include "Editor/EditorEngine.h"
#include "Settings/LevelEditorPlaySettings.h"
#endif

#include "Kismet/GameplayStatics.h"

#include "EngineClasses/SpatialNetDriver.h"
#include "EngineClasses/SpatialPendingNetGame.h"
#include "Interop/Connection/SpatialConnectionManager.h"
#include "Interop/Connection/SpatialWorkerConnection.h"
#include "Interop/GlobalStateManager.h"
#include "Interop/SpatialWorkerFlags.h"
#include "SpatialConstants.h"
#include "Utils/SpatialDebugger.h"
#include "Utils/SpatialLatencyTracer.h"
#include "Utils/SpatialMetrics.h"
#include "Utils/SpatialMetricsDisplay.h"
#include "Utils/SpatialStatics.h"

DEFINE_LOG_CATEGORY(LogSpatialGameInstance);

USpatialGameInstance::USpatialGameInstance()
	: Super()
	, bIsSpatialNetDriverReady(false)
	, bPreparingForShutdown(false)
{
}

bool USpatialGameInstance::HasSpatialNetDriver() const
{
	bool bHasSpatialNetDriver = false;

	const bool bUseSpatial = GetDefault<UGeneralProjectSettings>()->UsesSpatialNetworking();

	if (WorldContext != nullptr)
	{
		UWorld* World = GetWorld();
		UNetDriver* NetDriver = GEngine->FindNamedNetDriver(World, NAME_PendingNetDriver);
		bool bShouldDestroyNetDriver = false;

		if (NetDriver == nullptr)
		{
			// If Spatial networking is enabled, override the GameNetDriver with the SpatialNetDriver
			if (bUseSpatial)
			{
				if (FNetDriverDefinition* DriverDefinition =
						GEngine->NetDriverDefinitions.FindByPredicate([](const FNetDriverDefinition& CurDef) {
							return CurDef.DefName == NAME_GameNetDriver;
						}))
				{
					DriverDefinition->DriverClassName = DriverDefinition->DriverClassNameFallback =
						TEXT("/Script/SpatialGDK.SpatialNetDriver");
				}
			}

			bShouldDestroyNetDriver = GEngine->CreateNamedNetDriver(World, NAME_PendingNetDriver, NAME_GameNetDriver);
			NetDriver = GEngine->FindNamedNetDriver(World, NAME_PendingNetDriver);
		}

		if (NetDriver != nullptr)
		{
			bHasSpatialNetDriver = NetDriver->IsA<USpatialNetDriver>();

			if (bShouldDestroyNetDriver)
			{
				GEngine->DestroyNamedNetDriver(World, NAME_PendingNetDriver);
			}
		}
	}

	if (bUseSpatial && !bHasSpatialNetDriver)
	{
		UE_LOG(LogSpatialGameInstance, Error,
			   TEXT("Could not find SpatialNetDriver even though Spatial networking is switched on! "
					"Please make sure you set up the net driver definitions as specified in the porting "
					"guide and that you don't override the main net driver."));
	}

	return bHasSpatialNetDriver;
}

void USpatialGameInstance::CreateNewSpatialConnectionManager()
{
	SpatialConnectionManager = NewObject<USpatialConnectionManager>(this);

	GlobalStateManager = NewObject<UGlobalStateManager>();
}

void USpatialGameInstance::DestroySpatialConnectionManager()
{
	if (GlobalStateManager != nullptr)
	{
		GlobalStateManager->ConditionalBeginDestroy();
		GlobalStateManager = nullptr;
	}

	if (SpatialConnectionManager != nullptr)
	{
		SpatialConnectionManager->DestroyConnection();
		SpatialConnectionManager = nullptr;
	}
}

#if WITH_EDITOR
FGameInstancePIEResult USpatialGameInstance::StartPlayInEditorGameInstance(ULocalPlayer* LocalPlayer,
																		   const FGameInstancePIEParameters& Params)
{
	SpatialWorkerType = Params.SpatialWorkerType;
	bIsSimulatedPlayer = Params.bIsSimulatedPlayer;

	StartSpatialConnection();
	return Super::StartPlayInEditorGameInstance(LocalPlayer, Params);
}
#endif

void USpatialGameInstance::StartSpatialConnection()
{
	if (HasSpatialNetDriver())
	{
		// If we are using spatial networking then prepare a spatial connection.
		TryInjectSpatialLocatorIntoCommandLine();
		CreateNewSpatialConnectionManager();
	}
#if TRACE_LIB_ACTIVE
	else
	{
		// In native, setup worker name here as we don't get a HandleOnConnected() callback
		FString WorkerName =
			FString::Printf(TEXT("%s:%s"), *SpatialWorkerType.ToString(), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
		SpatialLatencyTracer->SetWorkerId(WorkerName);
	}
#endif
}

void USpatialGameInstance::TryInjectSpatialLocatorIntoCommandLine()
{
	if (!HasPreviouslyConnectedToSpatial())
	{
		SetHasPreviouslyConnectedToSpatial();
		// Native Unreal creates a NetDriver and attempts to automatically connect if a Host is specified as the first commandline argument.
		// Since the SpatialOS Launcher does not specify this, we need to check for a locator loginToken to allow automatic connection to
		// provide parity with native.

		// Initialize a locator configuration which will parse command line arguments.
		FLocatorConfig LocatorConfig;
		if (LocatorConfig.TryLoadCommandLineArgs())
		{
			// Modify the commandline args to have a Host IP to force a NetDriver to be used.
			const TCHAR* CommandLineArgs = FCommandLine::Get();

			FString NewCommandLineArgs = LocatorConfig.LocatorHost + TEXT(" ");
			NewCommandLineArgs.Append(FString(CommandLineArgs));

			FCommandLine::Set(*NewCommandLineArgs);
		}
	}
}

void USpatialGameInstance::StartGameInstance()
{
	if (GetDefault<USpatialGDKSettings>()->GetPreventClientCloudDeploymentAutoConnect())
	{
		DisableShouldConnectUsingCommandLineArgs();
	}
	else
	{
		StartSpatialConnection();
	}

	Super::StartGameInstance();
}

bool USpatialGameInstance::ProcessConsoleExec(const TCHAR* Cmd, FOutputDevice& Ar, UObject* Executor)
{
	if (Super::ProcessConsoleExec(Cmd, Ar, Executor))
	{
		return true;
	}

	if (const UWorld* World = GetWorld())
	{
		if (const USpatialNetDriver* NetDriver = Cast<USpatialNetDriver>(World->GetNetDriver()))
		{
			if (NetDriver->SpatialMetrics && NetDriver->SpatialMetrics->ProcessConsoleExec(Cmd, Ar, Executor))
			{
				return true;
			}

			if (NetDriver->SpatialMetricsDisplay && NetDriver->SpatialMetricsDisplay->ProcessConsoleExec(Cmd, Ar, Executor))
			{
				return true;
			}

			if (NetDriver->SpatialDebugger && NetDriver->SpatialDebugger->ProcessConsoleExec(Cmd, Ar, Executor))
			{
				return true;
			}
		}
	}

	return false;
}

namespace
{
constexpr uint8 SimPlayerErrorExitCode = 10;

void HandleOnSimulatedPlayerNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type NetworkFailureType,
										   const FString& Reason)
{
	UE_LOG(LogSpatialGameInstance, Log, TEXT("SimulatedPlayer network failure due to: %s"), *Reason);

	FPlatformMisc::RequestExitWithStatus(/*bForce =*/false, SimPlayerErrorExitCode);
}
} // namespace

void USpatialGameInstance::Init()
{
	Super::Init();

	if (UGameplayStatics::HasLaunchOption(TEXT("FailOnNetworkFailure")))
	{
		GetEngine()->OnNetworkFailure().AddStatic(&HandleOnSimulatedPlayerNetworkFailure);
	}

	SpatialLatencyTracer = NewObject<USpatialLatencyTracer>(this);

	if (HasSpatialNetDriver())
	{
		FWorldDelegates::LevelInitializedNetworkActors.AddUObject(this, &USpatialGameInstance::OnLevelInitializedNetworkActors);
	}
}

void USpatialGameInstance::HandleOnConnected(USpatialNetDriver& NetDriver)
{
	UE_LOG(LogSpatialGameInstance, Log, TEXT("Successfully connected to SpatialOS"));
	SetSpatialWorkerId(SpatialConnectionManager->GetWorkerConnection()->GetWorkerId());
#if TRACE_LIB_ACTIVE
	SpatialLatencyTracer->SetWorkerId(GetSpatialWorkerId());
#endif

	OnSpatialConnected.Broadcast();

	if (NetDriver.IsServer())
	{
		FOnWorkerFlagUpdatedBP WorkerFlagDelegate;
		WorkerFlagDelegate.BindDynamic(this, &USpatialGameInstance::HandlePrepareShutdownWorkerFlagUpdated);

		NetDriver.SpatialWorkerFlags->RegisterFlagUpdatedCallback(SpatialConstants::SHUTDOWN_PREPARATION_WORKER_FLAG, WorkerFlagDelegate);
	}
	NetDriver.OnShutdown.AddUObject(this, &USpatialGameInstance::DestroySpatialConnectionManager);
}

void USpatialGameInstance::HandlePrepareShutdownWorkerFlagUpdated(const FString& FlagName, const FString& FlagValue)
{
	if (!bPreparingForShutdown)
	{
		bPreparingForShutdown = true;
		UE_LOG(LogSpatialGameInstance, Log, TEXT("Shutdown preparation triggered."));
		OnPrepareShutdown.Broadcast();
	}
}

void USpatialGameInstance::HandleOnConnectionFailed(const FString& Reason)
{
	UE_LOG(LogSpatialGameInstance, Error, TEXT("Could not connect to SpatialOS. Reason: %s"), *Reason);
#if TRACE_LIB_ACTIVE
	SpatialLatencyTracer->ResetWorkerId();
#endif
	OnSpatialConnectionFailed.Broadcast(Reason);
}

void USpatialGameInstance::HandleOnPlayerSpawnFailed(const FString& Reason)
{
	UE_LOG(LogSpatialGameInstance, Error, TEXT("Could not spawn the local player on SpatialOS. Reason: %s"), *Reason);
	OnSpatialPlayerSpawnFailed.Broadcast(Reason);
}

void USpatialGameInstance::OnLevelInitializedNetworkActors(ULevel* LoadedLevel, UWorld* OwningWorld) const
{
	UE_LOG(LogSpatialOSNetDriver, Log, TEXT("OnLevelInitializedNetworkActors: Level (%s) OwningWorld (%s) World (%s)"),
		   *GetNameSafe(LoadedLevel), *GetNameSafe(OwningWorld), *GetNameSafe(OwningWorld));

	if (OwningWorld != GetWorld() || !OwningWorld->IsServer() || OwningWorld->GetNetDriver() == nullptr
		|| !Cast<USpatialNetDriver>(OwningWorld->GetNetDriver())->IsReady()
		|| (OwningWorld->WorldType != EWorldType::PIE && OwningWorld->WorldType != EWorldType::Game
			&& OwningWorld->WorldType != EWorldType::GamePreview))
	{
		// We only want to do something if this is the correct process and we are on a spatial server, and we are in-game
		return;
	}

	for (AActor* Actor : LoadedLevel->Actors)
	{
		GlobalStateManager->HandleActorBasedOnLoadBalancer(Actor);
	}
}
