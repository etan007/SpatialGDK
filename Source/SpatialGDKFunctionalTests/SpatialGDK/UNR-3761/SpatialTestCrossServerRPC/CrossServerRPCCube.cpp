// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "CrossServerRPCCube.h"
#include "EngineClasses/SpatialNetDriver.h"
#include "EngineClasses/SpatialPackageMapClient.h"
#include "Net/UnrealNetwork.h"

ACrossServerRPCCube::ACrossServerRPCCube()
{
	bAlwaysRelevant = true;
	bNetLoadOnClient = true;
	bNetLoadOnNonAuthServer = true;
}

void ACrossServerRPCCube::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACrossServerRPCCube, ReceivedCrossServerRPCS);
	DOREPLIFETIME(ACrossServerRPCCube, AuthEntityId);
}

void ACrossServerRPCCube::CrossServerTestRPC_Implementation(int SendingServerID)
{
	ReceivedCrossServerRPCS.Add(SendingServerID);
}

void ACrossServerRPCCube::RecordEntityId()
{
	if (HasAuthority())
	{
		USpatialNetDriver* SpatialNetDriver = Cast<USpatialNetDriver>(GetNetDriver());
		AuthEntityId = SpatialNetDriver->PackageMap->GetEntityIdFromObject(this);
	}
}
