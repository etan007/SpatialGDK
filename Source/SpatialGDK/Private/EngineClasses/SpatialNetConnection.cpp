// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "EngineClasses/SpatialNetConnection.h"

#include "EngineClasses/SpatialNetDriver.h"
#include "EngineClasses/SpatialPackageMapClient.h"
#include "Interop/ActorSystem.h"
#include "Interop/ClientConnectionManager.h"
#include "Interop/Connection/SpatialConnectionManager.h"
#include "Interop/Connection/SpatialWorkerConnection.h"
#include "Interop/SpatialReceiver.h"
#include "Interop/SpatialSender.h"
#include "SpatialConstants.h"
#include "SpatialGDKSettings.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

#include <WorkerSDK/improbable/c_schema.h>

DEFINE_LOG_CATEGORY(LogSpatialNetConnection);

DECLARE_CYCLE_STAT(TEXT("UpdateLevelVisibility"), STAT_SpatialNetConnectionUpdateLevelVisibility, STATGROUP_SpatialNet);

USpatialNetConnection::USpatialNetConnection(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bReliableSpatialConnection(false)
	, ConnectionClientWorkerSystemEntityId(SpatialConstants::INVALID_ENTITY_ID)
{
#if ENGINE_MINOR_VERSION <= 24
	InternalAck = 1;
#else
	SetInternalAck(true);
#endif
}

void USpatialNetConnection::CleanUp()
{
	if (USpatialNetDriver* SpatialNetDriver = Cast<USpatialNetDriver>(Driver))
	{
		SpatialNetDriver->ClientConnectionManager->CleanUpClientConnection(this);
	}

	Super::CleanUp();
}

void USpatialNetConnection::InitBase(UNetDriver* InDriver, class FSocket* InSocket, const FURL& InURL, EConnectionState InState,
									 int32 InMaxPacket /*= 0*/, int32 InPacketOverhead /*= 0*/)
{
	Super::InitBase(InDriver, InSocket, InURL, InState, InMaxPacket, InPacketOverhead);

	if (Cast<USpatialNetDriver>(InDriver)->PackageMap == nullptr)
	{
		// This should only happen if we're setting up the special "SpatialOS" connection.
		auto PackageMapClient = NewObject<USpatialPackageMapClient>(this);
		PackageMapClient->Initialize(this, InDriver->GuidCache);
		PackageMap = PackageMapClient;
		Cast<USpatialNetDriver>(InDriver)->PackageMap = PackageMapClient;
	}
	else
	{
		PackageMap = Cast<USpatialNetDriver>(InDriver)->PackageMap;
	}
}

void USpatialNetConnection::LowLevelSend(void* Data, int32 CountBits, FOutPacketTraits& Traits)
{
	// Intentionally does not call Super::
}

bool USpatialNetConnection::ClientHasInitializedLevelFor(const AActor* TestActor) const
{
	ensureAlwaysMsgf(Driver->IsServer(), TEXT("ClientHasInitializedLevelFor should only be called on servers. Actor %s"),
					 *GetNameSafe(TestActor));
	return true;
	// Intentionally does not call Super::
}

int32 USpatialNetConnection::IsNetReady(bool Saturate)
{
	// TODO: UNR-664 - Currently we do not report the number of bits sent when replicating, this means channel saturation cannot be checked
	// properly. This will always return true until we solve this.
	return true;
}

 
void USpatialNetConnection::UpdateLevelVisibility(const struct FUpdateLevelVisibilityLevelInfo& LevelVisibility)
{
	SCOPE_CYCLE_COUNTER(STAT_SpatialNetConnectionUpdateLevelVisibility);
	UNetConnection::UpdateLevelVisibility(LevelVisibility);
	// We want to update our interest as fast as possible
	// So we send an Interest update immediately.
	SpatialGDK::ActorSystem* ActorSystem = Cast<USpatialNetDriver>(Driver)->ActorSystem.Get();
	ActorSystem->UpdateInterestComponent(Cast<AActor>(PlayerController));
}

void USpatialNetConnection::FlushDormancy(AActor* Actor)
{
	Super::FlushDormancy(Actor);

	// This gets called from UNetDriver::FlushActorDormancyInternal for each connection. We inject our refresh
	// of dormancy component here. This is slightly backwards, but means we don't have to make an engine change.
	if (bReliableSpatialConnection)
	{
		const bool bMakeDormant = false;
		Cast<USpatialNetDriver>(Driver)->RefreshActorDormancy(Actor, bMakeDormant);
	}
}

Worker_EntityId USpatialNetConnection::GetPlayerControllerEntityId() const
{
	if (USpatialPackageMapClient* SpatialPackageMap = Cast<USpatialPackageMapClient>(PackageMap))
	{
		return SpatialPackageMap->GetEntityIdFromObject(PlayerController);
	}
	return SpatialConstants::INVALID_ENTITY_ID;
}
