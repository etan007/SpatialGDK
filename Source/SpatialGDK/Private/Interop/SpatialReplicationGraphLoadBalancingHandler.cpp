// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "SpatialReplicationGraphLoadBalancingHandler.h"

#include "EngineClasses/SpatialReplicationGraph.h"

FSpatialReplicationGraphLoadBalancingContext::FSpatialReplicationGraphLoadBalancingContext(USpatialNetDriver* InNetDriver,
																						   USpatialReplicationGraph* InReplicationGraph,
																						   FPerConnectionActorInfoMap& InInfoMap,
																						   FPrioritizedRepList& InRepList)
	: NetDriver(InNetDriver)
	, ReplicationGraph(InReplicationGraph)
	, InfoMap(InInfoMap)
	, ActorsToReplicate(InRepList)
{
}

FSpatialReplicationGraphLoadBalancingContext::FRepListArrayAdaptor FSpatialReplicationGraphLoadBalancingContext::GetActorsBeingReplicated()
{
	return FRepListArrayAdaptor(ActorsToReplicate);
}

void FSpatialReplicationGraphLoadBalancingContext::RemoveAdditionalActor(AActor* Actor)
{
	AdditionalActorsToReplicate.Remove(Actor);
}

void FSpatialReplicationGraphLoadBalancingContext::AddActorToReplicate(AActor* Actor)
{
	ReplicationGraph->ForceNetUpdate(Actor);
	AdditionalActorsToReplicate.Add(Actor);
}


const FGlobalActorReplicationInfo::FDependantListType& FSpatialReplicationGraphLoadBalancingContext::GetDependentActors(AActor* Actor)
{
	static FGlobalActorReplicationInfo::FDependantListType EmptyList;

	if (FGlobalActorReplicationInfo* GlobalActorInfo = ReplicationGraph->GetGlobalActorReplicationInfoMap().Find(Actor))
	{
		return GlobalActorInfo->GetDependentActorList();
	}
	return EmptyList;
}

EActorMigrationResult FSpatialReplicationGraphLoadBalancingContext::IsActorReadyForMigration(AActor* Actor)
{
	if (!Actor->HasAuthority())
	{
		return EActorMigrationResult::NotAuthoritative;
	}

	if (!Actor->IsActorReady())
	{
		return EActorMigrationResult::NotReady;
	}

	// The following checks are extracted from UReplicationGraph::ReplicateActorListsForConnections_Default
	// More accurately, from the loop with the section named NET_ReplicateActors_PrioritizeForConnection
	// The part called "Distance Scaling" is ignored, since it is SpatialOS's job.

	if (!Actor->GetClass()->HasAnySpatialClassFlags(SPATIALCLASS_SpatialType))
	{
		return EActorMigrationResult::NoSpatialClassFlags;
	}

	FConnectionReplicationActorInfo& ConnectionData = InfoMap.FindOrAdd(Actor);
	if (ConnectionData.bDormantOnConnection)
	{
		return EActorMigrationResult::DormantOnConnection;
	}

	return EActorMigrationResult::Success;
}
