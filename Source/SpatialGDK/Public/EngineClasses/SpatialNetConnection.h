// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "Schema/Interest.h"

#include "CoreMinimal.h"
#include "IpConnection.h"
#include "Misc/Optional.h"
#include "Runtime/Launch/Resources/Version.h"

#include <WorkerSDK/improbable/c_worker.h>

#include "SpatialNetConnection.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSpatialNetConnection, Log, All);

UCLASS(transient)
class SPATIALGDK_API USpatialNetConnection : public UIpConnection
{
	GENERATED_BODY()
public:
	USpatialNetConnection(const FObjectInitializer& ObjectInitializer);

	virtual void InitBase(UNetDriver* InDriver, class FSocket* InSocket, const FURL& InURL, EConnectionState InState, int32 InMaxPacket = 0,
						  int32 InPacketOverhead = 0) override;
	virtual void LowLevelSend(void* Data, int32 CountBits, FOutPacketTraits& Traits) override;
	virtual bool ClientHasInitializedLevelFor(const AActor* TestActor) const override;
	virtual int32 IsNetReady(bool Saturate) override;

	/** Called by PlayerController to tell connection about client level visibility change */
 
	virtual void UpdateLevelVisibility(const struct FUpdateLevelVisibilityLevelInfo& LevelVisibility) override;
 

	virtual void FlushDormancy(class AActor* Actor) override;

	virtual bool IsReplayConnection() const override { return false; }

	// These functions don't make a lot of sense in a SpatialOS implementation.
	virtual FString LowLevelGetRemoteAddress(bool bAppendPort = false) override { return TEXT(""); }
	virtual FString LowLevelDescribe() override { return TEXT(""); }
	virtual FString RemoteAddressToString() override { return TEXT(""); }
	virtual void CleanUp() override;
	///////
	// End NetConnection Interface

	Worker_EntityId GetPlayerControllerEntityId() const;

	UPROPERTY()
	bool bReliableSpatialConnection;

	// Store the client system worker entity ID corresponding to this net connection.
	// When the corresponding PlayerController is successfully spawned, we will claim
	// the PlayerController as a partition entity for the client worker.
	Worker_EntityId ConnectionClientWorkerSystemEntityId;
};
