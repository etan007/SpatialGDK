// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "LoadBalancing/OwnershipLockingPolicy.h"

#include "Schema/Component.h"
#include "Utils/SpatialActorUtils.h"

#include "Improbable/SpatialEngineDelegates.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY(LogOwnershipLockingPolicy);

bool UOwnershipLockingPolicy::CanAcquireLock(const AActor* Actor)
{
	if (Actor == nullptr)
	{
		UE_LOG(LogOwnershipLockingPolicy, Error, TEXT("Failed to lock nullptr actor"));
		return false;
	}

	return Actor->Role == ROLE_Authority;
}

ActorLockToken UOwnershipLockingPolicy::AcquireLock(AActor* Actor, FString DebugString)
{
	if (!CanAcquireLock(Actor))
	{
		UE_LOG(LogOwnershipLockingPolicy, Error, TEXT("Called AcquireLock but CanAcquireLock returned false. Actor: %s."),
			   *GetNameSafe(Actor));
		return SpatialConstants::INVALID_ACTOR_LOCK_TOKEN;
	}

	if (MigrationLockElement* ActorLockingState = ActorToLockingState.Find(Actor))
	{
		++ActorLockingState->LockCount;
	}
	else
	{
		// We want to avoid memory leak if a locked actor is deleted.
		// To do this, we register with the Actor OnDestroyed delegate with a function that cleans up the internal map.
		if (!Actor->OnDestroyed.IsAlreadyBound(this, &UOwnershipLockingPolicy::OnExplicitlyLockedActorDeleted))
		{
			Actor->OnDestroyed.AddDynamic(this, &UOwnershipLockingPolicy::OnExplicitlyLockedActorDeleted);
		}

		AActor* OwnershipHierarchyRoot = SpatialGDK::GetTopmostReplicatedOwner(Actor);
		AddOwnershipHierarchyRootInformation(OwnershipHierarchyRoot, Actor);

		ActorToLockingState.Add(Actor, MigrationLockElement{ 1, OwnershipHierarchyRoot });
	}

	UE_LOG(LogOwnershipLockingPolicy, Verbose, TEXT("Acquiring migration lock. Actor: %s. Lock name: %s. Token %lld: Locks held: %d."),
		   *GetNameSafe(Actor), *DebugString, NextToken, ActorToLockingState.Find(Actor)->LockCount);
	TokenToNameAndActor.Emplace(NextToken, LockNameAndActor{ MoveTemp(DebugString), Actor });
	return NextToken++;
}

bool UOwnershipLockingPolicy::ReleaseLock(const ActorLockToken Token)
{
	const LockNameAndActor* NameAndActor = TokenToNameAndActor.Find(Token);
	if (NameAndActor == nullptr)
	{
		UE_LOG(LogOwnershipLockingPolicy, Error, TEXT("Called ReleaseLock for unidentified Actor lock token. Token: %lld."), Token);
		return false;
	}

	AActor* Actor = NameAndActor->Actor;
	const FString& Name = NameAndActor->LockName;
	UE_LOG(LogOwnershipLockingPolicy, Verbose, TEXT("Releasing Actor migration lock. Actor: %s. Token: %lld. Lock name: %s"),
		   *Actor->GetName(), Token, *Name);

	if (!ensureAlwaysMsgf(ActorToLockingState.Contains(Actor),
						  TEXT("Tried to release lock on Actor which wasn't present in locking state map. Actor: %s"), *GetNameSafe(Actor)))
	{
		return false;
	}

	{
		// Reduce the reference count and erase the entry if reduced to 0.
		auto CountIt = ActorToLockingState.CreateKeyIterator(Actor);
		MigrationLockElement& ActorLockingState = CountIt.Value();
		if (ActorLockingState.LockCount == 1)
		{
			UE_LOG(LogOwnershipLockingPolicy, Verbose, TEXT("Actor migration no longer locked. Actor: %s"), *Actor->GetName());
			Actor->OnDestroyed.RemoveDynamic(this, &UOwnershipLockingPolicy::OnExplicitlyLockedActorDeleted);
			RemoveOwnershipHierarchyRootInformation(ActorLockingState.HierarchyRoot, Actor);
			CountIt.RemoveCurrent();
		}
		else
		{
			--ActorLockingState.LockCount;
		}
	}

	TokenToNameAndActor.Remove(Token);

	return true;
}

bool UOwnershipLockingPolicy::IsLocked(const AActor* Actor) const
{
	if (Actor == nullptr)
	{
		UE_LOG(LogOwnershipLockingPolicy, Warning, TEXT("IsLocked called for nullptr"));
		return false;
	}

	// Is this Actor explicitly locked or on a locked hierarchy ownership path.
	if (IsExplicitlyLocked(Actor) || IsLockedHierarchyRoot(Actor))
	{
		return true;
	}

	// Is the hierarchy root of this Actor explicitly locked or on a locked hierarchy ownership path.
	if (AActor* HierarchyRoot = SpatialGDK::GetTopmostReplicatedOwner(Actor))
	{
		return IsExplicitlyLocked(HierarchyRoot) || IsLockedHierarchyRoot(HierarchyRoot);
	}

	return false;
}

int32 UOwnershipLockingPolicy::GetActorLockCount(const AActor* Actor) const
{
	const MigrationLockElement* LockData = ActorToLockingState.Find(Actor);

	if (LockData == nullptr)
	{
		return 0;
	}

	return LockData->LockCount;
}

bool UOwnershipLockingPolicy::IsExplicitlyLocked(const AActor* Actor) const
{
	return ActorToLockingState.Contains(Actor);
}

bool UOwnershipLockingPolicy::IsLockedHierarchyRoot(const AActor* Actor) const
{
	return LockedOwnershipRootActorToExplicitlyLockedActors.Contains(Actor);
}

bool UOwnershipLockingPolicy::AcquireLockFromDelegate(AActor* ActorToLock, const FString& DelegateLockIdentifier)
{
	if (DelegateLockingIdentifierToActorLockToken.Contains(DelegateLockIdentifier))
	{
		UE_LOG(LogOwnershipLockingPolicy, Error,
			   TEXT("AcquireLockFromDelegate: A lock with identifier \"%s\" already exists for actor \"%s\"."), *DelegateLockIdentifier,
			   *GetNameSafe(ActorToLock));
		return false;
	}

	const ActorLockToken LockToken = AcquireLock(ActorToLock, DelegateLockIdentifier);
	if (LockToken == SpatialConstants::INVALID_ACTOR_LOCK_TOKEN)
	{
		UE_LOG(LogOwnershipLockingPolicy, Error,
			   TEXT("AcquireLock called from engine delegate returned an invalid token. Lock identifier: %s, Actor: %s"),
			   *DelegateLockIdentifier, *GetNameSafe(ActorToLock));
		return false;
	}

	DelegateLockingIdentifierToActorLockToken.Add(DelegateLockIdentifier, LockToken);
	return true;
}

bool UOwnershipLockingPolicy::ReleaseLockFromDelegate(AActor* ActorToRelease, const FString& DelegateLockIdentifier)
{
	if (!DelegateLockingIdentifierToActorLockToken.Contains(DelegateLockIdentifier))
	{
		UE_LOG(LogOwnershipLockingPolicy, Error,
			   TEXT("ReleaseLockFromDelegate: Lock identifier \"%s\" has no lock associated with it for actor \"%s\"."),
			   *DelegateLockIdentifier, *GetNameSafe(ActorToRelease));
		return false;
	}
	const ActorLockToken LockToken = DelegateLockingIdentifierToActorLockToken.FindAndRemoveChecked(DelegateLockIdentifier);

	return ReleaseLock(LockToken);
}

void UOwnershipLockingPolicy::OnOwnerUpdated(const AActor* Actor, const AActor* OldOwner)
{
	if (!ensureAlwaysMsgf(Actor != nullptr, TEXT("Attempted to call owner update locking policy callback for nullptr Actor")))
	{
		return;
	}

	// If an explicitly locked Actor is changing owner.
	if (IsExplicitlyLocked(Actor))
	{
		RecalculateLockedActorOwnershipHierarchyInformation(Actor);
	}

	// If a locked hierarchy root is changing owner.
	if (IsLockedHierarchyRoot(Actor))
	{
		RecalculateAllExplicitlyLockedActorsInThisHierarchy(Actor);
	}
	// If an Actor in a locked hierarchy is changing owner (i.e. either the old owner or
	// the root hierarchy of the old owner is the root of a locked hierarchy), we need to
	// recalculate ownership hierarchies of all explicitly locked Actors in that hierarchy.
	else if (OldOwner != nullptr)
	{
		const AActor* OldHierarchyRoot = OldOwner->GetOwner() != nullptr ? SpatialGDK::GetTopmostReplicatedOwner(OldOwner) : OldOwner;
		if (IsLockedHierarchyRoot(OldHierarchyRoot))
		{
			RecalculateAllExplicitlyLockedActorsInThisHierarchy(OldHierarchyRoot);
		}
	}
}

void UOwnershipLockingPolicy::OnExplicitlyLockedActorDeleted(AActor* DestroyedActor)
{
	// Find all tokens for this Actor and unlock.
	for (auto TokenNameActorIterator = TokenToNameAndActor.CreateIterator(); TokenNameActorIterator; ++TokenNameActorIterator)
	{
		if (TokenNameActorIterator->Value.Actor == DestroyedActor)
		{
			TokenNameActorIterator.RemoveCurrent();
		}
	}

	// Delete Actor from local mapping.
	const MigrationLockElement ActorLockingState = ActorToLockingState.FindAndRemoveChecked(DestroyedActor);

	// Update ownership path Actor mapping to remove this Actor.
	RemoveOwnershipHierarchyRootInformation(ActorLockingState.HierarchyRoot, DestroyedActor);
}

void UOwnershipLockingPolicy::OnHierarchyRootActorDeleted(AActor* DeletedHierarchyRoot)
{
	if (!ensureAlwaysMsgf(LockedOwnershipRootActorToExplicitlyLockedActors.Contains(DeletedHierarchyRoot),
						  TEXT("OnHierarchyRootActorDeleted called but couldn't find hierarchy root %s in local map"),
						  *GetNameSafe(DeletedHierarchyRoot)))
	{
		return;
	}

	// For all explicitly locked Actors where this Actor is on the ownership path, recalculate the
	// ownership path information to account for this Actor's deletion.
	RecalculateAllExplicitlyLockedActorsInThisHierarchy(DeletedHierarchyRoot);
	LockedOwnershipRootActorToExplicitlyLockedActors.Remove(DeletedHierarchyRoot);
}

void UOwnershipLockingPolicy::RecalculateAllExplicitlyLockedActorsInThisHierarchy(const AActor* HierarchyRoot)
{
	TArray<const AActor*>* ExplicitlyLockedActorsWithThisActorInOwnershipPath =
		LockedOwnershipRootActorToExplicitlyLockedActors.Find(HierarchyRoot);

	if (!ensureAlwaysMsgf(ExplicitlyLockedActorsWithThisActorInOwnershipPath != nullptr,
						  TEXT("Tried to recalculate hierarchy locking state but couldn't find root Actor %s in map"),
						  *GetNameSafe(HierarchyRoot)))
	{
		return;
	}

	for (const AActor* ExplicitlyLockedActor : *ExplicitlyLockedActorsWithThisActorInOwnershipPath)
	{
		RecalculateLockedActorOwnershipHierarchyInformation(ExplicitlyLockedActor);
	}
}

void UOwnershipLockingPolicy::RecalculateLockedActorOwnershipHierarchyInformation(const AActor* ExplicitlyLockedActor)
{
	// For the old ownership path, update ownership path Actor mapping to explicitly locked Actors to remove this Actor.
	MigrationLockElement* OldHierarchyLockData = ActorToLockingState.Find(ExplicitlyLockedActor);
	if (ensureAlwaysMsgf(OldHierarchyLockData != nullptr,
						 TEXT("Tried to update Actor %s hierarchy locking data but previous hierarchy root wasn't found in local mapping"),
						 *GetNameSafe(ExplicitlyLockedActor)))
	{
		RemoveOwnershipHierarchyRootInformation(OldHierarchyLockData->HierarchyRoot, ExplicitlyLockedActor);
	}

	// For the new ownership path, update ownership path Actor mapping to explicitly locked Actors to include this Actor.
	AActor* NewOwnershipHierarchyRoot = SpatialGDK::GetTopmostReplicatedOwner(ExplicitlyLockedActor);
	ActorToLockingState.FindChecked(ExplicitlyLockedActor).HierarchyRoot = NewOwnershipHierarchyRoot;
	AddOwnershipHierarchyRootInformation(NewOwnershipHierarchyRoot, ExplicitlyLockedActor);
}

void UOwnershipLockingPolicy::RemoveOwnershipHierarchyRootInformation(AActor* HierarchyRoot, const AActor* ExplicitlyLockedActor)
{
	if (HierarchyRoot == nullptr)
	{
		return;
	}

	// Find Actors in this root Actor's hierarchy which are explicitly locked.
	TArray<const AActor*>* ExplicitlyLockedActorsWithThisActorOnPath = LockedOwnershipRootActorToExplicitlyLockedActors.Find(HierarchyRoot);

	if (!ensureAlwaysMsgf(ExplicitlyLockedActorsWithThisActorOnPath != nullptr && ExplicitlyLockedActorsWithThisActorOnPath->Num() > 0,
						  TEXT("Tried to cleanup Actor hierarchy locking information but the hierarchy data was empty or invalid")))
	{
		return;
	}

	// If there's only one explicitly locked Actor in the hierarchy, we're removing the only Actor with this root,
	// so we can stop caring about the root itself. Otherwise, just remove the specific Actor entry in the root's list.
	if (ExplicitlyLockedActorsWithThisActorOnPath->Num() == 1)
	{
		LockedOwnershipRootActorToExplicitlyLockedActors.Remove(HierarchyRoot);
		HierarchyRoot->OnDestroyed.RemoveDynamic(this, &UOwnershipLockingPolicy::OnHierarchyRootActorDeleted);
	}
	else
	{
		ExplicitlyLockedActorsWithThisActorOnPath->Remove(ExplicitlyLockedActor);
	}
}

void UOwnershipLockingPolicy::AddOwnershipHierarchyRootInformation(AActor* HierarchyRoot, const AActor* ExplicitlyLockedActor)
{
	if (HierarchyRoot == nullptr)
	{
		return;
	}

	// For the hierarchy root of an explicitly locked Actor, we store a reference from the hierarchy root Actor back to
	// the explicitly locked Actor, as well as binding a deletion delegate to the hierarchy root Actor.
	TArray<const AActor*>& ExplicitlyLockedActorsWithThisActorOnPath =
		LockedOwnershipRootActorToExplicitlyLockedActors.FindOrAdd(HierarchyRoot);
	ExplicitlyLockedActorsWithThisActorOnPath.AddUnique(ExplicitlyLockedActor);

	if (!HierarchyRoot->OnDestroyed.IsAlreadyBound(this, &UOwnershipLockingPolicy::OnHierarchyRootActorDeleted))
	{
		HierarchyRoot->OnDestroyed.AddDynamic(this, &UOwnershipLockingPolicy::OnHierarchyRootActorDeleted);
	}
}
