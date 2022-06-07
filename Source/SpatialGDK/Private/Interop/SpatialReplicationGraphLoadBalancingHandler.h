// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "Utils/SpatialLoadBalancingHandler.h"

#include "ReplicationGraphTypes.h"

class USpatialReplicationGraph;

// Specialization of the load balancing handler for the ReplicationGraph.
struct FSpatialReplicationGraphLoadBalancingContext
{
	FSpatialReplicationGraphLoadBalancingContext(USpatialNetDriver* InNetDriver, USpatialReplicationGraph* InReplicationGraph,
												 FPerConnectionActorInfoMap& InfoMap, FPrioritizedRepList& InRepList);

	struct FRepListArrayAdaptor
	{
		struct Iterator
		{
			Iterator(TArray<FPrioritizedRepList::FItem>::RangedForIteratorType Iterator)
				: IteratorImpl(Iterator)
			{
			}

			AActor* operator*() const { return (*IteratorImpl).Actor; }
			void operator++() { ++IteratorImpl; }
			bool operator!=(Iterator const& iRHS) const { return IteratorImpl != iRHS.IteratorImpl; }

			TArray<FPrioritizedRepList::FItem>::RangedForIteratorType IteratorImpl;
		};

		FRepListArrayAdaptor(FPrioritizedRepList& InRepList)
			: RepList(InRepList)
		{
		}

		Iterator begin() { return Iterator(RepList.Items.begin()); }
		Iterator end() { return Iterator(RepList.Items.end()); }

		FPrioritizedRepList& RepList;
	};

	FRepListArrayAdaptor GetActorsBeingReplicated();

	void RemoveAdditionalActor(AActor* Actor);

	void AddActorToReplicate(AActor* Actor);

 
	const FGlobalActorReplicationInfo::FDependantListType& GetDependentActors(AActor* Actor);
 

	EActorMigrationResult IsActorReadyForMigration(AActor* Actor);

	USpatialNetDriver* NetDriver;
	USpatialReplicationGraph* ReplicationGraph;
	FPerConnectionActorInfoMap& InfoMap;
	FPrioritizedRepList& ActorsToReplicate;
	TSet<AActor*> AdditionalActorsToReplicate;
};
