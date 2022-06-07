// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "Interop/RPCs/RPCStore.h"

namespace SpatialGDK
{
Schema_ComponentUpdate* FRPCStore::GetOrCreateComponentUpdate(const EntityComponentId EntityComponentIdPair,
															  const FSpatialGDKSpanId& SpanId /*= {}*/)
{
	PendingUpdate* ComponentUpdatePtr = PendingComponentUpdatesToSend.Find(EntityComponentIdPair);
	if (ComponentUpdatePtr == nullptr)
	{
		ComponentUpdatePtr = &PendingComponentUpdatesToSend.Emplace(EntityComponentIdPair, Schema_CreateComponentUpdate());
	}
	return ComponentUpdatePtr->Update;
}

void FRPCStore::AddSpanIdForComponentUpdate(EntityComponentId EntityComponentIdPair, const FSpatialGDKSpanId& SpanId)
{
	PendingUpdate* ComponentUpdatePtr = PendingComponentUpdatesToSend.Find(EntityComponentIdPair);
	if (ComponentUpdatePtr != nullptr)
	{
		ComponentUpdatePtr->SpanIds.Add(SpanId);
	}
}

Schema_ComponentData* FRPCStore::GetOrCreateComponentData(const EntityComponentId EntityComponentIdPair)
{
	Schema_ComponentData** ComponentDataPtr = PendingRPCsOnEntityCreation.Find(EntityComponentIdPair);
	if (ComponentDataPtr == nullptr)
	{
		ComponentDataPtr = &PendingRPCsOnEntityCreation.Add(EntityComponentIdPair, Schema_CreateComponentData());
	}
	return *ComponentDataPtr;
}
} // namespace SpatialGDK
