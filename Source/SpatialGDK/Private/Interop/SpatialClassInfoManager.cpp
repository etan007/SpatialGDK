// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "Interop/SpatialClassInfoManager.h"

#include "AssetRegistryModule.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "Misc/MessageDialog.h"
#include "Runtime/Launch/Resources/Version.h"
#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"

#if WITH_EDITOR
#include "Kismet/KismetSystemLibrary.h"
#endif

#include "EngineClasses/SpatialNetConnection.h"
#include "EngineClasses/SpatialNetDriver.h"
#include "EngineClasses/SpatialPackageMapClient.h"
#include "EngineClasses/SpatialWorldSettings.h"
#include "LoadBalancing/AbstractLBStrategy.h"
#include "LoadBalancing/SpatialMultiWorkerSettings.h"
#include "Utils/GDKPropertyMacros.h"
#include "Utils/RepLayoutUtils.h"

DEFINE_LOG_CATEGORY(LogSpatialClassInfoManager);

bool USpatialClassInfoManager::TryInit(USpatialNetDriver* InNetDriver)
{
	check(InNetDriver != nullptr);
	NetDriver = InNetDriver;

	FSoftObjectPath SchemaDatabasePath =
		FSoftObjectPath(FPaths::SetExtension(SpatialConstants::SCHEMA_DATABASE_ASSET_PATH, TEXT(".SchemaDatabase")));
	SchemaDatabase = Cast<USchemaDatabase>(SchemaDatabasePath.TryLoad());

	if (SchemaDatabase == nullptr)
	{
		UE_LOG(LogSpatialClassInfoManager, Error,
			   TEXT("SchemaDatabase not found! Please generate schema or turn off SpatialOS networking."));
		QuitGame();
		return false;
	}

	if (SchemaDatabase->SchemaDatabaseVersion < ESchemaDatabaseVersion::LatestVersion)
	{
		UE_LOG(LogSpatialClassInfoManager, Error,
			   TEXT("SchemaDatabase version old! Loaded: %d Expected: %d Please regenerate schema or turn off SpatialOS networking."),
			   SchemaDatabase->SchemaDatabaseVersion, ESchemaDatabaseVersion::LatestVersion);
		QuitGame();
		return false;
	}

	return true;
}

bool USpatialClassInfoManager::ValidateOrExit_IsSupportedClass(const FString& PathName)
{
	if (!IsSupportedClass(PathName))
	{
		UE_LOG(LogSpatialClassInfoManager, Error,
			   TEXT("Could not find class %s in schema database. Double-check whether replication is enabled for this class, the class is "
					"marked as SpatialType, and schema has been generated."),
			   *PathName);
#if !UE_BUILD_SHIPPING
		UE_LOG(LogSpatialClassInfoManager, Error, TEXT("Disconnecting due to no generated schema for %s."), *PathName);
		QuitGame();
#endif //! UE_BUILD_SHIPPING
		return false;
	}

	return true;
}

FORCEINLINE UClass* ResolveClass(FString& ClassPath)
{
	FSoftClassPath SoftClassPath(ClassPath);
	UClass* Class = SoftClassPath.ResolveClass();
	if (Class == nullptr)
	{
		UE_LOG(LogSpatialClassInfoManager, Warning, TEXT("Failed to find class at path %s! Attempting to load it."), *ClassPath);
		Class = SoftClassPath.TryLoadClass<UObject>();
	}
	return Class;
}

ERPCType GetRPCType(UFunction* RemoteFunction)
{
	if (RemoteFunction->HasAnyFunctionFlags(FUNC_NetMulticast))
	{
		return ERPCType::NetMulticast;
	}
	else if (RemoteFunction->HasAnyFunctionFlags(FUNC_NetCrossServer | FUNC_NetWriteFence))
	{
		return ERPCType::CrossServer;
	}
	else if (RemoteFunction->HasAnyFunctionFlags(FUNC_NetReliable))
	{
		if (RemoteFunction->HasAnyFunctionFlags(FUNC_NetClient))
		{
			return ERPCType::ClientReliable;
		}
		else if (RemoteFunction->HasAnyFunctionFlags(FUNC_NetServer))
		{
			return ERPCType::ServerReliable;
		}
	}
	else
	{
		if (RemoteFunction->HasAnyFunctionFlags(FUNC_NetClient))
		{
			return ERPCType::ClientUnreliable;
		}
		else if (RemoteFunction->HasAnyFunctionFlags(FUNC_NetServer))
		{
			if (GetDefault<USpatialGDKSettings>()->bEnableAlwaysWriteRPCs
				&& (RemoteFunction->SpatialFunctionFlags & SPATIALFUNC_AlwaysWrite))
			{
				return ERPCType::ServerAlwaysWrite;
			}
			else
			{
				return ERPCType::ServerUnreliable;
			}
		}
	}

	return ERPCType::Invalid;
}

void USpatialClassInfoManager::CreateClassInfoForClass(UClass* Class)
{
	// Remove PIE prefix on class if it exists to properly look up the class.
	FString ClassPath = Class->GetPathName();
 
	GEngine->NetworkRemapPath(NetDriver->GetSpatialOSNetConnection(), ClassPath, false /*bIsReading*/);
 

	if (!bHandoverActive.IsSet())
	{
		if (NetDriver->LoadBalanceStrategy != nullptr)
		{
			bHandoverActive = NetDriver->LoadBalanceStrategy->RequiresHandoverData();
		}
		else
		{
			UE_LOG(LogSpatialClassInfoManager, Log, TEXT("Load Balancing Strategy not set, handover will be disabled."));
			bHandoverActive = false;
		}
	}

	TSharedRef<FClassInfo> Info = ClassInfoMap.Add(Class, MakeShared<FClassInfo>());
	Info->Class = Class;

	// Note: we have to add Class to ClassInfoMap before quitting, as it is expected to be in there by GetOrCreateClassInfoByClass.
	// Therefore the quitting logic cannot be moved higher up.
	if (!ValidateOrExit_IsSupportedClass(ClassPath))
	{
		return;
	}

	TArray<UFunction*> RelevantClassFunctions = SpatialGDK::GetClassRPCFunctions(Class);

	// Save AlwaysWrite RPCs to validate there's at most one per class.
	TArray<UFunction*> AlwaysWriteRPCs;

	const bool bIsActorClass = Class->IsChildOf<AActor>();

	for (UFunction* RemoteFunction : RelevantClassFunctions)
	{
		ERPCType RPCType = GetRPCType(RemoteFunction);
		checkf(RPCType != ERPCType::Invalid, TEXT("Could not determine RPCType for RemoteFunction: %s"), *GetPathNameSafe(RemoteFunction));

		if (RPCType == ERPCType::ServerAlwaysWrite)
		{
			if (bIsActorClass)
			{
				AlwaysWriteRPCs.Add(RemoteFunction);
			}
			else
			{
				UE_LOG(LogSpatialClassInfoManager, Error,
					   TEXT("Found AlwaysWrite RPC on a subobject class. This is not supported and the RPC will be treated as Unreliable. "
							"Please route it through the owning actor if AlwaysWrite behavior is necessary. Class: %s, function: %s"),
					   *Class->GetPathName(), *RemoteFunction->GetName());
				RPCType = ERPCType::ServerUnreliable;
			}
		}

		FRPCInfo RPCInfo;
		RPCInfo.Type = RPCType;

		// Index is guaranteed to be the same on Clients & Servers since we process remote functions in the same order.
		RPCInfo.Index = Info->RPCs.Num();

		Info->RPCs.Add(RemoteFunction);
		Info->RPCInfoMap.Add(RemoteFunction, RPCInfo);
	}

	if (AlwaysWriteRPCs.Num() > 1)
	{
		UE_LOG(LogSpatialClassInfoManager, Error,
			   TEXT("Found more than 1 function with AlwaysWrite for class. This is not supported and may cause unexpected behavior. "
					"Class: %s, functions:"),
			   *Class->GetPathName());
		for (UFunction* AlwaysWriteRPC : AlwaysWriteRPCs)
		{
			UE_LOG(LogSpatialClassInfoManager, Error, TEXT("%s"), *AlwaysWriteRPC->GetName());
		}
	}

	for (TFieldIterator<GDK_PROPERTY(Property)> PropertyIt(Class); PropertyIt; ++PropertyIt)
	{
		GDK_PROPERTY(Property)* Property = *PropertyIt;

		if (Property->PropertyFlags & CPF_AlwaysInterested)
		{
			for (int32 ArrayIdx = 0; ArrayIdx < PropertyIt->ArrayDim; ++ArrayIdx)
			{
				FInterestPropertyInfo InterestInfo;
				InterestInfo.Offset = Property->GetOffset_ForGC() + Property->ElementSize * ArrayIdx;
				InterestInfo.Property = Property;

				Info->InterestProperties.Add(InterestInfo);
			}
		}
	}

	if (bIsActorClass)
	{
		FinishConstructingActorClassInfo(ClassPath, Info);
	}
	else
	{
		FinishConstructingSubobjectClassInfo(ClassPath, Info);
	}
}

void USpatialClassInfoManager::FinishConstructingActorClassInfo(const FString& ClassPath, TSharedRef<FClassInfo>& Info)
{
	ForAllSchemaComponentTypes([&](ESchemaComponentType Type) {
		Worker_ComponentId ComponentId = SchemaDatabase->ActorClassPathToSchema[ClassPath].SchemaComponents[Type];
		if (IsComponentIdForTypeValid(ComponentId, Type))
		{
			Info->SchemaComponents[Type] = ComponentId;
			ComponentToClassInfoMap.Add(ComponentId, Info);
			ComponentToOffsetMap.Add(ComponentId, 0);
			ComponentToCategoryMap.Add(ComponentId, (ESchemaComponentType)Type);
		}
	});

	for (auto& SubobjectClassDataPair : SchemaDatabase->ActorClassPathToSchema[ClassPath].SubobjectData)
	{
		int32 Offset = SubobjectClassDataPair.Key;
		FActorSpecificSubobjectSchemaData SubobjectSchemaData = SubobjectClassDataPair.Value;

		UClass* SubobjectClass = ResolveClass(SubobjectSchemaData.ClassPath);
		if (SubobjectClass == nullptr)
		{
			UE_LOG(LogSpatialClassInfoManager, Error,
				   TEXT("Failed to resolve the class for subobject %s (class path: %s) on actor class %s! This subobject will not be able "
						"to replicate in Spatial!"),
				   *SubobjectSchemaData.Name.ToString(), *SubobjectSchemaData.ClassPath, *ClassPath);
			continue;
		}

		const FClassInfo& SubobjectInfo = GetOrCreateClassInfoByClass(SubobjectClass);

		// Make a copy of the already made FClassInfo for this specific subobject
		TSharedRef<FClassInfo> ActorSubobjectInfo = MakeShared<FClassInfo>(SubobjectInfo);
		ActorSubobjectInfo->SubobjectName = SubobjectSchemaData.Name;

		ForAllSchemaComponentTypes([&](ESchemaComponentType Type) {
			Worker_ComponentId ComponentId = SubobjectSchemaData.SchemaComponents[Type];
			if (IsComponentIdForTypeValid(ComponentId, Type))
			{
				ActorSubobjectInfo->SchemaComponents[Type] = ComponentId;
				ComponentToClassInfoMap.Add(ComponentId, ActorSubobjectInfo);
				ComponentToOffsetMap.Add(ComponentId, Offset);
				ComponentToCategoryMap.Add(ComponentId, ESchemaComponentType(Type));
			}
		});

		Info->SubobjectInfo.Add(Offset, ActorSubobjectInfo);
	}
}

void USpatialClassInfoManager::FinishConstructingSubobjectClassInfo(const FString& ClassPath, TSharedRef<FClassInfo>& Info)
{
	for (const auto& DynamicSubobjectData : SchemaDatabase->SubobjectClassPathToSchema[ClassPath].DynamicSubobjectComponents)
	{
		// Make a copy of the already made FClassInfo for this dynamic subobject
		TSharedRef<FClassInfo> SpecificDynamicSubobjectInfo = MakeShared<FClassInfo>(Info.Get());
		SpecificDynamicSubobjectInfo->bDynamicSubobject = true;

		int32 Offset = DynamicSubobjectData.SchemaComponents[SCHEMA_Data];
		if (!ensureAlwaysMsgf(Offset != SpatialConstants::INVALID_COMPONENT_ID,
							  TEXT("Failed to get dynamic subobject data offset when constructing subobject. Is Schema up to date?")))
		{
			continue;
		}

		ForAllSchemaComponentTypes([&](ESchemaComponentType Type) {
			Worker_ComponentId ComponentId = DynamicSubobjectData.SchemaComponents[Type];

			if (IsComponentIdForTypeValid(ComponentId, Type))
			{
				SpecificDynamicSubobjectInfo->SchemaComponents[Type] = ComponentId;
				ComponentToClassInfoMap.Add(ComponentId, SpecificDynamicSubobjectInfo);
				ComponentToOffsetMap.Add(ComponentId, Offset);
				ComponentToCategoryMap.Add(ComponentId, ESchemaComponentType(Type));
			}
		});

		Info->DynamicSubobjectInfo.Add(SpecificDynamicSubobjectInfo);
	}
}

bool USpatialClassInfoManager::IsComponentIdForTypeValid(const Worker_ComponentId ComponentId, const ESchemaComponentType Type) const
{
	// If handover is inactive, mark server only components as invalid.
	return ComponentId != SpatialConstants::INVALID_COMPONENT_ID && (Type != SCHEMA_ServerOnly || bHandoverActive.Get(false));
}

void USpatialClassInfoManager::TryCreateClassInfoForComponentId(Worker_ComponentId ComponentId)
{
	if (FString* ClassPath = SchemaDatabase->ComponentIdToClassPath.Find(ComponentId))
	{
		if (UClass* Class = LoadObject<UClass>(nullptr, **ClassPath))
		{
			CreateClassInfoForClass(Class);
		}
	}
}

bool USpatialClassInfoManager::IsSupportedClass(const FString& PathName) const
{
	return SchemaDatabase->ActorClassPathToSchema.Contains(PathName) || SchemaDatabase->SubobjectClassPathToSchema.Contains(PathName);
}

const FClassInfo& USpatialClassInfoManager::GetOrCreateClassInfoByClass(UClass* Class)
{
	if (!ClassInfoMap.Contains(Class))
	{
		CreateClassInfoForClass(Class);
	}

	return ClassInfoMap[Class].Get();
}

const FClassInfo& USpatialClassInfoManager::GetOrCreateClassInfoByObject(UObject* Object)
{
	if (AActor* Actor = Cast<AActor>(Object))
	{
		return GetOrCreateClassInfoByClass(Actor->GetClass());
	}
	else
	{
		check(Object->GetTypedOuter<AActor>() != nullptr);

		FUnrealObjectRef ObjectRef = NetDriver->PackageMap->GetUnrealObjectRefFromObject(Object);

		check(ObjectRef.IsValid());

		return ComponentToClassInfoMap[ObjectRef.Offset].Get();
	}
}

const FClassInfo& USpatialClassInfoManager::GetClassInfoByComponentId(Worker_ComponentId ComponentId)
{
	if (!ComponentToClassInfoMap.Contains(ComponentId))
	{
		TryCreateClassInfoForComponentId(ComponentId);
	}

	TSharedRef<FClassInfo> Info = ComponentToClassInfoMap.FindChecked(ComponentId);
	return Info.Get();
}

UClass* USpatialClassInfoManager::GetClassByComponentId(Worker_ComponentId ComponentId)
{
	TSharedRef<FClassInfo> Info = ComponentToClassInfoMap.FindChecked(ComponentId);
	if (UClass* Class = Info->Class.Get())
	{
		return Class;
	}
	else
	{
		UE_LOG(LogSpatialClassInfoManager, Log,
			   TEXT("Class corresponding to component %d has been unloaded! Will try to reload based on the component id."), ComponentId);

		// The weak pointer to the class stored in the FClassInfo will be the same as the one used as the key in ClassInfoMap, so we can use
		// it to clean up the old entry.
		ClassInfoMap.Remove(Info->Class);

		// The old references in the other maps (ComponentToClassInfoMap etc) will be replaced by reloading the info (as a part of
		// TryCreateClassInfoForComponentId).
		TryCreateClassInfoForComponentId(ComponentId);
		TSharedRef<FClassInfo> NewInfo = ComponentToClassInfoMap.FindChecked(ComponentId);
		if (UClass* NewClass = NewInfo->Class.Get())
		{
			return NewClass;
		}
		else
		{
			UE_LOG(LogSpatialClassInfoManager, Error, TEXT("Could not reload class for component %d!"), ComponentId);
		}
	}

	return nullptr;
}

uint32 USpatialClassInfoManager::GetComponentIdForClass(const UClass& Class) const
{
	if (const FActorSchemaData* ActorSchemaData = SchemaDatabase->ActorClassPathToSchema.Find(Class.GetPathName()))
	{
		return ActorSchemaData->SchemaComponents[SCHEMA_Data];
	}
	return SpatialConstants::INVALID_COMPONENT_ID;
}

TArray<Worker_ComponentId> USpatialClassInfoManager::GetComponentIdsForClassHierarchy(const UClass& BaseClass,
																					  const bool bIncludeDerivedTypes /* = true */) const
{
	TArray<Worker_ComponentId> OutComponentIds;

	check(SchemaDatabase);
	if (bIncludeDerivedTypes)
	{
		for (TObjectIterator<UClass> It; It; ++It)
		{
			const UClass* Class = *It;
			check(Class);
			if (Class->IsChildOf(&BaseClass))
			{
				const Worker_ComponentId ComponentId = GetComponentIdForClass(*Class);
				if (ComponentId != SpatialConstants::INVALID_COMPONENT_ID)
				{
					OutComponentIds.Add(ComponentId);
				}
			}
		}
	}
	else
	{
		const uint32 ComponentId = GetComponentIdForClass(BaseClass);
		if (ComponentId != SpatialConstants::INVALID_COMPONENT_ID)
		{
			OutComponentIds.Add(ComponentId);
		}
	}

	return OutComponentIds;
}

bool USpatialClassInfoManager::GetOffsetByComponentId(Worker_ComponentId ComponentId, ObjectOffset& OutOffset)
{
	if (!ComponentToOffsetMap.Contains(ComponentId))
	{
		TryCreateClassInfoForComponentId(ComponentId);
	}

	if (ObjectOffset* Offset = ComponentToOffsetMap.Find(ComponentId))
	{
		OutOffset = *Offset;
		return true;
	}

	return false;
}

ESchemaComponentType USpatialClassInfoManager::GetCategoryByComponentId(Worker_ComponentId ComponentId)
{
	if (!ComponentToCategoryMap.Contains(ComponentId))
	{
		TryCreateClassInfoForComponentId(ComponentId);
	}

	if (ESchemaComponentType* Category = ComponentToCategoryMap.Find(ComponentId))
	{
		return *Category;
	}

	return ESchemaComponentType::SCHEMA_Invalid;
}

const TArray<Schema_FieldId>& USpatialClassInfoManager::GetFieldIdsByComponentId(Worker_ComponentId ComponentId)
{
	return SchemaDatabase->FieldIdsArray[SchemaDatabase->ComponentIdToFieldIdsIndex[ComponentId]].FieldIds;
}

const FRPCInfo& USpatialClassInfoManager::GetRPCInfo(UObject* Object, UFunction* Function)
{
	check(Object != nullptr && Function != nullptr);

	const FClassInfo& Info = GetOrCreateClassInfoByObject(Object);
	const FRPCInfo* RPCInfoPtr = Info.RPCInfoMap.Find(Function);

	// We potentially have a parent function and need to find the child function.
	// This exists as it's possible in blueprints to explicitly call the parent function.
	if (RPCInfoPtr == nullptr)
	{
		for (auto It = Info.RPCInfoMap.CreateConstIterator(); It; ++It)
		{
			if (It.Key()->GetName() == Function->GetName())
			{
				// Matching child function found. Use this for the remote function call.
				RPCInfoPtr = &It.Value();
				break;
			}
		}
	}
	check(RPCInfoPtr != nullptr);
	return *RPCInfoPtr;
}

Worker_ComponentId USpatialClassInfoManager::GetComponentIdFromLevelPath(const FString& LevelPath) const
{
	FString CleanLevelPath = UWorld::RemovePIEPrefix(LevelPath);
	if (const Worker_ComponentId* ComponentId = SchemaDatabase->LevelPathToComponentId.Find(CleanLevelPath))
	{
		return *ComponentId;
	}
	return SpatialConstants::INVALID_COMPONENT_ID;
}

bool USpatialClassInfoManager::IsSublevelComponent(Worker_ComponentId ComponentId) const
{
	return SchemaDatabase->LevelComponentIds.Contains(ComponentId);
}

const TMap<float, Worker_ComponentId>& USpatialClassInfoManager::GetNetCullDistanceToComponentIds() const
{
	return SchemaDatabase->NetCullDistanceToComponentId;
}

const FClassInfo* USpatialClassInfoManager::GetClassInfoForNewSubobject(const UObject* Object, Worker_EntityId EntityId,
																		USpatialPackageMapClient* PackageMapClient)
{
	const FClassInfo* Info = nullptr;

	const FClassInfo& SubobjectInfo = GetOrCreateClassInfoByClass(Object->GetClass());

	// Find the first ClassInfo relating to a dynamic subobject
	// which has not been used on this entity.
	for (const auto& DynamicSubobjectInfo : SubobjectInfo.DynamicSubobjectInfo)
	{
		if (!PackageMapClient->GetObjectFromUnrealObjectRef(FUnrealObjectRef(EntityId, DynamicSubobjectInfo->SchemaComponents[SCHEMA_Data]))
				 .IsValid())
		{
			Info = &DynamicSubobjectInfo.Get();
			break;
		}
	}

	// If all ClassInfos are used up, we error.
	if (Info == nullptr)
	{
		const AActor* Actor = Cast<AActor>(PackageMapClient->GetObjectFromEntityId(EntityId));
		UE_LOG(LogSpatialPackageMap, Error,
			   TEXT("Too many dynamic subobjects of type %s attached to Actor %s! Please increase"
					" the max number of dynamically attached subobjects per class in the SpatialOS runtime settings."),
			   *Object->GetClass()->GetName(), *GetNameSafe(Actor));
	}

	return Info;
}

Worker_ComponentId USpatialClassInfoManager::GetComponentIdForNetCullDistance(float NetCullDistance) const
{
	if (const uint32* ComponentId = SchemaDatabase->NetCullDistanceToComponentId.Find(NetCullDistance))
	{
		return *ComponentId;
	}
	return SpatialConstants::INVALID_COMPONENT_ID;
}

bool USpatialClassInfoManager::IsNetCullDistanceComponent(Worker_ComponentId ComponentId) const
{
	return SchemaDatabase->NetCullDistanceComponentIds.Contains(ComponentId);
}

bool USpatialClassInfoManager::IsGeneratedQBIMarkerComponent(Worker_ComponentId ComponentId) const
{
	return IsSublevelComponent(ComponentId) || IsNetCullDistanceComponent(ComponentId)
		   || SpatialConstants::IsEntityCompletenessComponent(ComponentId);
}

void USpatialClassInfoManager::QuitGame()
{
#if WITH_EDITOR
	// There is no C++ method to quit the current game, so using the Blueprint's QuitGame() that is calling ConsoleCommand("quit")
	// Note: don't use RequestExit() in Editor since it would terminate the Engine loop
	UKismetSystemLibrary::QuitGame(NetDriver->GetWorld(), nullptr, EQuitPreference::Quit, false);

#else
	FGenericPlatformMisc::RequestExit(false);
#endif
}

Worker_ComponentId USpatialClassInfoManager::ComputeActorInterestComponentId(const AActor* Actor) const
{
	if (!ensureAlwaysMsgf(Actor != nullptr, TEXT("Trying to compute Actor interest component ID for nullptr Actor")))
	{
		return SpatialConstants::INVALID_COMPONENT_ID;
	}

	const AActor* ActorForRelevancy = Actor;
	// bAlwaysRelevant takes precedence over bNetUseOwnerRelevancy - see AActor::IsNetRelevantFor
	while (!ActorForRelevancy->bAlwaysRelevant && ActorForRelevancy->bNetUseOwnerRelevancy && ActorForRelevancy->GetOwner() != nullptr)
	{
		ActorForRelevancy = ActorForRelevancy->GetOwner();
	}

	if (ActorForRelevancy->bAlwaysRelevant)
	{
		if (ActorForRelevancy->GetClass()->HasAnySpatialClassFlags(SPATIALCLASS_ServerOnly))
		{
			return SpatialConstants::SERVER_ONLY_ALWAYS_RELEVANT_COMPONENT_ID;
		}
		else
		{
			return SpatialConstants::ALWAYS_RELEVANT_COMPONENT_ID;
		}
	}

	checkf(!Actor->IsA<APlayerController>() || Actor->bOnlyRelevantToOwner,
		   TEXT("Player controllers must have bOnlyRelevantToOwner enabled."));
	// Don't add NCD component to actors only relevant to their owner (player controllers etc.) and server only actors
	// as we don't want clients to otherwise gain interest in them.
	if (GetDefault<USpatialGDKSettings>()->bEnableNetCullDistanceInterest && !Actor->bOnlyRelevantToOwner
		&& !Actor->GetClass()->HasAnySpatialClassFlags(SPATIALCLASS_ServerOnly))
	{
		Worker_ComponentId NCDComponentId = GetComponentIdForNetCullDistance(ActorForRelevancy->NetCullDistanceSquared);
		if (NCDComponentId != SpatialConstants::INVALID_COMPONENT_ID)
		{
			return NCDComponentId;
		}

		const AActor* DefaultActor = ActorForRelevancy->GetClass()->GetDefaultObject<AActor>();
		if (ActorForRelevancy->NetCullDistanceSquared != DefaultActor->NetCullDistanceSquared)
		{
			UE_LOG(LogSpatialClassInfoManager, Error,
				   TEXT("Could not find Net Cull Distance Component for distance %f, processing Actor %s via %s, because its Net Cull "
						"Distance is different from its default one."),
				   ActorForRelevancy->NetCullDistanceSquared, *Actor->GetPathName(), *ActorForRelevancy->GetPathName());

			return ComputeActorInterestComponentId(DefaultActor);
		}
		else
		{
			UE_LOG(
				LogSpatialClassInfoManager, Error,
				TEXT("Could not find Net Cull Distance Component for distance %f, processing Actor %s via %s. Have you generated schema?"),
				ActorForRelevancy->NetCullDistanceSquared, *Actor->GetPathName(), *ActorForRelevancy->GetPathName());
		}
	}
	return SpatialConstants::INVALID_COMPONENT_ID;
}
