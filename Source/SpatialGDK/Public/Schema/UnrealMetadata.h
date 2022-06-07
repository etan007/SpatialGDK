// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "Schema/Component.h"
#include "Schema/UnrealObjectRef.h"
#include "SpatialConstants.h"
#include "SpatialGDKSettings.h"
#include "Utils/SchemaUtils.h"

#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogSpatialUnrealMetadata, Warning, All);

namespace SpatialGDK
{
struct UnrealMetadata : AbstractMutableComponent
{
	static const Worker_ComponentId ComponentId = SpatialConstants::UNREAL_METADATA_COMPONENT_ID;

	UnrealMetadata() = default;

	UnrealMetadata(const TSchemaOption<FUnrealObjectRef>& InStablyNamedRef, const FString& InClassPath,
				   const TSchemaOption<bool>& InbNetStartup)
		: StablyNamedRef(InStablyNamedRef)
		, ClassPath(InClassPath)
		, bNetStartup(InbNetStartup)
	{
	}

	explicit UnrealMetadata(const Worker_ComponentData& Data)
		: UnrealMetadata(Data.schema_type)
	{
	}

	explicit UnrealMetadata(Schema_ComponentData* Data)
	{
		Schema_Object* ComponentObject = Schema_GetComponentDataFields(Data);

		if (Schema_GetObjectCount(ComponentObject, SpatialConstants::UNREAL_METADATA_STABLY_NAMED_REF_ID) == 1)
		{
			StablyNamedRef = GetObjectRefFromSchema(ComponentObject, SpatialConstants::UNREAL_METADATA_STABLY_NAMED_REF_ID);
		}
		ClassPath = GetStringFromSchema(ComponentObject, SpatialConstants::UNREAL_METADATA_CLASS_PATH_ID);

		if (Schema_GetBoolCount(ComponentObject, SpatialConstants::UNREAL_METADATA_NET_STARTUP_ID) == 1)
		{
			bNetStartup = GetBoolFromSchema(ComponentObject, SpatialConstants::UNREAL_METADATA_NET_STARTUP_ID);
		}
	}

	Worker_ComponentData CreateComponentData() const override
	{
		Worker_ComponentData Data = {};
		Data.component_id = ComponentId;
		Data.schema_type = Schema_CreateComponentData();
		Schema_Object* ComponentObject = Schema_GetComponentDataFields(Data.schema_type);

		if (StablyNamedRef.IsSet())
		{
			AddObjectRefToSchema(ComponentObject, SpatialConstants::UNREAL_METADATA_STABLY_NAMED_REF_ID, StablyNamedRef.GetValue());
		}
		AddStringToSchema(ComponentObject, SpatialConstants::UNREAL_METADATA_CLASS_PATH_ID, ClassPath);
		if (bNetStartup.IsSet())
		{
			Schema_AddBool(ComponentObject, SpatialConstants::UNREAL_METADATA_NET_STARTUP_ID, bNetStartup.GetValue());
		}

		return Data;
	}

	FORCEINLINE UClass* GetNativeEntityClass()
	{
		if (NativeClass.IsValid())
		{
			return NativeClass.Get();
		}

#if !UE_BUILD_SHIPPING
		if (NativeClass.IsStale())
		{
			UE_LOG(LogSpatialUnrealMetadata, Warning, TEXT("UnrealMetadata native class %s unloaded whilst entity in view."), *ClassPath);
		}
#endif
		UClass* Class = FindObject<UClass>(nullptr, *ClassPath, false);

		// Unfortunately StablyNameRef doesn't mean NameStableForNetworking as we add a StablyNameRef for every startup actor (see
		// USpatialSender::CreateEntity)
		// TODO: UNR-2537 Investigate why FindObject can be used the first time the actor comes into view for a client but not subsequent
		// loads.
		if (Class == nullptr && !(StablyNamedRef.IsSet() && bNetStartup.IsSet() && bNetStartup.GetValue()))
		{
			if (GetDefault<USpatialGDKSettings>()->bAsyncLoadNewClassesOnEntityCheckout)
			{
				UE_LOG(LogSpatialUnrealMetadata, Warning,
					   TEXT("Class couldn't be found even though async loading on entity checkout is enabled. Will attempt to load it "
							"synchronously. Class: %s"),
					   *ClassPath);
			}

			Class = LoadObject<UClass>(nullptr, *ClassPath);
		}

		if (Class != nullptr && Class->IsChildOf<AActor>())
		{
			NativeClass = Class;
			return Class;
		}

		return nullptr;
	}

	TSchemaOption<FUnrealObjectRef> StablyNamedRef;
	FString ClassPath;
	TSchemaOption<bool> bNetStartup;

	TWeakObjectPtr<UClass> NativeClass;
};

} // namespace SpatialGDK
