// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Utils/GDKPropertyMacros.h"
#include "Utils/SchemaDatabase.h"

#include <WorkerSDK/improbable/c_worker.h>

#include "SpatialClassInfoManager.generated.h"

FORCEINLINE void ForAllSchemaComponentTypes(TFunction<void(ESchemaComponentType)> Callback)
{
	for (int32 Type = SCHEMA_Begin; Type < SCHEMA_Count; Type++)
	{
		Callback(ESchemaComponentType(Type));
	}
}

FORCEINLINE ESchemaComponentType GetGroupFromCondition(ELifetimeCondition Condition)
{
	static_assert(SCHEMA_Count == 4,
				  "Unexpected number of Schema type components, please make sure GetGroupFromCondition is still correct.");

	switch (Condition)
	{
	case COND_AutonomousOnly:
	case COND_ReplayOrOwner:
	case COND_OwnerOnly:
		return SCHEMA_OwnerOnly;
	case COND_InitialOnly:
		return SCHEMA_InitialOnly;
	case COND_ServerOnly:
		return SCHEMA_ServerOnly;
	default:
		return SCHEMA_Data;
	}
}

struct FRPCInfo
{
	ERPCType Type;
	uint32 Index;
};

struct FInterestPropertyInfo
{
	GDK_PROPERTY(Property) * Property;
	int32 Offset;
};

USTRUCT()
struct FClassInfo
{
	GENERATED_BODY()

	TWeakObjectPtr<UClass> Class;

	// Exists for all classes
	TArray<UFunction*> RPCs;
	TMap<UFunction*, FRPCInfo> RPCInfoMap;
	TArray<FInterestPropertyInfo> InterestProperties;

	// For Actors and default Subobjects belonging to Actors
	Worker_ComponentId SchemaComponents[ESchemaComponentType::SCHEMA_Count] = {};

	// Only for Actors
	TMap<ObjectOffset, TSharedRef<const FClassInfo>> SubobjectInfo;

	// Only for default Subobjects belonging to Actors
	FName SubobjectName;

	// Only true on class FClassInfos that represent a dynamic subobject
	bool bDynamicSubobject = false;

	// Only for Subobject classes
	TArray<TSharedRef<const FClassInfo>> DynamicSubobjectInfo;
};

class USpatialNetDriver;

DECLARE_LOG_CATEGORY_EXTERN(LogSpatialClassInfoManager, Log, All)

UCLASS()
class SPATIALGDK_API USpatialClassInfoManager : public UObject
{
	GENERATED_BODY()

public:
	bool TryInit(USpatialNetDriver* InNetDriver);

	// Checks whether a class is supported and quits the game if not. This is to avoid crashing
	// when running with an out-of-date schema database.
	bool ValidateOrExit_IsSupportedClass(const FString& PathName);

	// Returns true if the class path corresponds to an Actor or Subobject class path in SchemaDatabase
	// In PIE, PathName must be NetworkRemapped (bReading = false)
	bool IsSupportedClass(const FString& PathName) const;

	const FClassInfo& GetOrCreateClassInfoByClass(UClass* Class);
	const FClassInfo& GetOrCreateClassInfoByObject(UObject* Object);
	const FClassInfo& GetClassInfoByComponentId(Worker_ComponentId ComponentId);

	UClass* GetClassByComponentId(Worker_ComponentId ComponentId);
	bool GetOffsetByComponentId(Worker_ComponentId ComponentId, ObjectOffset& OutOffset);
	ESchemaComponentType GetCategoryByComponentId(Worker_ComponentId ComponentId);
	const TArray<Schema_FieldId>& GetFieldIdsByComponentId(Worker_ComponentId ComponentId);

	Worker_ComponentId GetComponentIdForClass(const UClass& Class) const;
	TArray<Worker_ComponentId> GetComponentIdsForClassHierarchy(const UClass& BaseClass, const bool bIncludeDerivedTypes = true) const;

	const FRPCInfo& GetRPCInfo(UObject* Object, UFunction* Function);

	Worker_ComponentId GetComponentIdFromLevelPath(const FString& LevelPath) const;
	bool IsSublevelComponent(Worker_ComponentId ComponentId) const;

	const TMap<float, Worker_ComponentId>& GetNetCullDistanceToComponentIds() const;

	Worker_ComponentId GetComponentIdForNetCullDistance(float NetCullDistance) const;
	Worker_ComponentId ComputeActorInterestComponentId(const AActor* Actor) const;

	bool IsNetCullDistanceComponent(Worker_ComponentId ComponentId) const;

	// Used to check if component is used for qbi tracking only
	bool IsGeneratedQBIMarkerComponent(Worker_ComponentId ComponentId) const;

	// Tries to find ClassInfo corresponding to an unused dynamic subobject on the given entity
	const FClassInfo* GetClassInfoForNewSubobject(const UObject* Object, Worker_EntityId EntityId,
												  USpatialPackageMapClient* PackageMapClient);

	UPROPERTY()
	USchemaDatabase* SchemaDatabase;

	void QuitGame();

private:
	void CreateClassInfoForClass(UClass* Class);
	void TryCreateClassInfoForComponentId(Worker_ComponentId ComponentId);

	void FinishConstructingActorClassInfo(const FString& ClassPath, TSharedRef<FClassInfo>& Info);
	void FinishConstructingSubobjectClassInfo(const FString& ClassPath, TSharedRef<FClassInfo>& Info);

	bool IsComponentIdForTypeValid(const Worker_ComponentId ComponentId, const ESchemaComponentType Type) const;

private:
	UPROPERTY()
	USpatialNetDriver* NetDriver;

	TMap<TWeakObjectPtr<UClass>, TSharedRef<FClassInfo>, FDefaultSetAllocator,
		 TWeakObjectPtrMapKeyFuncs<TWeakObjectPtr<UClass>, TSharedRef<FClassInfo>, false>>
		ClassInfoMap;
	TMap<Worker_ComponentId, TSharedRef<FClassInfo>> ComponentToClassInfoMap;
	TMap<Worker_ComponentId, ObjectOffset> ComponentToOffsetMap;
	TMap<Worker_ComponentId, ESchemaComponentType> ComponentToCategoryMap;

	TOptional<bool> bHandoverActive;
};
