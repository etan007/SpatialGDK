// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "LoadBalancing/LayeredLBStrategy.h"

#include "EngineClasses/SpatialNetDriver.h"
#include "EngineClasses/SpatialWorldSettings.h"
#include "LoadBalancing/GridBasedLBStrategy.h"
#include "Schema/ActorGroupMember.h"
#include "Utils/LayerInfo.h"
#include "Utils/SpatialActorUtils.h"

#include "Templates/Tuple.h"

DEFINE_LOG_CATEGORY(LogLayeredLBStrategy);

ULayeredLBStrategy::ULayeredLBStrategy()
	: Super()
{
}

FString ULayeredLBStrategy::ToString() const
{
	const FName LocalLayerName = GetLocalLayerName();
	const UAbstractLBStrategy* const* LBStrategy = LayerNameToLBStrategy.Find(LocalLayerName);

	FString Description =
		FString::Printf(TEXT("Layered, LocalLayerName = %s, LocalVirtualWorkerId = %d, LayerStrategy = %s"), *LocalLayerName.ToString(),
						LocalVirtualWorkerId, *LBStrategy ? *(*LBStrategy)->ToString() : TEXT("NoStrategy"));

	if (VirtualWorkerIdToLayerName.Num() > 0)
	{
		Description += TEXT(", LayerNamesPerVirtualWorkerId = {");
		for (const auto& Entry : VirtualWorkerIdToLayerName)
		{
			Description += FString::Printf(TEXT("%d = %s, "), Entry.Key, *Entry.Value.ToString());
		}
		if (ensureAlwaysMsgf(Description.Len() > 1, TEXT("Load balancing strategy description should be more than 1 character in length")))
		{
			Description.LeftChopInline(2);
			Description += TEXT("}");
		}
	}
	return Description;
}

void ULayeredLBStrategy::SetLayers(const TArray<FLayerInfo>& WorkerLayers)
{
	check(WorkerLayers.Num() != 0);

	// For each Layer, add a LB Strategy for that layer.
	for (int32 LayerIndex = 0; LayerIndex < WorkerLayers.Num(); ++LayerIndex)
	{
		const FLayerInfo& LayerInfo = WorkerLayers[LayerIndex];
		checkf(*LayerInfo.LoadBalanceStrategy != nullptr,
			   TEXT("WorkerLayer %s does not specify a load balancing strategy (or it cannot be resolved)"), *LayerInfo.Name.ToString());

		UE_LOG(LogLayeredLBStrategy, Log, TEXT("Creating LBStrategy for Layer %s."), *LayerInfo.Name.ToString());

		AddStrategyForLayer(LayerInfo.Name, NewObject<UAbstractLBStrategy>(this, LayerInfo.LoadBalanceStrategy));

		for (const TSoftClassPtr<AActor>& ClassPtr : LayerInfo.ActorClasses)
		{
			UE_LOG(LogLayeredLBStrategy, Log, TEXT(" - Adding class %s."), *ClassPtr.GetAssetName());
			ClassPathToLayerName.Emplace(ClassPtr, LayerInfo.Name);
		}

		LayerData.Emplace(LayerInfo.Name, FLayerData{ LayerInfo.Name, LayerIndex });
	}
}

void ULayeredLBStrategy::SetLocalVirtualWorkerId(VirtualWorkerId InLocalVirtualWorkerId)
{
	if (LocalVirtualWorkerId != SpatialConstants::INVALID_VIRTUAL_WORKER_ID)
	{
		UE_LOG(LogLayeredLBStrategy, Error,
			   TEXT("The Local Virtual Worker Id cannot be set twice. Current value: %d Requested new value: %d"), LocalVirtualWorkerId,
			   InLocalVirtualWorkerId);
		return;
	}

	LocalVirtualWorkerId = InLocalVirtualWorkerId;
	for (const auto& Elem : LayerNameToLBStrategy)
	{
		Elem.Value->SetLocalVirtualWorkerId(InLocalVirtualWorkerId);
	}
}

TSet<VirtualWorkerId> ULayeredLBStrategy::GetVirtualWorkerIds() const
{
	return TSet<VirtualWorkerId>(VirtualWorkerIds);
}

bool ULayeredLBStrategy::ShouldHaveAuthority(const AActor& Actor) const
{
	if (!IsReady())
	{
		UE_LOG(LogLayeredLBStrategy, Warning, TEXT("LayeredLBStrategy not ready to relinquish authority for Actor %s."),
			   *AActor::GetDebugName(&Actor));
		return false;
	}

	const AActor* RootOwner = &Actor;
	while (RootOwner->GetOwner() != nullptr && RootOwner->GetOwner()->GetIsReplicated())
	{
		RootOwner = RootOwner->GetOwner();
	}

	const FName& LayerName = GetLayerNameForActor(*RootOwner);
	if (!LayerNameToLBStrategy.Contains(LayerName))
	{
		UE_LOG(LogLayeredLBStrategy, Error, TEXT("LayeredLBStrategy doesn't have a LBStrategy for Actor %s which is in Layer %s."),
			   *AActor::GetDebugName(RootOwner), *LayerName.ToString());
		return false;
	}

	// If this worker is not responsible for the Actor's layer, just return false.
	if (VirtualWorkerIdToLayerName.Contains(LocalVirtualWorkerId) && VirtualWorkerIdToLayerName[LocalVirtualWorkerId] != LayerName)
	{
		return false;
	}

	return LayerNameToLBStrategy[LayerName]->ShouldHaveAuthority(Actor);
}

VirtualWorkerId ULayeredLBStrategy::WhoShouldHaveAuthority(const AActor& Actor) const
{
	if (!IsReady())
	{
		UE_LOG(LogLayeredLBStrategy, Warning, TEXT("LayeredLBStrategy not ready to decide on authority for Actor %s."),
			   *AActor::GetDebugName(&Actor));
		return SpatialConstants::INVALID_VIRTUAL_WORKER_ID;
	}

	const AActor* RootOwner = &Actor;
	while (RootOwner->GetOwner() != nullptr && RootOwner->GetOwner()->GetIsReplicated())
	{
		RootOwner = RootOwner->GetOwner();
	}

	const FName& LayerName = GetLayerNameForActor(*RootOwner);
	if (!LayerNameToLBStrategy.Contains(LayerName))
	{
		UE_LOG(LogLayeredLBStrategy, Error, TEXT("LayeredLBStrategy doesn't have a LBStrategy for Actor %s which is in Layer %s."),
			   *AActor::GetDebugName(RootOwner), *LayerName.ToString());
		return SpatialConstants::INVALID_VIRTUAL_WORKER_ID;
	}

	const VirtualWorkerId ReturnedWorkerId = LayerNameToLBStrategy[LayerName]->WhoShouldHaveAuthority(*RootOwner);

	UE_LOG(LogLayeredLBStrategy, Log, TEXT("LayeredLBStrategy returning virtual worker id %d for Actor %s."), ReturnedWorkerId,
		   *AActor::GetDebugName(RootOwner));
	return ReturnedWorkerId;
}

SpatialGDK::FActorLoadBalancingGroupId ULayeredLBStrategy::GetActorGroupId(const AActor& Actor) const
{
	const FName ActorLayerName = GetLayerNameForActor(Actor);

	const int32 ActorLayerIndex = LayerData.FindChecked(ActorLayerName).LayerIndex + 1;

	// We're not going deeper inside nested strategies intentionally; LBStrategy, or nesting thereof,
	// won't exist when the Strategy Worker is finished, and GroupIDs are only necessary for it to work.
	return ActorLayerIndex;
}

SpatialGDK::QueryConstraint ULayeredLBStrategy::GetWorkerInterestQueryConstraint(const VirtualWorkerId VirtualWorker) const
{
	if (!VirtualWorkerIdToLayerName.Contains(VirtualWorker))
	{
		UE_LOG(LogLayeredLBStrategy, Error, TEXT("LayeredLBStrategy doesn't have a LBStrategy for worker %d."), VirtualWorker);
		SpatialGDK::QueryConstraint Constraint;
		Constraint.ComponentConstraint = 0;
		return Constraint;
	}

	const FName& LayerName = VirtualWorkerIdToLayerName[VirtualWorker];
	check(LayerNameToLBStrategy.Contains(LayerName));
	return LayerNameToLBStrategy[LayerName]->GetWorkerInterestQueryConstraint(VirtualWorker);
}

bool ULayeredLBStrategy::RequiresHandoverData() const
{
	for (const auto& Elem : LayerNameToLBStrategy)
	{
		if (Elem.Value->RequiresHandoverData())
		{
			return true;
		}
	}
	return false;
}

FVector ULayeredLBStrategy::GetWorkerEntityPosition() const
{
	if (!ensureAlwaysMsgf(IsReady(), TEXT("Called GetWorkerEntityPosition before load balancing strategy was ready")))
	{
		return FVector::ZeroVector;
	}

	if (!VirtualWorkerIdToLayerName.Contains(LocalVirtualWorkerId))
	{
		UE_LOG(LogLayeredLBStrategy, Error, TEXT("LayeredLBStrategy doesn't have a LBStrategy for worker %d."), LocalVirtualWorkerId);
		return FVector::ZeroVector;
	}

	const FName& LayerName = VirtualWorkerIdToLayerName[LocalVirtualWorkerId];

	if (!ensureAlwaysMsgf(LayerNameToLBStrategy.Contains(LayerName),
						  TEXT("Called GetWorkerEntityPosition but couldn't find layer %s in local map"), *LayerName.ToString()))
	{
		return FVector::ZeroVector;
	}

	return LayerNameToLBStrategy[LayerName]->GetWorkerEntityPosition();
}

uint32 ULayeredLBStrategy::GetMinimumRequiredWorkers() const
{
	// The MinimumRequiredWorkers for this strategy is a sum of the required workers for each of the wrapped strategies.
	uint32 MinimumRequiredWorkers = 0;
	for (const auto& Elem : LayerNameToLBStrategy)
	{
		MinimumRequiredWorkers += Elem.Value->GetMinimumRequiredWorkers();
	}

	UE_LOG(LogLayeredLBStrategy, Verbose, TEXT("LayeredLBStrategy needs %d workers to support all layer strategies."),
		   MinimumRequiredWorkers);
	return MinimumRequiredWorkers;
}

void ULayeredLBStrategy::SetVirtualWorkerIds(const VirtualWorkerId& FirstVirtualWorkerId, const VirtualWorkerId& LastVirtualWorkerId)
{
	// If the LayeredLBStrategy wraps { SingletonStrategy, 2x2 grid, Singleton } and is given IDs 1 through 6 it will assign:
	// Singleton : 1
	// Grid : 2 - 5
	// Singleton: 6
	VirtualWorkerId NextWorkerIdToAssign = FirstVirtualWorkerId;
	for (const auto& Elem : LayerNameToLBStrategy)
	{
		UAbstractLBStrategy* LBStrategy = Elem.Value;
		VirtualWorkerId MinimumRequiredWorkers = LBStrategy->GetMinimumRequiredWorkers();

		VirtualWorkerId LastVirtualWorkerIdToAssign = NextWorkerIdToAssign + MinimumRequiredWorkers - 1;
		if (LastVirtualWorkerIdToAssign > LastVirtualWorkerId)
		{
			UE_LOG(LogLayeredLBStrategy, Error,
				   TEXT("LayeredLBStrategy was not given enough VirtualWorkerIds to meet the demands of the layer strategies."));
			return;
		}
		UE_LOG(LogLayeredLBStrategy, Log, TEXT("LayeredLBStrategy assigning VirtualWorkerIds %d to %d to Layer %s"), NextWorkerIdToAssign,
			   LastVirtualWorkerIdToAssign, *Elem.Key.ToString());
		LBStrategy->SetVirtualWorkerIds(NextWorkerIdToAssign, LastVirtualWorkerIdToAssign);

		for (VirtualWorkerId id = NextWorkerIdToAssign; id <= LastVirtualWorkerIdToAssign; id++)
		{
			VirtualWorkerIdToLayerName.Add(id, Elem.Key);
		}

		NextWorkerIdToAssign += MinimumRequiredWorkers;
	}

	// Keep a copy of the VirtualWorkerIds. This is temporary and will be removed in the next PR.
	for (VirtualWorkerId CurrentVirtualWorkerId = FirstVirtualWorkerId; CurrentVirtualWorkerId <= LastVirtualWorkerId;
		 CurrentVirtualWorkerId++)
	{
		VirtualWorkerIds.Add(CurrentVirtualWorkerId);
	}
}

// DEPRECATED
// This is only included because Scavengers uses the function in SpatialStatics that calls this.
// Once they are pick up this code, they should be able to switch to another method and we can remove this.
bool ULayeredLBStrategy::CouldHaveAuthority(const TSubclassOf<AActor> Class) const
{
	if (!ensureAlwaysMsgf(IsReady(), TEXT("Called CouldHaveAuthority before load balancing strategy was ready")))
	{
		return false;
	}

	return *VirtualWorkerIdToLayerName.Find(LocalVirtualWorkerId) == GetLayerNameForClass(Class);
}

UAbstractLBStrategy* ULayeredLBStrategy::GetLBStrategyForVisualRendering() const
{
	// The default strategy is guaranteed to exist as long as the strategy is ready.
	if (!ensureAlwaysMsgf(
			LayerNameToLBStrategy.Contains(SpatialConstants::DefaultLayer),
			TEXT("Load balancing strategy does not contain default layer which is needed to render worker debug visualization. "
				 "Default layer presence should be enforced by MultiWorkerSettings edit validation. Class: %s"),
			*GetNameSafe(this)))
	{
		return nullptr;
	}
	return GetLBStrategyForLayer(SpatialConstants::DefaultLayer);
}

UAbstractLBStrategy* ULayeredLBStrategy::GetLBStrategyForLayer(FName Layer) const
{
	// Editor has the option to display the load balanced zones and could query the strategy anytime.
#ifndef WITH_EDITOR
	if (!ensureAlwaysMsgf(IsReady(), TEXT("Called GetLBStrategyForLayer before load balancing strategy was ready")))
	{
		return nullptr;
	}
#endif

	if (UAbstractLBStrategy* const* Entry = LayerNameToLBStrategy.Find(Layer))
	{
		return *Entry;
	}
	return nullptr;
}

FName ULayeredLBStrategy::GetLocalLayerName() const
{
	if (!IsReady())
	{
		UE_LOG(LogLayeredLBStrategy, Error, TEXT("Tried to get worker layer name before the load balancing strategy was ready."));
		return NAME_None;
	}

	const FName* LocalLayerName = VirtualWorkerIdToLayerName.Find(LocalVirtualWorkerId);
	if (LocalLayerName == nullptr)
	{
		UE_LOG(LogLayeredLBStrategy, Error, TEXT("Load balancing strategy didn't contain mapping between virtual worker ID to layer name."),
			   LocalVirtualWorkerId);
		return NAME_None;
	}

	return *LocalLayerName;
}

FName ULayeredLBStrategy::GetLayerNameForClass(const TSubclassOf<AActor> Class) const
{
	if (Class == nullptr)
	{
		return NAME_None;
	}

	UClass* FoundClass = Class;
	TSoftClassPtr<AActor> ClassPtr = TSoftClassPtr<AActor>(FoundClass);

	while (FoundClass != nullptr && FoundClass->IsChildOf(AActor::StaticClass()))
	{
		if (const FName* Layer = ClassPathToLayerName.Find(ClassPtr))
		{
			const FName LayerHolder = *Layer;
			if (FoundClass != Class)
			{
				ClassPathToLayerName.Add(TSoftClassPtr<AActor>(Class), LayerHolder);
			}
			return LayerHolder;
		}

		FoundClass = FoundClass->GetSuperClass();
		ClassPtr = TSoftClassPtr<AActor>(FoundClass);
	}

	// No mapping found so set and return default actor group.
	ClassPathToLayerName.Emplace(TSoftClassPtr<AActor>(Class), SpatialConstants::DefaultLayer);
	return SpatialConstants::DefaultLayer;
}

bool ULayeredLBStrategy::IsSameWorkerType(const AActor* ActorA, const AActor* ActorB) const
{
	if (ActorA == nullptr || ActorB == nullptr)
	{
		return false;
	}
	return GetLayerNameForClass(ActorA->GetClass()) == GetLayerNameForClass(ActorB->GetClass());
}

FName ULayeredLBStrategy::GetLayerNameForActor(const AActor& Actor) const
{
	return GetLayerNameForClass(Actor.GetClass());
}

void ULayeredLBStrategy::AddStrategyForLayer(const FName& LayerName, UAbstractLBStrategy* LBStrategy)
{
	LayerNameToLBStrategy.Add(LayerName, LBStrategy);
	LayerNameToLBStrategy[LayerName]->Init();
}
