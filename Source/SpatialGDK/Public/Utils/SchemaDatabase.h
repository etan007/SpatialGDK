// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "Containers/Map.h"
#include "Containers/StaticArray.h"
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/World.h"
#include "SpatialConstants.h"

#include "SchemaDatabase.generated.h"

// Schema data related to a default Subobject owned by a specific Actor class.
USTRUCT()
struct FActorSpecificSubobjectSchemaData
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(Category = "SpatialGDK", VisibleAnywhere)
	FString ClassPath;

	UPROPERTY(Category = "SpatialGDK", VisibleAnywhere)
	FName Name;

	UPROPERTY(Category = "SpatialGDK", VisibleAnywhere)
	uint32 SchemaComponents[SCHEMA_Count] = {};
};

// Schema data related to an Actor class
USTRUCT()
struct FActorSchemaData
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(Category = "SpatialGDK", VisibleAnywhere)
	FString GeneratedSchemaName;

	UPROPERTY(Category = "SpatialGDK", VisibleAnywhere)
	uint32 SchemaComponents[SCHEMA_Count] = {};

	UPROPERTY(Category = "SpatialGDK", VisibleAnywhere)
	TMap<uint32, FActorSpecificSubobjectSchemaData> SubobjectData;
};

USTRUCT()
struct FDynamicSubobjectSchemaData
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(Category = "SpatialGDK", VisibleAnywhere)
	uint32 SchemaComponents[SCHEMA_Count] = {};
};

// Schema data related to a Subobject class
USTRUCT()
struct FSubobjectSchemaData
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(Category = "SpatialGDK", VisibleAnywhere)
	FString GeneratedSchemaName;

	UPROPERTY(Category = "SpatialGDK", VisibleAnywhere)
	TArray<FDynamicSubobjectSchemaData> DynamicSubobjectComponents;

	FORCEINLINE Worker_ComponentId GetDynamicSubobjectComponentId(int Idx, ESchemaComponentType ComponentType) const
	{
		Worker_ComponentId ComponentId = 0;
		if (Idx < DynamicSubobjectComponents.Num())
		{
			ComponentId = DynamicSubobjectComponents[Idx].SchemaComponents[ComponentType];
		}
		return ComponentId;
	}
};

USTRUCT()
struct FFieldIDs
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	TArray<uint32> FieldIds;
};

USTRUCT()
struct FComponentIDs
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	TArray<uint32> ComponentIDs;
};

UENUM()
enum class ESchemaDatabaseVersion : uint8
{
	BeforeVersionSupportAdded = 0,
	VersionSupportAdded,
	AlwaysWriteRPCAdded,
	InitialOnlyDataAdded,
	FieldIDsAdded,
	HandoverToServerOnlyChanged,

	// Add new versions here

	LatestVersionPlusOne,
	LatestVersion = LatestVersionPlusOne - 1
};

UCLASS()
class SPATIALGDK_API USchemaDatabase : public UDataAsset
{
	GENERATED_BODY()

public:
	USchemaDatabase()
		: NextAvailableComponentId(SpatialConstants::STARTING_GENERATED_COMPONENT_ID)
	{
	}

	UPROPERTY(Category = "SpatialGDK", VisibleAnywhere)
	TMap<FString, FActorSchemaData> ActorClassPathToSchema;

	UPROPERTY(Category = "SpatialGDK", VisibleAnywhere)
	TMap<FString, FSubobjectSchemaData> SubobjectClassPathToSchema;

	UPROPERTY(Category = "SpatialGDK", VisibleAnywhere)
	TMap<FString, uint32> LevelPathToComponentId;

	UPROPERTY(Category = "SpatialGDK", VisibleAnywhere)
	TMap<float, uint32> NetCullDistanceToComponentId;

	UPROPERTY(Category = "SpatialGDK", VisibleAnywhere)
	TSet<uint32> NetCullDistanceComponentIds;

	UPROPERTY(Category = "SpatialGDK", VisibleAnywhere)
	TMap<uint32, FString> ComponentIdToClassPath;

	UPROPERTY(Category = "SpatialGDK", VisibleAnywhere)
	TArray<uint32> LevelComponentIds;

	UPROPERTY(Category = "SpatialGDK", VisibleAnywhere)
	uint32 NextAvailableComponentId;

	UPROPERTY(Category = "SpatialGDK", VisibleAnywhere)
	uint32 SchemaBundleHash;

	// A map from component IDs to an index into the FieldIdsArray.
	UPROPERTY(Category = "SpatialGDK", VisibleAnywhere)
	TMap<uint32, uint32> ComponentIdToFieldIdsIndex;

	UPROPERTY(Category = "SpatialGDK", VisibleAnywhere)
	TArray<FFieldIDs> FieldIdsArray;

	UPROPERTY(Category = "SpatialGDK", VisibleAnywhere)
	TMap<uint32, FComponentIDs> ComponentSetIdToComponentIds;

	UPROPERTY(Category = "SpatialGDK", VisibleAnywhere)
	TMap<ERPCType, uint32> RPCRingBufferSizeMap;

	UPROPERTY(Category = "SpatialGDK", VisibleAnywhere)
	ESchemaDatabaseVersion SchemaDatabaseVersion;
};
