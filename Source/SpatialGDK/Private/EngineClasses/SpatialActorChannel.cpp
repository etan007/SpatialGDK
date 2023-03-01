// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "EngineClasses/SpatialActorChannel.h"

#include "Engine/DemoNetDriver.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Net/DataBunch.h"
#include "Net/NetworkProfiler.h"

#if WITH_EDITOR
#include "Settings/LevelEditorPlaySettings.h"
#endif

#include "EngineClasses/SpatialNetConnection.h"
#include "EngineClasses/SpatialNetDriver.h"
#include "EngineClasses/SpatialPackageMapClient.h"
#include "EngineStats.h"
#include "Interop/ActorSystem.h"
#include "Interop/Connection/SpatialEventTracer.h"
#include "Interop/GlobalStateManager.h"
#include "Interop/SpatialReceiver.h"
#include "Interop/SpatialSender.h"
#include "LoadBalancing/AbstractLBStrategy.h"
#include "Schema/ActorOwnership.h"
#include "Schema/NetOwningClientWorker.h"
#include "SpatialConstants.h"
#include "SpatialGDKSettings.h"
#include "Utils/ComponentFactory.h"
#include "Utils/EntityFactory.h"
#include "Utils/GDKPropertyMacros.h"
#include "Utils/InterestFactory.h"
#include "Utils/RepLayoutUtils.h"
#include "Utils/SchemaOption.h"
#include "Utils/SpatialActorUtils.h"

DEFINE_LOG_CATEGORY(LogSpatialActorChannel);

DECLARE_CYCLE_STAT(TEXT("ReplicateActor"), STAT_SpatialActorChannelReplicateActor, STATGROUP_SpatialNet);
DECLARE_CYCLE_STAT(TEXT("UpdateSpatialPosition"), STAT_SpatialActorChannelUpdateSpatialPosition, STATGROUP_SpatialNet);
DECLARE_CYCLE_STAT(TEXT("ReplicateSubobject"), STAT_SpatialActorChannelReplicateSubobject, STATGROUP_SpatialNet);
DECLARE_CYCLE_STAT(TEXT("ServerProcessOwnershipChange"), STAT_ServerProcessOwnershipChange, STATGROUP_SpatialNet);
DECLARE_CYCLE_STAT(TEXT("ClientProcessOwnershipChange"), STAT_ClientProcessOwnershipChange, STATGROUP_SpatialNet);
DECLARE_CYCLE_STAT(TEXT("IsAuthoritativeServer"), STAT_IsAuthoritativeServer, STATGROUP_SpatialNet);

namespace
{
const int32 MaxSendingChangeHistory = FSendingRepState::MAX_CHANGE_HISTORY;

// This is a bookkeeping function that is similar to the one in RepLayout.cpp, modified for our needs (e.g. no NaKs)
// We can't use the one in RepLayout.cpp because it's private and it cannot account for our approach.
// In this function, we poll for any changes in Unreal properties compared to the last time we replicated this actor.
void UpdateChangelistHistory(TUniquePtr<FRepState>& RepState)
{
	FSendingRepState* SendingRepState = RepState->GetSendingRepState();

	if (!ensureAlwaysMsgf(SendingRepState->HistoryEnd >= SendingRepState->HistoryStart,
						  TEXT("HistoryEnd buffer index should never be smaller than HistoryStart")))
	{
		return;
	}

	const int32 HistoryCount = SendingRepState->HistoryEnd - SendingRepState->HistoryStart;

	if (!ensureAlwaysMsgf(HistoryCount < MaxSendingChangeHistory,
						  TEXT("Changelist history should always be smaller than the MaxSendingChangeHistory")))
	{
		return;
	}

	for (int32 i = SendingRepState->HistoryStart; i < SendingRepState->HistoryEnd; i++)
	{
		const int32 HistoryIndex = i % MaxSendingChangeHistory;

		FRepChangedHistory& HistoryItem = SendingRepState->ChangeHistory[HistoryIndex];

		ensureAlwaysMsgf(HistoryItem.Changed.Num() > 0, TEXT("All active history items should contain a change list"));

		HistoryItem.Changed.Empty();
		HistoryItem.OutPacketIdRange = FPacketIdRange();
		SendingRepState->HistoryStart++;
	}

	// Remove any tiling in the history markers to keep them from wrapping over time
	const int32 NewHistoryCount = SendingRepState->HistoryEnd - SendingRepState->HistoryStart;

	if (!ensureAlwaysMsgf(NewHistoryCount <= MaxSendingChangeHistory, TEXT("NewHistoryCount greater or equal to MaxSendingChangeHistory")))
	{
		return;
	}

	SendingRepState->HistoryStart = SendingRepState->HistoryStart % MaxSendingChangeHistory;
	SendingRepState->HistoryEnd = SendingRepState->HistoryStart + NewHistoryCount;
}

} // end anonymous namespace

bool FSpatialObjectRepState::MoveMappedObjectToUnmapped_r(const FUnrealObjectRef& ObjRef, FObjectReferencesMap& ObjectReferencesMap)
{
	bool bFoundRef = false;

	for (auto& ObjReferencePair : ObjectReferencesMap)
	{
		FObjectReferences& ObjReferences = ObjReferencePair.Value;

		if (ObjReferences.Array.IsValid())
		{
			if (MoveMappedObjectToUnmapped_r(ObjRef, *ObjReferences.Array))
			{
				bFoundRef = true;
			}
			continue;
		}

		if (ObjReferences.MappedRefs.Contains(ObjRef))
		{
			ObjReferences.MappedRefs.Remove(ObjRef);
			ObjReferences.UnresolvedRefs.Add(ObjRef);
			bFoundRef = true;
		}
	}

	return bFoundRef;
}

bool FSpatialObjectRepState::MoveMappedObjectToUnmapped(const FUnrealObjectRef& ObjRef)
{
	if (MoveMappedObjectToUnmapped_r(ObjRef, ReferenceMap))
	{
		UnresolvedRefs.Add(ObjRef);
		return true;
	}
	return false;
}

void FSpatialObjectRepState::GatherObjectRef(TSet<FUnrealObjectRef>& OutReferenced, TSet<FUnrealObjectRef>& OutUnresolved,
											 const FObjectReferences& CurReferences) const
{
	if (CurReferences.Array.IsValid())
	{
		for (auto const& Entry : *CurReferences.Array)
		{
			GatherObjectRef(OutReferenced, OutUnresolved, Entry.Value);
		}
	}

	OutUnresolved.Append(CurReferences.UnresolvedRefs);

	// Add both kind of references to OutReferenced map.
	// It is simpler to manage the Ref to RepState map that way by not requiring strict partitioning between both sets.
	OutReferenced.Append(CurReferences.UnresolvedRefs);
	OutReferenced.Append(CurReferences.MappedRefs);
}

void FSpatialObjectRepState::UpdateRefToRepStateMap(FObjectToRepStateMap& RepStateMap)
{
	// Inspired by FObjectReplicator::UpdateGuidToReplicatorMap
	UnresolvedRefs.Empty();

	TSet<FUnrealObjectRef> LocalReferencedObj;
	for (auto& Entry : ReferenceMap)
	{
		GatherObjectRef(LocalReferencedObj, UnresolvedRefs, Entry.Value);
	}

	// TODO : Support references in structures updated by deltas. UNR-2556
	// Look for the code iterating over LifetimeCustomDeltaProperties in the equivalent FObjectReplicator method.

	// Go over all referenced guids, and make sure we're tracking them in the GuidToReplicatorMap
	for (const FUnrealObjectRef& Ref : LocalReferencedObj)
	{
		if (!ReferencedObj.Contains(Ref))
		{
			RepStateMap.FindOrAdd(Ref).Add(ThisObj);
		}
	}

	// Remove any guids that we were previously tracking but no longer should
	for (const FUnrealObjectRef& Ref : ReferencedObj)
	{
		if (!LocalReferencedObj.Contains(Ref))
		{
			TSet<FChannelObjectPair>* RepStatesWithRef = RepStateMap.Find(Ref);

			if (ensure(RepStatesWithRef))
			{
				RepStatesWithRef->Remove(ThisObj);

				if (RepStatesWithRef->Num() == 0)
				{
					RepStateMap.Remove(Ref);
				}
			}
		}
	}

	ReferencedObj = MoveTemp(LocalReferencedObj);
}

USpatialActorChannel::USpatialActorChannel(const FObjectInitializer& ObjectInitializer /*= FObjectInitializer::Get()*/)
	: Super(ObjectInitializer)
	, bCreatedEntity(false)
	, bCreatingNewEntity(false)
	, EntityId(SpatialConstants::INVALID_ENTITY_ID)
	, bInterestDirty(false)
	, bNetOwned(false)
	, NetDriver(nullptr)
	, EventTracer(nullptr)
	, LastPositionSinceUpdate(FVector::ZeroVector)
	, TimeWhenPositionLastUpdated(0.0)
{
}

void USpatialActorChannel::Init(UNetConnection* InConnection, int32 ChannelIndex, EChannelCreateFlags CreateFlag)
{
	Super::Init(InConnection, ChannelIndex, CreateFlag);

	// Actor Channels are pooled, so we must initialize internal state here.
	bCreatedEntity = false;
	bCreatingNewEntity = false;
	EntityId = SpatialConstants::INVALID_ENTITY_ID;
	bInterestDirty = false;
	bNetOwned = false;
	bIsAuthClient = false;
	bIsAuthServer = false;
	LastPositionSinceUpdate = FVector::ZeroVector;
	TimeWhenPositionLastUpdated = 0.0;
	AuthorityReceivedTimestamp = 0;
	bNeedOwnerInterestUpdate = false;

	PendingDynamicSubobjects.Empty();
	SavedInterestBucketComponentID = SpatialConstants::INVALID_COMPONENT_ID;

	FramesTillDormancyAllowed = 0;

	NetDriver = Cast<USpatialNetDriver>(Connection->Driver);
	check(NetDriver);
	Sender = NetDriver->Sender;

	check(IsValid(NetDriver->Connection));
	EventTracer = NetDriver->Connection->GetEventTracer();
}

void USpatialActorChannel::RetireEntityIfAuthoritative()
{
	if (NetDriver->Connection == nullptr)
	{
		return;
	}

	if (!NetDriver->IsAuthoritativeDestructionAllowed())
	{
		return;
	}

	const bool bHasAuthority = NetDriver->HasServerAuthority(EntityId);
	if (Actor != nullptr)
	{
		if (bHasAuthority)
		{
			// Workaround to delay the delete entity request if tearing off.
			// Task to improve this: UNR-841
			if (Actor->GetTearOff())
			{
				NetDriver->DelayedRetireEntity(EntityId, 1.0f, Actor->IsNetStartupActor());
				if (ensureMsgf(Actor->HasAuthority(), TEXT("EntityId %lld Actor %s doesn't have authority, can't disable replication"),
							   EntityId, *Actor->GetName()))
				{
					// Since the entity deletion is delayed, this creates a situation,
					// when the Actor is torn off, but still replicates.
					// Disabling replication makes RPC calls impossible for this Actor.
					Actor->SetReplicates(false);
				}
			}
			else
			{
				NetDriver->ActorSystem->RetireEntity(EntityId, Actor->IsNetStartupActor());
			}
		}
		else if (bCreatedEntity) // We have not gained authority yet
		{
			if (ensureMsgf(Actor->HasAuthority(), TEXT("EntityId %lld Actor %s doesn't have authority, can't disable replication"),
						   EntityId, *Actor->GetName()))
			{
				Actor->SetReplicates(false);
			}

			NetDriver->ActorSystem->RetireWhenAuthoritative(
				EntityId, NetDriver->ClassInfoManager->GetComponentIdForClass(*Actor->GetClass()), Actor->IsNetStartupActor(),
				Actor->GetTearOff()); // Ensure we don't recreate the actor
		}
	}
	else
	{
		// This is unsupported, and shouldn't happen, don't attempt to cleanup entity to better indicate something has gone wrong
		UE_LOG(LogSpatialActorChannel, Error,
			   TEXT("RetireEntityIfAuthoritative called on actor channel with null actor - entity id (%lld)"), EntityId);
	}
}

void USpatialActorChannel::ValidateChannelNotBroken()
{
	// In native Unreal, channels can be broken in certain circumstances (e.g. when unloading streaming levels or failing to process a
	// bunch). This shouldn't happen in Spatial and would likely lead to unexpected behavior.
	if (Broken)
	{
		UE_LOG(LogSpatialActorChannel, Error, TEXT("Channel broken when cleaning up/closing channel. Entity id: %lld, actor: %s"), EntityId,
			   *GetNameSafe(Actor));
	}
}

bool USpatialActorChannel::CleanUp(const bool bForDestroy, EChannelCloseReason CloseReason)
{
	ValidateChannelNotBroken();

	if (NetDriver != nullptr)
	{
#if WITH_EDITOR
		const bool bDeleteDynamicEntities = GetDefault<ULevelEditorPlaySettings>()->GetDeleteDynamicEntities();

		if (bDeleteDynamicEntities && NetDriver->IsServer() && NetDriver->GetActorChannelByEntityId(EntityId) != nullptr
			&& CloseReason != EChannelCloseReason::Dormancy)
		{
			// If we're a server worker, and the entity hasn't already been cleaned up, delete it on shutdown.
			RetireEntityIfAuthoritative();
		}
#endif // WITH_EDITOR

		if (CloseReason != EChannelCloseReason::Dormancy)
		{
			// Must cleanup actor and subobjects before UActorChannel::Cleanup as it will clear CreateSubObjects.
			NetDriver->PackageMap->RemoveEntityActor(EntityId);
		}
		else
		{
			NetDriver->RegisterDormantEntityId(EntityId);
		}

		if (CloseReason == EChannelCloseReason::Destroyed || CloseReason == EChannelCloseReason::LevelUnloaded)
		{
			NetDriver->GetRPCService()->ClearPendingRPCs(EntityId);
		}
		NetDriver->RemoveActorChannel(EntityId, *this);
	}

	EventTracer = nullptr;

	return UActorChannel::CleanUp(bForDestroy, CloseReason);
}

int64 USpatialActorChannel::Close(EChannelCloseReason Reason)
{
	ValidateChannelNotBroken();

	if (Reason == EChannelCloseReason::Dormancy)
	{
		// Closed for dormancy reasons, ensure we update the component state of this entity.
		const bool bMakeDormant = true;
		NetDriver->RefreshActorDormancy(Actor, bMakeDormant);
		NetDriver->RegisterDormantEntityId(EntityId);
	}
	else if (Reason == EChannelCloseReason::Relevancy)
	{
		ensureAlwaysMsgf(IsAuthoritativeServer(),
						 TEXT("Trying to close SpatialActorChannel because of Relevancy on a non-authoritative server"));
		// Do nothing except close actor channel - this should only get processed on auth server
	}
	else
	{
		RetireEntityIfAuthoritative();
		NetDriver->PackageMap->RemoveEntityActor(EntityId);
	}

	NetDriver->RemoveActorChannel(EntityId, *this);

	return Super::Close(Reason);
}

void USpatialActorChannel::UpdateShadowData()
{
	if (!ensureAlwaysMsgf(Actor != nullptr, TEXT("Called UpdateShadowData but Actor was nullptr")))
	{
		return;
	}

	// If this channel was responsible for creating the actor, we do not want to initialize our shadow data
	// to the latest state since there could have been state that has changed between creation of the entity
	// and gaining of authority. Revisit this with UNR-1034
	// TODO: UNR-1029 - log when the shadow data differs from the current state of the Actor.
	if (bCreatedEntity)
	{
		return;
	}

	// Refresh shadow data when crossing over servers to prevent stale/out-of-date data.
	ResetShadowData(*ActorReplicator->RepLayout, ActorReplicator->ChangelistMgr->GetRepChangelistState()->StaticBuffer, Actor);

	// Refresh the shadow data for all replicated components of this actor as well.
	for (UActorComponent* ActorComponent : Actor->GetReplicatedComponents())
	{
		FObjectReplicator& ComponentReplicator = FindOrCreateReplicator(ActorComponent).Get();
		ResetShadowData(*ComponentReplicator.RepLayout, ComponentReplicator.ChangelistMgr->GetRepChangelistState()->StaticBuffer,
						ActorComponent);
	}
}

FRepChangeState USpatialActorChannel::CreateInitialRepChangeState(TWeakObjectPtr<UObject> Object)
{
	checkf(Object != nullptr, TEXT("Attempted to create initial rep change state on an object which is null."));
	checkf(!Object->IsPendingKill(),
		   TEXT("Attempted to create initial rep change state on an object which is pending kill. This will fail to create a RepLayout: "),
		   *Object->GetName());

	FObjectReplicator& Replicator = FindOrCreateReplicator(Object.Get()).Get();

	TArray<uint16> InitialRepChanged;

	int32 DynamicArrayDepth = 0;
	const int32 CmdCount = Replicator.RepLayout->Cmds.Num();
	for (uint16 CmdIdx = 0; CmdIdx < CmdCount; ++CmdIdx)
	{
		const auto& Cmd = Replicator.RepLayout->Cmds[CmdIdx];

		InitialRepChanged.Add(Cmd.RelativeHandle);

		if (Cmd.Type == ERepLayoutCmdType::DynamicArray)
		{
			DynamicArrayDepth++;

			// For the first layer of each dynamic array encountered at the root level
			// add the number of array properties to conform to Unreal's RepLayout design and
			// allow FRepHandleIterator to jump over arrays. Cmd.EndCmd is an index into
			// RepLayout->Cmds[] that points to the value after the termination NULL of this array.
			if (DynamicArrayDepth == 1)
			{
				InitialRepChanged.Add((Cmd.EndCmd - CmdIdx) - 2);
			}
		}
		else if (Cmd.Type == ERepLayoutCmdType::Return)
		{
			DynamicArrayDepth--;
			checkf(DynamicArrayDepth >= 0 || CmdIdx == CmdCount - 1, TEXT("Encountered erroneous RepLayout"));
		}
	}

	return { InitialRepChanged, *Replicator.RepLayout };
}

void USpatialActorChannel::UpdateVisibleComponent(AActor* InActor)
{
	// Make sure that the InActor is not a PlayerController, GameplayDebuggerCategoryReplicator or GameMode.
	if (SpatialGDK::DoesActorClassIgnoreVisibilityCheck(InActor))
	{
		return;
	}

	// Unreal applies the following rules (in order) in determining the relevant set of Actors for a player:
	// If the Actor is hidden (bHidden == true) and the root component does not collide then the Actor is not relevant.
	// We apply the same rules to add/remove the Visible component to an actor that determines if clients will checkout the actor or
	// not. Make sure that the Actor is also not always relevant.
	if (InActor->IsHidden() && (!InActor->GetRootComponent() || !InActor->GetRootComponent()->IsCollisionEnabled())
		&& !InActor->bAlwaysRelevant)
	{
		NetDriver->RefreshActorVisibility(InActor, false);
	}
	else
	{
		NetDriver->RefreshActorVisibility(InActor, true);
	}
}

int64 USpatialActorChannel::ReplicateActor()
{
	SCOPE_CYCLE_COUNTER(STAT_SpatialActorChannelReplicateActor);

	if (!IsReadyForReplication())
	{
		return 0;
	}

	check(Actor);
	check(!Closing);
	check(Connection);
	check(Connection->PackageMap);

	const UWorld* const ActorWorld = Actor->GetWorld();

#if STATS
	// Group specific actor class stats by parent native class, which is what vanilla Unreal does.
	UClass* ParentNativeClass = GetParentNativeClass(Actor->GetClass());
	SCOPE_CYCLE_UOBJECT(ParentNativeClass, ParentNativeClass);
#endif

	// Group actors by exact class, one level below parent native class.
	SCOPE_CYCLE_UOBJECT(ReplicateActor, Actor);


	const bool bReplay = ActorWorld && ActorWorld->GetDemoNetDriver() == Connection->GetDriver();


	//////////////////////////////////////////////////////////////////////////
	// Begin - error and stat duplication from DataChannel::ReplicateActor()
	if (!bReplay)
	{
		GNumReplicateActorCalls++;
	}

	// triggering replication of an Actor while already in the middle of replication can result in invalid data being sent and is therefore
	// illegal
	if (bIsReplicatingActor)
	{
		FString Error(FString::Printf(TEXT("ReplicateActor called while already replicating! %s"), *Describe()));
		UE_LOG(LogNet, Log, TEXT("%s"), *Error);
		ensureMsgf(false, TEXT("%s"), *Error);
		return 0;
	}
	else if (bActorIsPendingKill)
	{
		// Don't need to do anything, because it should have already been logged.
		return 0;
	}
	// If our Actor is PendingKill, that's bad. It means that somehow it wasn't properly removed
	// from the NetDriver or ReplicationDriver.
	// TODO: Maybe notify the NetDriver / RepDriver about this, and have the channel close?
	else if (Actor->IsPendingKillOrUnreachable())
	{
		bActorIsPendingKill = true;
		ActorReplicator.Reset();
		FString Error(FString::Printf(TEXT("ReplicateActor called with PendingKill Actor! %s"), *Describe()));
		UE_LOG(LogNet, Log, TEXT("%s"), *Error);
		ensureMsgf(false, TEXT("%s"), *Error);
		return 0;
	}
	// End - error and stat duplication from DataChannel::ReplicateActor()
	//////////////////////////////////////////////////////////////////////////

	// Create an outgoing bunch (to satisfy some of the functions below).
	FOutBunch Bunch(this, 0);
	if (Bunch.IsError())
	{
		return 0;
	}

	bIsReplicatingActor = true;
	FReplicationFlags RepFlags;

	// Send initial stuff.
	if (bCreatingNewEntity)
	{
		RepFlags.bNetInitial = true;
		// Include changes to Bunch (duplicating existing logic in DataChannel), despite us not using it,
		// since these are passed to the virtual OnSerializeNewActor, whose implementations could use them.
		Bunch.bClose = Actor->bNetTemporary;
		Bunch.bReliable = true; // Net temporary sends need to be reliable as well to force them to retry
	}

	// Here, Unreal would have determined if this connection belongs to this actor's Outer.
	// We don't have this concept when it comes to connections, our ownership-based logic is in the interop layer.
	// Setting this to true, but should not matter in the end.
	RepFlags.bNetOwner = true;

	// If initial, send init data.
	if (RepFlags.bNetInitial && OpenedLocally)
	{
		Actor->OnSerializeNewActor(Bunch);
	}

	RepFlags.bNetSimulated = (Actor->GetRemoteRole() == ROLE_SimulatedProxy);

	RepFlags.bRepPhysics = Actor->GetReplicatedMovement().bRepPhysics;

	RepFlags.bReplay = bReplay;

	UE_LOG(LogNetTraffic, Log, TEXT("Replicate %s, bNetInitial: %d, bNetOwner: %d"), *Actor->GetName(), RepFlags.bNetInitial,
		   RepFlags.bNetOwner);

	// Always replicate initial only properties and rely on QBI to filter where necessary.
	RepFlags.bNetInitial = true;

	FMemMark MemMark(FMemStack::Get()); // The calls to ReplicateProperties will allocate memory on FMemStack::Get(), and use it in
										// ::PostSendBunch. we free it below

	// ----------------------------------------------------------
	// Replicate Actor and Component properties and RPCs
	// ----------------------------------------------------------

#if USE_NETWORK_PROFILER
	const uint32 ActorReplicateStartTime = GNetworkProfiler.IsTrackingEnabled() ? FPlatformTime::Cycles() : 0;
#endif

	const USpatialGDKSettings* SpatialGDKSettings = GetDefault<USpatialGDKSettings>();

	// Update SpatialOS position.
	if (!bCreatingNewEntity)
	{
		if (SpatialGDKSettings->bBatchSpatialPositionUpdates)
		{
			NetDriver->ActorSystem->RegisterChannelForPositionUpdate(this);
		}
		else
		{
			UpdateSpatialPosition();
		}
	}

	if (Actor->GetIsHiddenDirty())
	{
		UpdateVisibleComponent(Actor);
		Actor->SetIsHiddenDirty(false);
	}

	// Update the replicated property change list.
	FRepChangelistState* ChangelistState = ActorReplicator->ChangelistMgr->GetRepChangelistState();


	const ERepLayoutResult UpdateResult =
		ActorReplicator->RepLayout->UpdateChangelistMgr(ActorReplicator->RepState->GetSendingRepState(), *ActorReplicator->ChangelistMgr,
														Actor, Connection->Driver->ReplicationFrame, RepFlags, bForceCompareProperties);

	if (UNLIKELY(ERepLayoutResult::FatalError == UpdateResult))
	{
		// This happens when a replicated array is over the maximum size (UINT16_MAX).
		// Native Unreal just closes the connection at this point, but we can't do that as
		// it may lead to unexpected consequences for the deployment. Instead, we just early out.
		// TODO: UNR-4667 - Investigate this behavior in more detail.

		// Connection->SetPendingCloseDueToReplicationFailure();
		return 0;
	}

	FSendingRepState* SendingRepState = ActorReplicator->RepState->GetSendingRepState();

	const int32 PossibleNewHistoryIndex = SendingRepState->HistoryEnd % MaxSendingChangeHistory;
	FRepChangedHistory& PossibleNewHistoryItem = SendingRepState->ChangeHistory[PossibleNewHistoryIndex];
	TArray<uint16>& RepChanged = PossibleNewHistoryItem.Changed;

	// Gather all change lists that are new since we last looked, and merge them all together into a single CL
	for (int32 i = SendingRepState->LastChangelistIndex; i < ChangelistState->HistoryEnd; i++)
	{
		const int32 HistoryIndex = i % FRepChangelistState::MAX_CHANGE_HISTORY;
		FRepChangedHistory& HistoryItem = ChangelistState->ChangeHistory[HistoryIndex];
		TArray<uint16> Temp = RepChanged;

		if (HistoryItem.Changed.Num() > 0)
		{
			ActorReplicator->RepLayout->MergeChangeList((uint8*)Actor, HistoryItem.Changed, Temp, RepChanged);
		}
		else
		{
			UE_LOG(LogSpatialActorChannel, Warning, TEXT("EntityId: %lld Actor: %s Changelist with index %d has no changed items"),
				   EntityId, *Actor->GetName(), i);
		}
	}

	SendingRepState->LastCompareIndex = ChangelistState->CompareIndex;

	const FClassInfo& Info = NetDriver->ClassInfoManager->GetOrCreateClassInfoByClass(Actor->GetClass());

	ReplicationBytesWritten = 0;

	if (!bCreatingNewEntity && NeedOwnerInterestUpdate() && NetDriver->InterestFactory->DoOwnersHaveEntityId(Actor))
	{
		NetDriver->ActorSystem->UpdateInterestComponent(Actor);
		SetNeedOwnerInterestUpdate(false);
	}

	// If any properties have changed, send a component update.
	if (bCreatingNewEntity || RepChanged.Num() > 0)
	{
		if (bCreatingNewEntity)
		{
			// Need to try replicating all subobjects before entity creation to make sure their respective FObjectReplicator exists
			// so we know what subobjects are relevant for replication when creating the entity.
			Actor->ReplicateSubobjects(this, &Bunch, &RepFlags);

			NetDriver->ActorSystem->SendCreateEntityRequest(*this, ReplicationBytesWritten);

			bCreatedEntity = true;

			// We preemptively set the Actor role to SimulatedProxy if load balancing is disabled
			// (since the legacy behaviour is to wait until Spatial tells us we have authority)
			if (NetDriver->LoadBalanceStrategy == nullptr)
			{
				Actor->Role = ROLE_SimulatedProxy;
				Actor->RemoteRole = ROLE_Authority;
			}
		}
		else
		{
			FRepChangeState RepChangeState = { RepChanged, GetObjectRepLayout(Actor) };

			NetDriver->ActorSystem->SendComponentUpdates(Actor, Info, this, &RepChangeState, ReplicationBytesWritten);

			bInterestDirty = false;
		}

		if (RepChanged.Num() > 0)
		{
			SendingRepState->HistoryEnd++;
		}
	}

	UpdateChangelistHistory(ActorReplicator->RepState);

	// This would indicate we need to flush our state before we could consider going dormant. In Spatial, this
	// dormancy can occur immediately (because we don't require acking), which means that dormancy can be thrashed
	// on and off if AActor::FlushNetDormancy is being called (possibly because replicated properties are being updated
	// within blueprints which invokes this call). Give a few frames before allowing channel to go dormant.
	if (ActorReplicator->bLastUpdateEmpty == 0)
	{
		FramesTillDormancyAllowed = 2;
	}
	else if (FramesTillDormancyAllowed > 0)
	{
		--FramesTillDormancyAllowed;
	}

	SendingRepState->LastChangelistIndex = ChangelistState->HistoryEnd;
	SendingRepState->bOpenAckedCalled = true;
	ActorReplicator->bLastUpdateEmpty = 1;

	if (bCreatingNewEntity)
	{
		bCreatingNewEntity = false;
	}
	else
	{
		FOutBunch DummyOutBunch;

		// Actor::ReplicateSubobjects is overridable and enables the Actor to replicate any subobjects directly, via a
		// call back into SpatialActorChannel::ReplicateSubobject, as well as issues a call to UActorComponent::ReplicateSubobjects
		// on any of its replicating actor components. This allows the component to replicate any of its subobjects directly via
		// the same SpatialActorChannel::ReplicateSubobject.
		Actor->ReplicateSubobjects(this, &DummyOutBunch, &RepFlags);

		// Look for deleted subobjects
		for (auto RepComp = ReplicationMap.CreateIterator(); RepComp; ++RepComp)
		{
			if (!RepComp.Value()->GetWeakObjectPtr().IsValid())
			{
				FUnrealObjectRef ObjectRef = NetDriver->PackageMap->GetUnrealObjectRefFromNetGUID(RepComp.Value().Get().ObjectNetGUID);

				if (ObjectRef.IsValid())
				{
					OnSubobjectDeleted(ObjectRef, RepComp.Key(), RepComp.Value()->GetWeakObjectPtr());

					NetDriver->ActorSystem->SendRemoveComponentForClassInfo(
						EntityId, NetDriver->ClassInfoManager->GetClassInfoByComponentId(ObjectRef.Offset));
				}

				RepComp.Value()->CleanUp();
				RepComp.RemoveCurrent();
			}
		}
	}

#if USE_NETWORK_PROFILER
	NETWORK_PROFILER(GNetworkProfiler.TrackReplicateActor(Actor, RepFlags, FPlatformTime::Cycles() - ActorReplicateStartTime, Connection));
#endif

	// If we evaluated everything, mark LastUpdateTime, even if nothing changed.
	LastUpdateTime = NetDriver->GetElapsedTime();

	MemMark.Pop();

	bIsReplicatingActor = false;

	bForceCompareProperties = false; // Only do this once per frame when set

	if (ReplicationBytesWritten > 0)
	{
		INC_DWORD_STAT_BY(STAT_NumReplicatedActors, 1);
	}
	INC_DWORD_STAT_BY(STAT_NumReplicatedActorBytes, ReplicationBytesWritten);

	return ReplicationBytesWritten * 8;
}

void USpatialActorChannel::DynamicallyAttachSubobject(UObject* Object)
{
	// Find out if this is a dynamic subobject or a subobject that is already attached but is now replicated
	FUnrealObjectRef ObjectRef = NetDriver->PackageMap->GetUnrealObjectRefFromObject(Object);

	const FClassInfo* Info = nullptr;

	// Subobject that's a part of the CDO by default does not need to be created.
	if (ObjectRef.IsValid())
	{
		Info = &NetDriver->ClassInfoManager->GetOrCreateClassInfoByObject(Object);
	}
	else
	{
		Info = NetDriver->PackageMap->TryResolveNewDynamicSubobjectAndGetClassInfo(Object);

		if (Info == nullptr)
		{
			// This is a failure but there is already a log inside TryResolveNewDynamicSubbojectAndGetClassInfo
			return;
		}
	}

	if (!ensureAlwaysMsgf(Info != nullptr, TEXT("Subobject info was nullptr. Actor: %s"), *GetNameSafe(Object)))
	{
		return;
	}

	NetDriver->ActorSystem->SendAddComponentForSubobject(this, Object, *Info, ReplicationBytesWritten);
}

bool USpatialActorChannel::ReplicateSubobject(UObject* Object, const FReplicationFlags& RepFlags)
{
	SCOPE_CYCLE_COUNTER(STAT_SpatialActorChannelReplicateSubobject);

#if STATS
	// Break down the subobject timing stats by parent native class.
	UClass* ParentNativeClass = GetParentNativeClass(Object->GetClass());
	SCOPE_CYCLE_UOBJECT(ReplicateSubobjectParentClass, ParentNativeClass);
#endif

	// Further break down the subobject timing stats by class.
	SCOPE_CYCLE_UOBJECT(ReplicateSubobjectSpecificClass, Object);

	bool bCreatedReplicator = false;

	FObjectReplicator& Replicator = FindOrCreateReplicator(Object, &bCreatedReplicator).Get();

	// If we're creating an entity, don't try replicating
	if (bCreatingNewEntity)
	{
		return false;
	}

	// New subobject that hasn't been replicated before
	if (bCreatedReplicator)
	{
		// Attach to to the entity
		DynamicallyAttachSubobject(Object);
		return false;
	}

	if (PendingDynamicSubobjects.Contains(Object))
	{
		// Still waiting on subobject to be attached so don't replicate
		return false;
	}

	FRepChangelistState* ChangelistState = Replicator.ChangelistMgr->GetRepChangelistState();


	const ERepLayoutResult UpdateResult =
		Replicator.RepLayout->UpdateChangelistMgr(Replicator.RepState->GetSendingRepState(), *Replicator.ChangelistMgr, Object,
												  Replicator.Connection->Driver->ReplicationFrame, RepFlags, bForceCompareProperties);

	if (UNLIKELY(ERepLayoutResult::FatalError == UpdateResult))
	{
		// This happens when a replicated array is over the maximum size (UINT16_MAX).
		// Native Unreal just closes the connection at this point, but we can't do that as
		// it may lead to unexpected consequences for the deployment. Instead, we just early out.
		// TODO: UNR-4667 - Investigate this behavior in more detail.

		// Connection->SetPendingCloseDueToReplicationFailure();
		return false;
	}


	FSendingRepState* SendingRepState = Replicator.RepState->GetSendingRepState();

	const int32 PossibleNewHistoryIndex = SendingRepState->HistoryEnd % MaxSendingChangeHistory;
	FRepChangedHistory& PossibleNewHistoryItem = SendingRepState->ChangeHistory[PossibleNewHistoryIndex];
	TArray<uint16>& RepChanged = PossibleNewHistoryItem.Changed;

	// Gather all change lists that are new since we last looked, and merge them all together into a single CL
	for (int32 i = SendingRepState->LastChangelistIndex; i < ChangelistState->HistoryEnd; i++)
	{
		const int32 HistoryIndex = i % FRepChangelistState::MAX_CHANGE_HISTORY;
		FRepChangedHistory& HistoryItem = ChangelistState->ChangeHistory[HistoryIndex];
		TArray<uint16> Temp = RepChanged;

		if (HistoryItem.Changed.Num() > 0)
		{
			Replicator.RepLayout->MergeChangeList((uint8*)Object, HistoryItem.Changed, Temp, RepChanged);
		}
		else
		{
			UE_LOG(LogSpatialActorChannel, Warning,
				   TEXT("EntityId: %lld Actor: %s Subobject: %s Changelist with index %d has no changed items"), EntityId,
				   *Actor->GetName(), *Object->GetName(), i);
		}
	}

	SendingRepState->LastCompareIndex = ChangelistState->CompareIndex;

	if (RepChanged.Num() > 0)
	{
		FRepChangeState RepChangeState = { RepChanged, GetObjectRepLayout(Object) };

		FUnrealObjectRef ObjectRef = NetDriver->PackageMap->GetUnrealObjectRefFromObject(Object);
		if (!ObjectRef.IsValid())
		{
			UE_LOG(LogSpatialActorChannel, Verbose,
				   TEXT("Attempted to replicate an invalid ObjectRef. This may be a dynamic component that couldn't attach: %s"),
				   *Object->GetName());
			return false;
		}

		const FClassInfo& Info = NetDriver->ClassInfoManager->GetOrCreateClassInfoByObject(Object);
		NetDriver->ActorSystem->SendComponentUpdates(Object, Info, this, &RepChangeState, ReplicationBytesWritten);

		SendingRepState->HistoryEnd++;
	}

	UpdateChangelistHistory(Replicator.RepState);

	SendingRepState->LastChangelistIndex = ChangelistState->HistoryEnd;
	SendingRepState->bOpenAckedCalled = true;
	Replicator.bLastUpdateEmpty = 1;

	return RepChanged.Num() > 0;
}

bool USpatialActorChannel::ReplicateSubobject(UObject* Obj, FOutBunch& Bunch, const FReplicationFlags& RepFlags)
{
	// Intentionally don't call Super::ReplicateSubobject() but rather call our custom version instead.
	return ReplicateSubobject(Obj, RepFlags);
}

bool USpatialActorChannel::ReadyForDormancy(bool bSuppressLogs /*= false*/)
{
	// Check Receiver doesn't have any pending operations for this channel
	if (NetDriver->ActorSystem->HasPendingOpsForChannel(*this))
	{
		return false;
	}

	// Hasn't been waiting for dormancy long enough allow dormancy, soft attempt to prevent dormancy thrashing
	if (FramesTillDormancyAllowed > 0)
	{
		return false;
	}

	return Super::ReadyForDormancy(bSuppressLogs);
}

void USpatialActorChannel::SetChannelActor(AActor* InActor, ESetChannelActorFlags Flags)
{
	Super::SetChannelActor(InActor, Flags);
	check(NetDriver->GetSpatialOSNetConnection() == Connection);
	USpatialPackageMapClient* PackageMap = NetDriver->PackageMap;
	EntityId = PackageMap->GetEntityIdFromObject(InActor);

	// If the entity registry has no entry for this actor, this means we need to create it.
	if (EntityId == SpatialConstants::INVALID_ENTITY_ID)
	{
		bCreatingNewEntity = true;
		TryResolveActor();
	}
	else
	{
		UE_LOG(LogSpatialActorChannel, Verbose, TEXT("Opened channel for actor %s with existing entity ID %lld."), *InActor->GetName(),
			   EntityId);

		if (PackageMap->IsEntityIdPendingCreation(EntityId))
		{
			bCreatingNewEntity = true;
			PackageMap->RemovePendingCreationEntityId(EntityId);
		}
		NetDriver->AddActorChannel(EntityId, this);
		NetDriver->UnregisterDormantEntityId(EntityId);
	}
}

bool USpatialActorChannel::TryResolveActor()
{
	EntityId = NetDriver->PackageMap->AllocateEntityIdAndResolveActor(Actor);

	if (EntityId == SpatialConstants::INVALID_ENTITY_ID)
	{
		return false;
	}

	// Inform USpatialNetDriver of this new actor channel/entity pairing
	NetDriver->AddActorChannel(EntityId, this);

	return true;
}

FObjectReplicator* USpatialActorChannel::PreReceiveSpatialUpdate(UObject* TargetObject)
{
	// If there is no NetGUID for this object, we will crash in FObjectReplicator::StartReplicating, so we verify this here.
	FNetworkGUID ObjectNetGUID = Connection->Driver->GuidCache->GetOrAssignNetGUID(TargetObject);
	if (ObjectNetGUID.IsDefault() || !ObjectNetGUID.IsValid())
	{
		// SpatialReceiver tried to resolve this object in the PackageMap, but it didn't propagate to GuidCache.
		// This could happen if the UnrealObjectRef was already mapped to a different object that's been destroyed.
		UE_LOG(LogSpatialActorChannel, Error, TEXT("PreReceiveSpatialUpdate: NetGUID is invalid! Object: %s"),
			   *TargetObject->GetPathName());
		return nullptr;
	}

	FObjectReplicator& Replicator = FindOrCreateReplicator(TargetObject).Get();
	TargetObject->PreNetReceive();

	return &Replicator;
}

void USpatialActorChannel::PostReceiveSpatialUpdate(UObject* TargetObject, const TArray<GDK_PROPERTY(Property) *>& RepNotifies,
													const TMap<GDK_PROPERTY(Property) *, FSpatialGDKSpanId>& PropertySpanIds)
{
	FObjectReplicator& Replicator = FindOrCreateReplicator(TargetObject).Get();
	TargetObject->PostNetReceive();

	Replicator.RepState->GetReceivingRepState()->RepNotifies = RepNotifies;

	auto PreCallRepNotify = [EventTracer = EventTracer, PropertySpanIds](GDK_PROPERTY(Property) * Property) {
		const FSpatialGDKSpanId* SpanId = PropertySpanIds.Find(Property);
		if (SpanId != nullptr)
		{
			EventTracer->AddToStack(*SpanId);
		}
	};

	auto PostCallRepNotify = [EventTracer = EventTracer, PropertySpanIds](GDK_PROPERTY(Property) * Property) {
		const FSpatialGDKSpanId* SpanId = PropertySpanIds.Find(Property);
		if (SpanId != nullptr)
		{
			EventTracer->PopFromStack();
		}
	};

	if (EventTracer != nullptr && PropertySpanIds.Num() > 0)
	{
		Replicator.RepLayout->PreRepNotify.BindLambda(PreCallRepNotify);
		Replicator.RepLayout->PostRepNotify.BindLambda(PostCallRepNotify);
	}

	Replicator.CallRepNotifies(false);
}

void USpatialActorChannel::UpdateSpatialPosition()
{
	SCOPE_CYCLE_COUNTER(STAT_SpatialActorChannelUpdateSpatialPosition);

	// Additional check to validate Actor is still present
	if (Actor == nullptr || Actor->IsPendingKill())
	{
		return;
	}

	// When we update an Actor's position, we want to update the position of all the children of this Actor.
	// If this Actor is a PlayerController, we want to update all of its children and its possessed Pawn.
	// That means if this Actor has an Owner or has a NetConnection and is NOT a PlayerController
	// we want to defer updating position until we reach the highest parent.
	AActor* ActorOwner = Actor->GetOwner();

	if ((ActorOwner != nullptr || Actor->GetNetConnection() != nullptr) && !Actor->IsA<APlayerController>())
	{
		// If this Actor's owner is not replicated (e.g. parent = AI Controller), the actor will not have it's spatial
		// position updated as this code will never be run for the parent.
		if (!(Actor->GetNetConnection() == nullptr && ActorOwner != nullptr && !ActorOwner->GetIsReplicated()))
		{
			return;
		}
	}


	if (!SatisfiesSpatialPositionUpdateRequirements())
	{
		return;
	}

	LastPositionSinceUpdate = SpatialGDK::GetActorSpatialPosition(Actor);
	TimeWhenPositionLastUpdated = NetDriver->GetElapsedTime();

	SendPositionUpdate(Actor, EntityId, LastPositionSinceUpdate);

	if (APlayerController* PlayerController = Cast<APlayerController>(Actor))
	{
		if (APawn* Pawn = PlayerController->GetPawn())
		{
			SendPositionUpdate(Pawn, NetDriver->PackageMap->GetEntityIdFromObject(Pawn), LastPositionSinceUpdate);
		}
	}
}

void USpatialActorChannel::SendPositionUpdate(AActor* InActor, Worker_EntityId InEntityId, const FVector& NewPosition)
{
	if (InEntityId != SpatialConstants::INVALID_ENTITY_ID && NetDriver->HasServerAuthority(InEntityId))
	{
		FWorkerComponentUpdate Update = SpatialGDK::Position::CreatePositionUpdate(SpatialGDK::Coordinates::FromFVector(NewPosition));
		NetDriver->Connection->SendComponentUpdate(InEntityId, &Update);
	}

	for (const auto& Child : InActor->Children)
	{
		SendPositionUpdate(Child, NetDriver->PackageMap->GetEntityIdFromObject(Child), NewPosition);
	}
}

void USpatialActorChannel::RemoveRepNotifiesWithUnresolvedObjs(TArray<GDK_PROPERTY(Property) *>& RepNotifies, const FRepLayout& RepLayout,
															   const FObjectReferencesMap& RefMap, UObject* Object)
{
	// Prevent rep notify callbacks from being issued when unresolved obj references exist inside UStructs.
	// This prevents undefined behaviour when engine rep callbacks are issued where they don't expect unresolved objects in native flow.
	RepNotifies.RemoveAll([&](GDK_PROPERTY(Property) * Property) {
		for (auto& ObjRef : RefMap)
		{
			if (!ensureAlwaysMsgf(ObjRef.Value.ParentIndex >= 0, TEXT("ParentIndex should always be >= 0, but it was %d."),
								  ObjRef.Value.ParentIndex))
			{
				continue;
			}

			// Skip only when there are unresolved refs (FObjectReferencesMap entry contains both mapped and unresolved references).
			if (ObjRef.Value.UnresolvedRefs.Num() == 0)
			{
				continue;
			}

			bool bIsSameRepNotify = RepLayout.Parents[ObjRef.Value.ParentIndex].Property == Property;
			bool bIsArray = RepLayout.Parents[ObjRef.Value.ParentIndex].Property->ArrayDim > 1
							|| GDK_CASTFIELD<GDK_PROPERTY(ArrayProperty)>(Property) != nullptr;
			if (bIsSameRepNotify && !bIsArray)
			{
				UE_LOG(LogSpatialActorChannel, Verbose, TEXT("RepNotify %s on %s ignored due to unresolved Actor"), *Property->GetName(),
					   *Object->GetName());
				return true;
			}
		}
		return false;
	});
}

void USpatialActorChannel::ServerProcessOwnershipChange()
{
	SCOPE_CYCLE_COUNTER(STAT_ServerProcessOwnershipChange);
	{
		if (!IsReadyForReplication() || !IsAuthoritativeServer())
		{
			return;
		}
	}

	// We only want to iterate through child Actors if the connection-owning worker ID or interest bucket component ID
	// for this Actor changes. This bool is used to keep track of whether it has changed, and used to exit early below.
	bool bUpdatedThisActor = false;

	// Changing an Actor's owner can affect its NetConnection so we need to reevaluate this.
	if (!ensureAlwaysMsgf(NetDriver->HasServerAuthority(EntityId),
						  TEXT("Trying to process ownership change on non-auth server. Entity: %lld"), EntityId))
	{
		return;
	}

	TOptional<SpatialGDK::NetOwningClientWorker> CurrentNetOwningClientData =
		SpatialGDK::DeserializeComponent<SpatialGDK::NetOwningClientWorker>(NetDriver->Connection->GetCoordinator(), EntityId);
	const Worker_PartitionId CurrentClientPartitionId = CurrentNetOwningClientData->ClientPartitionId.IsSet()
															? CurrentNetOwningClientData->ClientPartitionId.GetValue()
															: SpatialConstants::INVALID_ENTITY_ID;
	const Worker_PartitionId NewClientConnectionPartitionId = SpatialGDK::GetConnectionOwningPartitionId(Actor);
	if (CurrentClientPartitionId != NewClientConnectionPartitionId)
	{
		// Update the NetOwningClientWorker component.
		CurrentNetOwningClientData->SetPartitionId(NewClientConnectionPartitionId);
		FWorkerComponentUpdate Update = CurrentNetOwningClientData->CreateNetOwningClientWorkerUpdate();
		NetDriver->Connection->SendComponentUpdate(EntityId, &Update);

		// Notify the load balance enforcer of a potential short circuit if we are the delegation authoritative worker.
		NetDriver->LoadBalanceEnforcer->ShortCircuitMaybeRefreshAuthorityDelegation(EntityId);

		bUpdatedThisActor = true;
	}

	TOptional<SpatialGDK::ActorOwnership> CurrentActorOwnershipData =
		SpatialGDK::DeserializeComponent<SpatialGDK::ActorOwnership>(NetDriver->Connection->GetCoordinator(), EntityId);
	const SpatialGDK::ActorOwnership NewActorOwnership = SpatialGDK::ActorOwnership::CreateFromActor(*Actor, *NetDriver->PackageMap);
	if (CurrentActorOwnershipData != NewActorOwnership)
	{
		NetDriver->Connection->GetCoordinator().SendComponentUpdate(EntityId, NewActorOwnership.CreateComponentUpdate(),
																	FSpatialGDKSpanId());

		bUpdatedThisActor = true;
	}

	// Owner changed, update the actor's interest over it.
	NetDriver->ActorSystem->UpdateInterestComponent(Actor);
	SetNeedOwnerInterestUpdate(!NetDriver->InterestFactory->DoOwnersHaveEntityId(Actor));

	// Changing owner can affect which interest bucket the Actor should be in so we need to update it.
	const Worker_ComponentId NewInterestBucketComponentId = NetDriver->ClassInfoManager->ComputeActorInterestComponentId(Actor);
	if (SavedInterestBucketComponentID != NewInterestBucketComponentId)
	{
		NetDriver->ActorSystem->SendInterestBucketComponentChange(EntityId, SavedInterestBucketComponentID, NewInterestBucketComponentId);
		SavedInterestBucketComponentID = NewInterestBucketComponentId;
		bUpdatedThisActor = true;
	}

	// If we haven't updated this Actor, skip attempting to update child Actors.
	if (!bUpdatedThisActor)
	{
		return;
	}

	// Changes to NetConnection and InterestBucket for an Actor also affect all descendants which we
	// need to iterate through.
	for (AActor* Child : Actor->Children)
	{
		Worker_EntityId ChildEntityId = NetDriver->PackageMap->GetEntityIdFromObject(Child);

		if (USpatialActorChannel* Channel = NetDriver->GetActorChannelByEntityId(ChildEntityId))
		{
			Channel->ServerProcessOwnershipChange();
		}
	}
}

void USpatialActorChannel::ClientProcessOwnershipChange(bool bNewNetOwned)
{
	SCOPE_CYCLE_COUNTER(STAT_ClientProcessOwnershipChange);
	if (bNewNetOwned != bNetOwned)
	{
		bNetOwned = bNewNetOwned;

		Actor->SetIsOwnedByClient(bNetOwned);

		if (bNetOwned)
		{
			Actor->OnClientOwnershipGained();
		}
		else
		{
			Actor->OnClientOwnershipLost();
		}
	}
}

void USpatialActorChannel::OnSubobjectDeleted(const FUnrealObjectRef& ObjectRef, UObject* Object,
											  const TWeakObjectPtr<UObject>& ObjectWeakPtr)
{
	CreateSubObjects.Remove(Object);

	NetDriver->ActorSystem->MoveMappedObjectToUnmapped(ObjectRef);
	if (FSpatialObjectRepState* SubObjectRefMap = ObjectReferenceMap.Find(ObjectWeakPtr))
	{
		NetDriver->ActorSystem->CleanupRepStateMap(*SubObjectRefMap);
		ObjectReferenceMap.Remove(ObjectWeakPtr);
	}
}

void USpatialActorChannel::ResetShadowData(FRepLayout& RepLayout, FRepStateStaticBuffer& StaticBuffer, UObject* TargetObject)
{
	if (StaticBuffer.Num() == 0)
	{
		RepLayout.InitRepStateStaticBuffer(StaticBuffer, reinterpret_cast<const uint8*>(TargetObject));
	}
	else
	{
		RepLayout.CopyProperties(StaticBuffer, reinterpret_cast<uint8*>(TargetObject));
	}
}

bool USpatialActorChannel::SatisfiesSpatialPositionUpdateRequirements()
{
	// Check that the Actor satisfies both lower thresholds OR either of the maximum thresholds
	FVector ActorSpatialPosition = SpatialGDK::GetActorSpatialPosition(Actor);
	const float DistanceTravelledSinceLastUpdateSquared = FVector::DistSquared(ActorSpatialPosition, LastPositionSinceUpdate);

	// If the Actor did not travel at all, then we consider its position to be up to date and we early out.
	if (FMath::IsNearlyZero(DistanceTravelledSinceLastUpdateSquared))
	{
		if (APlayerController* PlayerController = Cast<APlayerController>(Actor))
		{
			if (APawn* Pawn = PlayerController->GetPawn())
			{
				int aaa = 1;
			}
		}
		return false;
	}
if (APlayerController* PlayerController = Cast<APlayerController>(Actor))
		{
			if (APawn* Pawn = PlayerController->GetPawn())
			{
				int aaa = 1;
			}
		}
	const float TimeSinceLastPositionUpdate = NetDriver->GetElapsedTime() - TimeWhenPositionLastUpdated;
	const USpatialGDKSettings* SpatialGDKSettings = GetDefault<USpatialGDKSettings>();
	const float SpatialMinimumPositionThresholdSquared = FMath::Square(SpatialGDKSettings->PositionUpdateLowerThresholdCentimeters);
	const float SpatialMaximumPositionThresholdSquared = FMath::Square(SpatialGDKSettings->PositionUpdateThresholdMaxCentimeters);

	if (TimeSinceLastPositionUpdate >= SpatialGDKSettings->PositionUpdateLowerThresholdSeconds
		&& DistanceTravelledSinceLastUpdateSquared >= SpatialMinimumPositionThresholdSquared)
	{
		return true;
	}

	if (TimeSinceLastPositionUpdate >= SpatialGDKSettings->PositionUpdateThresholdMaxSeconds)
	{
		return true;
	}

	if (DistanceTravelledSinceLastUpdateSquared >= SpatialMaximumPositionThresholdSquared)
	{
		return true;
	}

	return false;
}

void FObjectReferencesMapDeleter::operator()(FObjectReferencesMap* Ptr) const
{
	delete Ptr;
}
