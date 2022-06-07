// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "Utils/SpatialDebuggerSystem.h"

#include "EngineClasses/SpatialNetDriver.h"
#include "EngineClasses/SpatialPackageMapClient.h"
#include "EngineClasses/SpatialVirtualWorkerTranslator.h"
#include "EngineClasses/SpatialWorldSettings.h"
#include "Interop/Connection/SpatialWorkerConnection.h"

#include "Schema/SpatialDebugging.h"
#include "SpatialCommonTypes.h"
#include "SpatialConstants.h"

#include "Utils/InspectionColors.h"

namespace SpatialGDK
{
SpatialDebuggerSystem::SpatialDebuggerSystem(USpatialNetDriver* InNetDriver, const FSubView& InSubView)
	: NetDriver(InNetDriver)
	, SubView(&InSubView)
{
	check(IsValid(InNetDriver));

	if (!InNetDriver->IsServer())
	{
		EntityActorMapping.Reserve(ENTITY_ACTOR_MAP_RESERVATION_COUNT);
	}
}

void SpatialDebuggerSystem::Advance()
{
	for (TMap<Worker_EntityId_Key, TWeakObjectPtr<AActor>>::TIterator It = EntityActorMapping.CreateIterator(); It; ++It)
	{
		if (!It->Value.IsValid())
		{
			It.RemoveCurrent();
		}
	}

	if (IsValid(NetDriver->LockingPolicy))
	{
		for (const TPair<Worker_EntityId_Key, TWeakObjectPtr<AActor>>& EntityActorPair : EntityActorMapping)
		{
			// All actors are valid at this point since we've removed every invalid one
			// in the previous step, so we can dereference it safely.
			UpdateSpatialDebuggingData(EntityActorPair.Key, *EntityActorPair.Value);
		}
	}

	for (const EntityDelta& EntityDelta : SubView->GetViewDelta().EntityDeltas)
	{
		switch (EntityDelta.Type)
		{
		case EntityDelta::ADD:
			OnEntityAdded(EntityDelta.EntityId);
			break;
		case EntityDelta::REMOVE:
			OnEntityRemoved(EntityDelta.EntityId);
			break;
		case EntityDelta::TEMPORARILY_REMOVED:
			OnEntityRemoved(EntityDelta.EntityId);
			OnEntityAdded(EntityDelta.EntityId);
			break;
		default:
			break;
		}

		for (const AuthorityChange& AuthorityChange : EntityDelta.AuthorityGained)
		{
			if (AuthorityChange.Type == AuthorityChange::AUTHORITY_GAINED
				&& AuthorityChange.ComponentSetId == SpatialConstants::SERVER_AUTH_COMPONENT_SET_ID)
			{
				ActorAuthorityGained(EntityDelta.EntityId);
			}
		}
	}
}

void SpatialDebuggerSystem::UpdateSpatialDebuggingData(Worker_EntityId EntityId, const AActor& Actor)
{
	TOptional<SpatialDebugging> DebuggingInfo = GetDebuggingData(EntityId);
	const bool bIsLocked = NetDriver->LockingPolicy->IsLocked(&Actor);
	if (DebuggingInfo->IsLocked != bIsLocked)
	{
		DebuggingInfo->IsLocked = bIsLocked;
		FWorkerComponentUpdate DebuggingUpdate = DebuggingInfo->CreateSpatialDebuggingUpdate();
		NetDriver->Connection->SendComponentUpdate(EntityId, &DebuggingUpdate);
	}
}

void SpatialDebuggerSystem::OnEntityAdded(const Worker_EntityId EntityId)
{
	if (!ensureAlwaysMsgf(NetDriver != nullptr, TEXT("NetDriver was nullptr in OnEntityAdded %lld callback"), EntityId))
	{
		return;
	}

	if (NetDriver->IsServer())
	{
		if (SubView->HasAuthority(EntityId, SpatialConstants::SERVER_AUTH_COMPONENT_SET_ID))
		{
			ActorAuthorityGained(EntityId);
		}

		return;
	}

	if (!ensureAlwaysMsgf(!EntityActorMapping.Contains(EntityId), TEXT("NetDriver was nullptr in OnEntityAdded %lld callback"), EntityId))
	{
		return;
	}

	if (AActor* Actor = Cast<AActor>(NetDriver->PackageMap->GetObjectFromEntityId(EntityId).Get()))
	{
		EntityActorMapping.Add(EntityId, Actor);

		OnEntityActorAddedDelegate.Broadcast(Actor);
	}
}

void SpatialDebuggerSystem::OnEntityRemoved(const Worker_EntityId EntityId)
{
	if (!NetDriver->IsServer())
	{
		EntityActorMapping.Remove(EntityId);
	}
}

void SpatialDebuggerSystem::ActorAuthorityGained(const Worker_EntityId EntityId) const
{
	if (!NetDriver->VirtualWorkerTranslator.IsValid())
	{
		// Currently, there's nothing to display in the debugger other than load balancing information.
		return;
	}

	const VirtualWorkerId LocalVirtualWorkerId = NetDriver->VirtualWorkerTranslator->GetLocalVirtualWorkerId();
	const FColor LocalVirtualWorkerColor = GetColorForWorkerName(NetDriver->VirtualWorkerTranslator->GetLocalPhysicalWorkerName());

	TOptional<SpatialDebugging> DebuggingInfo = GetDebuggingData(EntityId);

	// ASpatialDebugger could not exist on our side yet as it's replicated, but this setting can be retrieved from its CDO.
	const FColor& InvalidServerTintColor = GetDefault<USpatialGDKSettings>()->SpatialDebugger.GetDefaultObject()->InvalidServerTintColor;

	if (!DebuggingInfo.IsSet())
	{
		// Some entities won't have debug info, so create it now.
		const SpatialDebugging NewDebuggingInfo(LocalVirtualWorkerId, LocalVirtualWorkerColor, SpatialConstants::INVALID_VIRTUAL_WORKER_ID,
												InvalidServerTintColor, false);
		FWorkerComponentData Data = NewDebuggingInfo.CreateComponentData();
		NetDriver->Connection->SendAddComponent(EntityId, &Data);
		return;
	}

	DebuggingInfo->AuthoritativeVirtualWorkerId = LocalVirtualWorkerId;
	DebuggingInfo->AuthoritativeColor = LocalVirtualWorkerColor;

	// Ensure the intent colour is up to date, as the physical worker name may have changed in the event of a snapshot reload
	const PhysicalWorkerName* AuthIntentPhysicalWorkerName =
		NetDriver->VirtualWorkerTranslator->GetPhysicalWorkerForVirtualWorker(DebuggingInfo->IntentVirtualWorkerId);
	DebuggingInfo->IntentColor =
		(AuthIntentPhysicalWorkerName != nullptr) ? GetColorForWorkerName(*AuthIntentPhysicalWorkerName) : InvalidServerTintColor;

	FWorkerComponentUpdate DebuggingUpdate = DebuggingInfo->CreateSpatialDebuggingUpdate();
	NetDriver->Connection->SendComponentUpdate(EntityId, &DebuggingUpdate);
}

void SpatialDebuggerSystem::ActorAuthorityIntentChanged(Worker_EntityId EntityId, VirtualWorkerId NewIntentVirtualWorkerId) const
{
	TOptional<SpatialDebugging> DebuggingInfo = GetDebuggingData(EntityId);
	if (!ensureAlwaysMsgf(DebuggingInfo.IsSet(),
						  TEXT("Failed to process auth intent change for entity %lld because debugging info was invalid"), EntityId))
	{
		return;
	}

	DebuggingInfo->IntentVirtualWorkerId = NewIntentVirtualWorkerId;

	const PhysicalWorkerName* NewAuthoritativePhysicalWorkerName =
		NetDriver->VirtualWorkerTranslator->GetPhysicalWorkerForVirtualWorker(NewIntentVirtualWorkerId);
	if (!ensureAlwaysMsgf(NewAuthoritativePhysicalWorkerName != nullptr, TEXT("Failed to get physical worker name for virtual worker %u"),
						  NewIntentVirtualWorkerId))
	{
		return;
	}

	DebuggingInfo->IntentColor = GetColorForWorkerName(*NewAuthoritativePhysicalWorkerName);
	FWorkerComponentUpdate DebuggingUpdate = DebuggingInfo->CreateSpatialDebuggingUpdate();
	NetDriver->Connection->SendComponentUpdate(EntityId, &DebuggingUpdate);
}

TOptional<SpatialDebugging> SpatialDebuggerSystem::GetDebuggingData(Worker_EntityId Entity) const
{
	const EntityViewElement* EntityViewElementPtr = SubView->GetView().Find(Entity);

	if (EntityViewElementPtr != nullptr)
	{
		const ComponentData* SpatialDebuggingDataPtr =
			EntityViewElementPtr->Components.FindByPredicate([](const ComponentData& ComponentData) {
				return ComponentData.GetComponentId() == SpatialDebugging::ComponentId;
			});

		if (SpatialDebuggingDataPtr != nullptr)
		{
			return SpatialDebugging(SpatialDebuggingDataPtr->GetWorkerComponentData());
		}
	}

	return {};
}

AActor* SpatialDebuggerSystem::GetActor(Worker_EntityId EntityId) const
{
	const TWeakObjectPtr<AActor>* ActorPtr = EntityActorMapping.Find(EntityId);

	if (ActorPtr != nullptr)
	{
		return ActorPtr->Get();
	}

	return nullptr;
}

const Worker_EntityId_Key* SpatialDebuggerSystem::GetActorEntityId(AActor* Actor) const
{
	return EntityActorMapping.FindKey(Actor);
}

const SpatialDebuggerSystem::FEntityToActorMap& SpatialDebuggerSystem::GetActors() const
{
	return EntityActorMapping;
}
} // namespace SpatialGDK
