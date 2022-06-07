// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "SpatialFunctionalTest.h"

#include "Schema/ActorGroupMember.h"
#include "Schema/ActorSetMember.h"

#include "LoadBalancing/SpatialMultiWorkerSettings.h"

#include "TestMaps/GeneratedTestMap.h"

#include "SpatialTestLoadBalancingData.generated.h"

UCLASS()
class USpatialTestLoadBalancingDataTestMap : public UGeneratedTestMap
{
	GENERATED_BODY()

	USpatialTestLoadBalancingDataTestMap();

	virtual void CreateCustomContentForMap() override;
};

UCLASS(HideDropdown)
class USpatialTestLoadBalancingDataMultiWorkerSettings : public USpatialMultiWorkerSettings
{
	GENERATED_BODY()

	USpatialTestLoadBalancingDataMultiWorkerSettings();
};

UCLASS()
class ASpatialTestLoadBalancingDataActor : public AActor
{
	GENERATED_BODY()
public:
	ASpatialTestLoadBalancingDataActor();
};

UCLASS()
class ASpatialTestLoadBalancingDataOffloadedActor : public AActor
{
	GENERATED_BODY()
public:
	ASpatialTestLoadBalancingDataOffloadedActor();
};

UCLASS()
class ASpatialTestLoadBalancingDataZonedActor : public AActor
{
	GENERATED_BODY()
public:
	ASpatialTestLoadBalancingDataZonedActor();
};

/*
 * This is a low-level test to verify that load balancing components are written correctly.
 * It should be replaced by a higher-level test verifying that load balancing works once
 * we get to implementing the Strategy worker.
 */
UCLASS()
class ASpatialTestLoadBalancingData : public ASpatialFunctionalTest
{
	GENERATED_BODY()

	virtual void PrepareTest() override;

	template <typename TComponent>
	TOptional<TComponent> GetSpatialComponent(const AActor* Actor) const;

	TOptional<SpatialGDK::ActorSetMember> GetActorSetData(const AActor* Actor) const;
	TOptional<SpatialGDK::ActorGroupMember> GetActorGroupData(const AActor* Actor) const;

	TWeakObjectPtr<ASpatialTestLoadBalancingDataActor> TargetActor;
	TWeakObjectPtr<ASpatialTestLoadBalancingDataOffloadedActor> TargetOffloadedActor;
	TArray<TWeakObjectPtr<ASpatialTestLoadBalancingDataZonedActor>> TargetZonedActors;
};
