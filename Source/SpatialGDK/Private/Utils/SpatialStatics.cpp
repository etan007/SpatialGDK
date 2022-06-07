// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "Utils/SpatialStatics.h"

#include "Engine/World.h"
#include "EngineClasses/SpatialGameInstance.h"
#include "EngineClasses/SpatialNetDriver.h"
#include "EngineClasses/SpatialPackageMapClient.h"
#include "EngineClasses/SpatialWorldSettings.h"
#include "GeneralProjectSettings.h"
#include "Interop/SpatialWorkerFlags.h"
#include "Kismet/KismetSystemLibrary.h"
#include "LoadBalancing/GameplayDebuggerLBStrategy.h"
#include "LoadBalancing/LayeredLBStrategy.h"
#include "LoadBalancing/SpatialMultiWorkerSettings.h"
#include "SpatialConstants.h"
#include "SpatialGDKSettings.h"
#include "Utils/InspectionColors.h"

DEFINE_LOG_CATEGORY(LogSpatial);

namespace
{
bool CanProcessActor(const AActor* Actor)
{
	if (Actor == nullptr)
	{
		UE_LOG(LogSpatial, Error, TEXT("Calling locking API functions on nullptr Actor is invalid."));
		return false;
	}

	const UNetDriver* NetDriver = Actor->GetWorld()->GetNetDriver();
	if (!NetDriver->IsServer())
	{
		UE_LOG(LogSpatial, Error, TEXT("Calling locking API functions on a client is invalid. Actor: %s"), *GetNameSafe(Actor));
		return false;
	}

	if (!Actor->HasAuthority())
	{
		UE_LOG(LogSpatial, Error, TEXT("Calling locking API functions on a non-auth Actor is invalid. Actor: %s."), *GetNameSafe(Actor));
		return false;
	}

	return true;
}

const ULayeredLBStrategy* GetLayeredLBStrategy(const USpatialNetDriver* NetDriver)
{
	if (const ULayeredLBStrategy* LayeredLBStrategy = Cast<ULayeredLBStrategy>(NetDriver->LoadBalanceStrategy))
	{
		return LayeredLBStrategy;
	}
	if (const UGameplayDebuggerLBStrategy* DebuggerLBStrategy = Cast<UGameplayDebuggerLBStrategy>(NetDriver->LoadBalanceStrategy))
	{
		if (const ULayeredLBStrategy* LayeredLBStrategy = Cast<ULayeredLBStrategy>(DebuggerLBStrategy->GetWrappedStrategy()))
		{
			return LayeredLBStrategy;
		}
	}
	return nullptr;
}
} // anonymous namespace

bool USpatialStatics::IsSpatialNetworkingEnabled()
{
	return GetDefault<UGeneralProjectSettings>()->UsesSpatialNetworking();
}

bool USpatialStatics::IsHandoverEnabled(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject->GetWorld();
	if (World == nullptr)
	{
		return true;
	}

	if (World->IsNetMode(NM_Client))
	{
		return true;
	}

	if (const USpatialNetDriver* SpatialNetDriver = Cast<USpatialNetDriver>(World->GetNetDriver()))
	{
		// Calling IsHandoverEnabled before NotifyBeginPlay has been called (when NetDriver is ready) is invalid.
		if (!SpatialNetDriver->IsReady())
		{
			UE_LOG(LogSpatial, Error,
				   TEXT("Called IsHandoverEnabled before NotifyBeginPlay has been called is invalid. Returning enabled."));
			return true;
		}

		return SpatialNetDriver->LoadBalanceStrategy->RequiresHandoverData();
	}
	return true;
}

FName USpatialStatics::GetCurrentWorkerType(const UObject* WorldContext)
{
	if (const UWorld* World = WorldContext->GetWorld())
	{
		if (const UGameInstance* GameInstance = World->GetGameInstance())
		{
			return GameInstance->GetSpatialWorkerType();
		}
	}

	return NAME_None;
}

bool USpatialStatics::GetWorkerFlag(const UObject* WorldContext, const FString& InFlagName, FString& OutFlagValue)
{
	if (const UWorld* World = WorldContext->GetWorld())
	{
		if (const USpatialNetDriver* SpatialNetDriver = Cast<USpatialNetDriver>(World->GetNetDriver()))
		{
			if (const USpatialWorkerFlags* SpatialWorkerFlags = SpatialNetDriver->SpatialWorkerFlags)
			{
				return SpatialWorkerFlags->GetWorkerFlag(InFlagName, OutFlagValue);
			}
		}
	}

	return false;
}

TArray<FDistanceFrequencyPair> USpatialStatics::GetNCDDistanceRatios()
{
	return GetDefault<USpatialGDKSettings>()->InterestRangeFrequencyPairs;
}

float USpatialStatics::GetFullFrequencyNetCullDistanceRatio()
{
	return GetDefault<USpatialGDKSettings>()->FullFrequencyNetCullDistanceRatio;
}

FColor USpatialStatics::GetInspectorColorForWorkerName(const FString& WorkerName)
{
	return SpatialGDK::GetColorForWorkerName(WorkerName);
}

bool USpatialStatics::IsMultiWorkerEnabled()
{
	const USpatialGDKSettings* SpatialGDKSettings = GetDefault<USpatialGDKSettings>();

	// Check if multi-worker settings class was overridden from the command line
	if (SpatialGDKSettings->OverrideMultiWorkerSettingsClass.IsSet())
	{
		// If command line override for Multi Worker Settings is set then enable multi-worker.
		return true;
	}
#if WITH_EDITOR
	else if (!SpatialGDKSettings->IsMultiWorkerEditorEnabled())
	{
		// If  multi-worker is not enabled in editor then disable multi-worker.
		return false;
	}
#endif // WITH_EDITOR
	return true;
}

TSubclassOf<UAbstractSpatialMultiWorkerSettings> USpatialStatics::GetSpatialMultiWorkerClass(const UObject* WorldContextObject,
																							 bool bForceNonEditorSettings)
{
	checkf(WorldContextObject != nullptr, TEXT("Called GetSpatialMultiWorkerClass with a nullptr WorldContextObject*"));

	const UWorld* World = WorldContextObject->GetWorld();
	checkf(World != nullptr, TEXT("Called GetSpatialMultiWorkerClass with a nullptr World*"));

	if (ASpatialWorldSettings* WorldSettings = Cast<ASpatialWorldSettings>(World->GetWorldSettings()))
	{
		return WorldSettings->GetMultiWorkerSettingsClass(bForceNonEditorSettings);
	}
	return USpatialMultiWorkerSettings::StaticClass();
}

bool USpatialStatics::IsSpatialOffloadingEnabled(const UWorld* World)
{
	if (World != nullptr)
	{
		if (const ASpatialWorldSettings* WorldSettings = Cast<ASpatialWorldSettings>(World->GetWorldSettings()))
		{
			if (!IsMultiWorkerEnabled())
			{
				return false;
			}

			const UAbstractSpatialMultiWorkerSettings* MultiWorkerSettings =
				USpatialStatics::GetSpatialMultiWorkerClass(World)->GetDefaultObject<UAbstractSpatialMultiWorkerSettings>();
			return MultiWorkerSettings->WorkerLayers.Num() > 1;
		}
	}

	return false;
}

bool USpatialStatics::IsActorGroupOwnerForActor(const AActor* Actor)
{
	if (Actor == nullptr)
	{
		return false;
	}

	// Offloading using the Unreal Load Balancing always load balances based on the owning actor.
	const AActor* RootOwner = Actor;
	while (RootOwner->GetOwner() != nullptr && RootOwner->GetOwner()->GetIsReplicated())
	{
		RootOwner = RootOwner->GetOwner();
	}

	return IsActorGroupOwnerForClass(RootOwner, RootOwner->GetClass());
}

bool USpatialStatics::IsActorGroupOwnerForClass(const UObject* WorldContextObject, const TSubclassOf<AActor> ActorClass)
{
	const UWorld* World = WorldContextObject->GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	if (World->IsNetMode(NM_Client))
	{
		return false;
	}

	if (const USpatialNetDriver* SpatialNetDriver = Cast<USpatialNetDriver>(World->GetNetDriver()))
	{
		// Calling IsActorGroupOwnerForClass before NotifyBeginPlay has been called (when NetDriver is ready) is invalid.
		if (!SpatialNetDriver->IsReady())
		{
			UE_LOG(LogSpatial, Error,
				   TEXT("Called IsActorGroupOwnerForClass before NotifyBeginPlay has been called is invalid. Actor class: %s"),
				   *GetNameSafe(ActorClass));
			return true;
		}

		if (const ULayeredLBStrategy* LBStrategy = GetLayeredLBStrategy(SpatialNetDriver))
		{
			return LBStrategy->CouldHaveAuthority(ActorClass);
		}
	}
	return true;
}

void USpatialStatics::PrintStringSpatial(UObject* WorldContextObject, const FString& InString /*= FString(TEXT("Hello"))*/,
										 bool bPrintToScreen /*= true*/, FLinearColor TextColor /*= FLinearColor(0.0, 0.66, 1.0)*/,
										 float Duration /*= 2.f*/)
{
	// This will be logged in the SpatialOutput so we don't want to double log this, therefore bPrintToLog is false.
	UKismetSystemLibrary::PrintString(WorldContextObject, InString, bPrintToScreen, false /*bPrintToLog*/, TextColor, Duration);

	// By logging to LogSpatial we will print to the spatial os runtime.
	UE_LOG(LogSpatial, Log, TEXT("%s"), *InString);
}

void USpatialStatics::PrintTextSpatial(UObject* WorldContextObject, const FText InText /*= INVTEXT("Hello")*/,
									   bool bPrintToScreen /*= true*/, FLinearColor TextColor /*= FLinearColor(0.0, 0.66, 1.0)*/,
									   float Duration /*= 2.f*/)
{
	PrintStringSpatial(WorldContextObject, InText.ToString(), bPrintToScreen, TextColor, Duration);
}

int64 USpatialStatics::GetActorEntityId(const AActor* Actor)
{
	if (Actor == nullptr)
	{
		return SpatialConstants::INVALID_ENTITY_ID;
	}

	if (const USpatialNetDriver* SpatialNetDriver = Cast<USpatialNetDriver>(Actor->GetNetDriver()))
	{
		return static_cast<int64>(SpatialNetDriver->PackageMap->GetEntityIdFromObject(Actor));
	}

	return SpatialConstants::INVALID_ENTITY_ID;
}

FString USpatialStatics::EntityIdToString(int64 EntityId)
{
	if (EntityId <= SpatialConstants::INVALID_ENTITY_ID)
	{
		return FString("Invalid");
	}

	return FString::Printf(TEXT("%lld"), EntityId);
}

FString USpatialStatics::GetActorEntityIdAsString(const AActor* Actor)
{
	return EntityIdToString(GetActorEntityId(Actor));
}

FLockingToken USpatialStatics::AcquireLock(AActor* Actor, const FString& DebugString)
{
	if (!CanProcessActor(Actor) || !IsMultiWorkerEnabled())
	{
		return FLockingToken{ SpatialConstants::INVALID_ACTOR_LOCK_TOKEN };
	}

	UAbstractLockingPolicy* LockingPolicy = Cast<USpatialNetDriver>(Actor->GetWorld()->GetNetDriver())->LockingPolicy;

	const ActorLockToken LockToken = LockingPolicy->AcquireLock(Actor, DebugString);

	UE_LOG(LogSpatial, Verbose, TEXT("LockingComponent called AcquireLock. Actor: %s. Token: %lld. New lock count: %d"), *Actor->GetName(),
		   LockToken, LockingPolicy->GetActorLockCount(Actor));

	return FLockingToken{ LockToken };
}

bool USpatialStatics::IsLocked(const AActor* Actor)
{
	if (!CanProcessActor(Actor) || !IsMultiWorkerEnabled())
	{
		return false;
	}

	return Cast<USpatialNetDriver>(Actor->GetWorld()->GetNetDriver())->LockingPolicy->IsLocked(Actor);
}

void USpatialStatics::ReleaseLock(const AActor* Actor, FLockingToken LockToken)
{
	if (!CanProcessActor(Actor) || !IsMultiWorkerEnabled())
	{
		return;
	}

	UAbstractLockingPolicy* LockingPolicy = Cast<USpatialNetDriver>(Actor->GetWorld()->GetNetDriver())->LockingPolicy;
	LockingPolicy->ReleaseLock(LockToken.Token);

	UE_LOG(LogSpatial, Verbose, TEXT("LockingComponent called ReleaseLock. Actor: %s. Token: %lld. Resulting lock count: %d"),
		   *Actor->GetName(), LockToken.Token, LockingPolicy->GetActorLockCount(Actor));
}

FName USpatialStatics::GetLayerName(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject->GetWorld();
	if (World == nullptr)
	{
		UE_LOG(LogSpatial, Error, TEXT("World was nullptr when calling GetLayerName"));
		return NAME_None;
	}

	if (World->IsNetMode(NM_Client))
	{
		return SpatialConstants::DefaultClientWorkerType;
	}

	if (!IsSpatialNetworkingEnabled())
	{
		return SpatialConstants::DefaultLayer;
	}

	const USpatialNetDriver* SpatialNetDriver = Cast<USpatialNetDriver>(World->GetNetDriver());
	if (SpatialNetDriver == nullptr || !SpatialNetDriver->IsReady())
	{
		UE_LOG(LogSpatial, Error,
			   TEXT("Called GetLayerName before NotifyBeginPlay has been called is invalid. Worker doesn't know its layer yet"));
		return NAME_None;
	}

	const ULayeredLBStrategy* LBStrategy = GetLayeredLBStrategy(SpatialNetDriver);
	if (!ensureAlwaysMsgf(LBStrategy != nullptr, TEXT("Failed calling GetLayerName because load balancing strategy was nullptr")))
	{
		return FName();
	}

	return LBStrategy->GetLocalLayerName();
}

int64 USpatialStatics::GetMaxDynamicallyAttachedSubobjectsPerClass()
{
	return GetDefault<USpatialGDKSettings>()->MaxDynamicallyAttachedSubobjectsPerClass;
}

void USpatialStatics::SpatialDebuggerSetOnConfigUIClosedCallback(const UObject* WorldContextObject, FOnConfigUIClosedDelegate Delegate)
{
	const UWorld* World = WorldContextObject->GetWorld();
	if (World == nullptr)
	{
		UE_LOG(LogSpatial, Error, TEXT("World was nullptr when calling SpatialDebuggerSetOnConfigUIClosedCallback"));
		return;
	}

	if (World->GetNetMode() != NM_Client)
	{
		UE_LOG(LogSpatial, Warning,
			   TEXT("SpatialDebuggerSetOnConfigUIClosedCallback should only be called on clients. It has no effects on servers."));
		return;
	}

	const USpatialNetDriver* SpatialNetDriver = Cast<USpatialNetDriver>(World->GetNetDriver());
	if (SpatialNetDriver == nullptr)
	{
		UE_LOG(LogSpatial, Error, TEXT("No spatial net driver found when calling SpatialDebuggerSetOnConfigUIClosedCallback"));
		return;
	}

	SpatialNetDriver->SpatialDebuggerReady->Await(FOnReady::CreateLambda([SpatialNetDriver, Delegate](const FString& ErrorMessage) {
		if (!ErrorMessage.IsEmpty())
		{
			UE_LOG(LogSpatial, Error, TEXT("Couldn't set config ui closed callback due to error: %s"), *ErrorMessage);
			return;
		}

		SpatialNetDriver->SpatialDebugger->OnConfigUIClosed = Delegate;
	}));
}

void USpatialStatics::SpatialSwitchHasAuthority(const AActor* Target, ESpatialHasAuthority& Authority)
{
	if (!ensureAlwaysMsgf(IsValid(Target) && Target->IsA(AActor::StaticClass()),
						  TEXT("Called SpatialSwitchHasAuthority for an invalid or non-Actor target: %s"), *GetNameSafe(Target)))
	{
		return;
	}

	if (!ensureAlwaysMsgf(Target->GetNetDriver() != nullptr,
						  TEXT("Called SpatialSwitchHasAuthority for %s but couldn't access NetDriver through Actor."),
						  *GetNameSafe(Target)))
	{
		return;
	}

	// A static UFunction does not have the Target parameter, here it is recreated by adding our own Target parameter
	// that is defaulted to self and hidden so that the user does not need to set it
	const bool bIsServer = Target->GetNetDriver()->IsServer();
	const bool bHasAuthority = Target->HasAuthority();

	if (bHasAuthority && bIsServer)
	{
		Authority = ESpatialHasAuthority::ServerAuth;
	}
	else if (!bHasAuthority && bIsServer)
	{
		Authority = ESpatialHasAuthority::ServerNonAuth;
	}
	else if (bHasAuthority && !bIsServer)
	{
		Authority = ESpatialHasAuthority::ClientAuth;
	}
	else
	{
		Authority = ESpatialHasAuthority::ClientNonAuth;
	}
}
