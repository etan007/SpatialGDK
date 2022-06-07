// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "RelevancyTestActors.h"

AAlwaysRelevantTestActor::AAlwaysRelevantTestActor()
{
	bAlwaysRelevant = true;
	NetCullDistanceSquared = 1;
}

AAlwaysRelevantServerOnlyTestActor::AAlwaysRelevantServerOnlyTestActor()
{
	bAlwaysRelevant = true;
	NetCullDistanceSquared = 1;
}

AOnlyRelevantToOwnerTestActor::AOnlyRelevantToOwnerTestActor()
{
	bOnlyRelevantToOwner = true;
	NetCullDistanceSquared = 1;
}

AUseOwnerRelevancyTestActor::AUseOwnerRelevancyTestActor()
{
	bNetUseOwnerRelevancy = true;
	NetCullDistanceSquared = 1;
}
