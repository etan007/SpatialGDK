// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "RPCInInterfaceActor.h"

ARPCInInterfaceActor::ARPCInInterfaceActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bAlwaysRelevant = true;
	bReplicates = true;
}

void ARPCInInterfaceActor::RPCInInterface_Implementation()
{
	bRPCReceived = true;
}
