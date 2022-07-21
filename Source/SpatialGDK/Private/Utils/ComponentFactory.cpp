// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "Utils/ComponentFactory.h"

#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/World.h"
#include "UObject/TextProperty.h"

#include "EngineClasses/SpatialActorChannel.h"
#include "EngineClasses/SpatialFastArrayNetSerialize.h"
#include "EngineClasses/SpatialNetBitWriter.h"
#include "EngineClasses/SpatialNetDriver.h"
#include "EngineClasses/SpatialPackageMapClient.h"
#include "Net/NetworkProfiler.h"
#include "Schema/Interest.h"
#include "SpatialConstants.h"
#include "SpatialGDKSettings.h"
#include "Utils/GDKPropertyMacros.h"
#include "Utils/InterestFactory.h"
#include "Utils/RepLayoutUtils.h"

DEFINE_LOG_CATEGORY(LogComponentFactory);

DECLARE_CYCLE_STAT(TEXT("Factory ProcessPropertyUpdates"), STAT_FactoryProcessPropertyUpdates, STATGROUP_SpatialNet);
DECLARE_CYCLE_STAT(TEXT("Factory ProcessFastArrayUpdate"), STAT_FactoryProcessFastArrayUpdate, STATGROUP_SpatialNet);

namespace SpatialGDK
{
ComponentFactory::ComponentFactory(bool bInterestDirty, USpatialNetDriver* InNetDriver)
	: NetDriver(InNetDriver)
	, PackageMap(InNetDriver->PackageMap)
	, ClassInfoManager(InNetDriver->ClassInfoManager)
	, bInterestHasChanged(bInterestDirty)
	, bInitialOnlyDataWritten(false)
	, bInitialOnlyReplicationEnabled(GetDefault<USpatialGDKSettings>()->bEnableInitialOnlyReplicationCondition)
{
}

uint32 ComponentFactory::FillSchemaObject(Schema_Object* ComponentObject, UObject* Object, const FRepChangeState& Changes,
										  ESchemaComponentType PropertyGroup, bool bIsInitialData,
										  TArray<Schema_FieldId>* ClearedIds /*= nullptr*/)
{
	SCOPE_CYCLE_COUNTER(STAT_FactoryProcessPropertyUpdates);

	const uint32 BytesStart = Schema_GetWriteBufferLength(ComponentObject);

	// Populate the replicated data component updates from the replicated property changelist.
	if (Changes.RepChanged.Num() > 0)
	{
		FChangelistIterator ChangelistIterator(Changes.RepChanged, 0);
		FRepHandleIterator HandleIterator(static_cast<UStruct*>(Changes.RepLayout.GetOwner()), ChangelistIterator, Changes.RepLayout.Cmds,
										  Changes.RepLayout.BaseHandleToCmdIndex, 0, 1, 0, Changes.RepLayout.Cmds.Num() - 1);
		while (HandleIterator.NextHandle())
		{
			const FRepLayoutCmd& Cmd = Changes.RepLayout.Cmds[HandleIterator.CmdIndex];
			const FRepParentCmd& Parent = Changes.RepLayout.Parents[Cmd.ParentIndex];

			if (GetGroupFromCondition(Parent.Condition) == PropertyGroup)
			{
				const uint8* Data = (uint8*)Object + Cmd.Offset;

				bool bProcessedFastArrayProperty = false;

#if USE_NETWORK_PROFILER
				const uint32 ProfilerBytesStart = Schema_GetWriteBufferLength(ComponentObject);
#endif

				if (Cmd.Type == ERepLayoutCmdType::DynamicArray)
				{
					GDK_PROPERTY(ArrayProperty)* ArrayProperty = GDK_CASTFIELD<GDK_PROPERTY(ArrayProperty)>(Cmd.Property);

					// Check if this is a FastArraySerializer array and if so, call our custom delta serialization
					if (UScriptStruct* NetDeltaStruct = GetFastArraySerializerProperty(ArrayProperty))
					{
						SCOPE_CYCLE_COUNTER(STAT_FactoryProcessFastArrayUpdate);

						FSpatialNetBitWriter ValueDataWriter(PackageMap);

						if (FSpatialNetDeltaSerializeInfo::DeltaSerializeWrite(NetDriver, ValueDataWriter, Object, Parent.ArrayIndex,
																			   Parent.Property, NetDeltaStruct)
							|| bIsInitialData)
						{
							AddBytesToSchema(ComponentObject, HandleIterator.Handle, ValueDataWriter);
						}

						bProcessedFastArrayProperty = true;
					}
				}

				if (!bProcessedFastArrayProperty)
				{
					AddProperty(ComponentObject, HandleIterator.Handle, Cmd.Property, Data, ClearedIds);
				}

#if USE_NETWORK_PROFILER
				/**
				 *  a good proxy for how many bits are being sent for a property. Reasons for why it might not be fully accurate:
						- the serialized size of a message is just the body contents. Typically something will send the message with the
				 length prefixed, which might be varint encoded, and you pushing the size over some size can cause the encoding of the
				 length be bigger
						- similarly, if you push the message over some size it can cause fragmentation which means you now have to pay for
				 the headers again
						- if there is any compression or anything else going on, the number of bytes actually transferred because of this
				 data can differ
						- lastly somewhat philosophical question of who pays for the overhead of a packet and whether you attribute a part
				 of it to each field or attribute it to the update itself, but I assume you care a bit less about this
				 */
				const uint32 ProfilerBytesEnd = Schema_GetWriteBufferLength(ComponentObject);
				NETWORK_PROFILER(
					GNetworkProfiler.TrackReplicateProperty(Cmd.Property, (ProfilerBytesEnd - ProfilerBytesStart) * CHAR_BIT, nullptr));
#endif
			}

			if (Cmd.Type == ERepLayoutCmdType::DynamicArray)
			{
				if (!HandleIterator.JumpOverArray())
				{
					break;
				}
			}
		}
	}

	const uint32 BytesEnd = Schema_GetWriteBufferLength(ComponentObject);
	

	return BytesEnd - BytesStart;
	
	return 0;
}

void ComponentFactory::AddProperty(Schema_Object* Object, Schema_FieldId FieldId, GDK_PROPERTY(Property) * Property, const uint8* Data,
								   TArray<Schema_FieldId>* ClearedIds)
{
	if (GDK_PROPERTY(StructProperty)* StructProperty = GDK_CASTFIELD<GDK_PROPERTY(StructProperty)>(Property))
	{
		UScriptStruct* Struct = StructProperty->Struct;
		FSpatialNetBitWriter ValueDataWriter(PackageMap);
		bool bHasUnmapped = false;

		if (Struct->StructFlags & STRUCT_NetSerializeNative)
		{
			UScriptStruct::ICppStructOps* CppStructOps = Struct->GetCppStructOps();
			check(CppStructOps); // else should not have STRUCT_NetSerializeNative
			bool bSuccess = true;
			if (!CppStructOps->NetSerialize(ValueDataWriter, PackageMap, bSuccess, const_cast<uint8*>(Data)))
			{
				bHasUnmapped = true;
			}

			// Check the success of the serialization and print a warning if it failed. This is how native handles failed serialization.
			if (!bSuccess)
			{
				UE_LOG(LogComponentFactory, Warning, TEXT("AddProperty: NetSerialize %s failed."), *Struct->GetFullName());
				return;
			}
		}
		else
		{
			TSharedPtr<FRepLayout> RepLayout = NetDriver->GetStructRepLayout(Struct);

			RepLayout_SerializePropertiesForStruct(*RepLayout, ValueDataWriter, PackageMap, const_cast<uint8*>(Data), bHasUnmapped);
		}

		AddBytesToSchema(Object, FieldId, ValueDataWriter);
	}
	else if (GDK_PROPERTY(BoolProperty)* BoolProperty = GDK_CASTFIELD<GDK_PROPERTY(BoolProperty)>(Property))
	{
		Schema_AddBool(Object, FieldId, (uint8)BoolProperty->GetPropertyValue(Data));
	}
	else if (GDK_PROPERTY(FloatProperty)* FloatProperty = GDK_CASTFIELD<GDK_PROPERTY(FloatProperty)>(Property))
	{
		Schema_AddFloat(Object, FieldId, FloatProperty->GetPropertyValue(Data));
	}
	else if (GDK_PROPERTY(DoubleProperty)* DoubleProperty = GDK_CASTFIELD<GDK_PROPERTY(DoubleProperty)>(Property))
	{
		Schema_AddDouble(Object, FieldId, DoubleProperty->GetPropertyValue(Data));
	}
	else if (GDK_PROPERTY(Int8Property)* Int8Property = GDK_CASTFIELD<GDK_PROPERTY(Int8Property)>(Property))
	{
		Schema_AddInt32(Object, FieldId, (int32)Int8Property->GetPropertyValue(Data));
	}
	else if (GDK_PROPERTY(Int16Property)* Int16Property = GDK_CASTFIELD<GDK_PROPERTY(Int16Property)>(Property))
	{
		Schema_AddInt32(Object, FieldId, (int32)Int16Property->GetPropertyValue(Data));
	}
	else if (GDK_PROPERTY(IntProperty)* IntProperty = GDK_CASTFIELD<GDK_PROPERTY(IntProperty)>(Property))
	{
		Schema_AddInt32(Object, FieldId, IntProperty->GetPropertyValue(Data));
	}
	else if (GDK_PROPERTY(Int64Property)* Int64Property = GDK_CASTFIELD<GDK_PROPERTY(Int64Property)>(Property))
	{
		Schema_AddInt64(Object, FieldId, Int64Property->GetPropertyValue(Data));
	}
	else if (GDK_PROPERTY(ByteProperty)* ByteProperty = GDK_CASTFIELD<GDK_PROPERTY(ByteProperty)>(Property))
	{
		Schema_AddUint32(Object, FieldId, (uint32)ByteProperty->GetPropertyValue(Data));
	}
	else if (GDK_PROPERTY(UInt16Property)* UInt16PropertyPtr = GDK_CASTFIELD<GDK_PROPERTY(UInt16Property)>(Property))
	{
		Schema_AddUint32(Object, FieldId, (uint32)UInt16PropertyPtr->GetPropertyValue(Data));
	}
	else if (GDK_PROPERTY(UInt32Property)* UInt32PropertyPtr = GDK_CASTFIELD<GDK_PROPERTY(UInt32Property)>(Property))
	{
		Schema_AddUint32(Object, FieldId, UInt32PropertyPtr->GetPropertyValue(Data));
	}
	else if (GDK_PROPERTY(UInt64Property)* UInt64PropertyPtr = GDK_CASTFIELD<GDK_PROPERTY(UInt64Property)>(Property))
	{
		Schema_AddUint64(Object, FieldId, UInt64PropertyPtr->GetPropertyValue(Data));
	}
	else if (GDK_PROPERTY(ObjectPropertyBase)* ObjectProperty = GDK_CASTFIELD<GDK_PROPERTY(ObjectPropertyBase)>(Property))
	{
		if (GDK_CASTFIELD<GDK_PROPERTY(SoftObjectProperty)>(Property))
		{
			const FSoftObjectPtr* ObjectPtr = reinterpret_cast<const FSoftObjectPtr*>(Data);

			AddObjectRefToSchema(Object, FieldId, FUnrealObjectRef::FromSoftObjectPath(ObjectPtr->ToSoftObjectPath()));
		}
		else
		{
			UObject* ObjectValue = ObjectProperty->GetObjectPropertyValue(Data);

			if (ObjectProperty->PropertyFlags & CPF_AlwaysInterested)
			{
				bInterestHasChanged = true;
			}
			AddObjectRefToSchema(Object, FieldId, FUnrealObjectRef::FromObjectPtr(ObjectValue, PackageMap));
		}
	}
	else if (GDK_PROPERTY(NameProperty)* NameProperty = GDK_CASTFIELD<GDK_PROPERTY(NameProperty)>(Property))
	{
		AddStringToSchema(Object, FieldId, NameProperty->GetPropertyValue(Data).ToString());
	}
	else if (GDK_PROPERTY(StrProperty)* StrProperty = GDK_CASTFIELD<GDK_PROPERTY(StrProperty)>(Property))
	{
		AddStringToSchema(Object, FieldId, StrProperty->GetPropertyValue(Data));
	}
	else if (GDK_PROPERTY(TextProperty)* TextProperty = GDK_CASTFIELD<GDK_PROPERTY(TextProperty)>(Property))
	{
		AddStringToSchema(Object, FieldId, TextProperty->GetPropertyValue(Data).ToString());
	}
	else if (GDK_PROPERTY(ArrayProperty)* ArrayProperty = GDK_CASTFIELD<GDK_PROPERTY(ArrayProperty)>(Property))
	{
		FScriptArrayHelper ArrayHelper(ArrayProperty, Data);
		for (int i = 0; i < ArrayHelper.Num(); i++)
		{
			AddProperty(Object, FieldId, ArrayProperty->Inner, ArrayHelper.GetRawPtr(i), ClearedIds);
		}

		if (ArrayHelper.Num() > 0 || (ArrayHelper.Num() == 0 && ClearedIds))
		{
			if (ArrayProperty->Inner->IsA<GDK_PROPERTY(ObjectPropertyBase)>())
			{
				if (ArrayProperty->PropertyFlags & CPF_AlwaysInterested)
				{
					bInterestHasChanged = true;
				}
			}
		}

		if (ArrayHelper.Num() == 0 && ClearedIds)
		{
			ClearedIds->Add(FieldId);
		}
	}
	else if (GDK_PROPERTY(EnumProperty)* EnumProperty = GDK_CASTFIELD<GDK_PROPERTY(EnumProperty)>(Property))
	{
		if (EnumProperty->ElementSize < 4)
		{
			Schema_AddUint32(Object, FieldId, (uint32)EnumProperty->GetUnderlyingProperty()->GetUnsignedIntPropertyValue(Data));
		}
		else
		{
			AddProperty(Object, FieldId, EnumProperty->GetUnderlyingProperty(), Data, ClearedIds);
		}
	}
	else if (Property->IsA<GDK_PROPERTY(DelegateProperty)>() || Property->IsA<GDK_PROPERTY(MulticastDelegateProperty)>()
			 || Property->IsA<GDK_PROPERTY(InterfaceProperty)>())
	{
		// These properties can be set to replicate, but won't serialize across the network.
	}
	else if (Property->IsA<GDK_PROPERTY(MapProperty)>())
	{
		UE_LOG(LogComponentFactory, Error, TEXT("Class %s with name %s in field %d: Replicated TMaps are not supported."),
			   *Property->GetClass()->GetName(), *Property->GetName(), FieldId);
	}
	else if (Property->IsA<GDK_PROPERTY(SetProperty)>())
	{
		UE_LOG(LogComponentFactory, Error, TEXT("Class %s with name %s in field %d: Replicated TSets are not supported."),
			   *Property->GetClass()->GetName(), *Property->GetName(), FieldId);
	}
	else
	{
		UE_LOG(LogComponentFactory, Error, TEXT("Class %s with name %s in field %d: Attempted to add unknown property type."),
			   *Property->GetClass()->GetName(), *Property->GetName(), FieldId);
	}
}

TArray<FWorkerComponentData> ComponentFactory::CreateComponentDatas(UObject* Object, const FClassInfo& Info,
																	const FRepChangeState& RepChangeState, uint32& OutBytesWritten)
{
	TArray<FWorkerComponentData> ComponentDatas;

	static_assert(SCHEMA_Count == 4, "Unexpected number of Schema type components, please check the enclosing function is still correct.");

	if (Info.SchemaComponents[SCHEMA_Data] != SpatialConstants::INVALID_COMPONENT_ID)
	{
		ComponentDatas.Add(CreateComponentData(Info.SchemaComponents[SCHEMA_Data], Object, RepChangeState, SCHEMA_Data, OutBytesWritten));
	}

	if (Info.SchemaComponents[SCHEMA_OwnerOnly] != SpatialConstants::INVALID_COMPONENT_ID)
	{
		ComponentDatas.Add(
			CreateComponentData(Info.SchemaComponents[SCHEMA_OwnerOnly], Object, RepChangeState, SCHEMA_OwnerOnly, OutBytesWritten));
	}

	if (Info.SchemaComponents[SCHEMA_ServerOnly] != SpatialConstants::INVALID_COMPONENT_ID)
	{
		ComponentDatas.Add(
			CreateComponentData(Info.SchemaComponents[SCHEMA_ServerOnly], Object, RepChangeState, SCHEMA_ServerOnly, OutBytesWritten));
	}

	if (Info.SchemaComponents[SCHEMA_InitialOnly] != SpatialConstants::INVALID_COMPONENT_ID)
	{
		// Initial only data on dynamic subobjects is not currently supported.
		// When initial only replication is enabled, don't allow updates to be sent to initial only components.
		// When initial only replication is disabled, initial only data is replicated per normal COND_None rules, so allow the update
		// through.
		if (bInitialOnlyReplicationEnabled && Info.bDynamicSubobject)
		{
			UE_LOG(LogComponentFactory, Warning,
				   TEXT("Dynamic component using InitialOnly data. This data will not be sent. Obj (%s) Outer (%s)."), *Object->GetName(),
				   *GetNameSafe(Object->GetOuter()));
		}
		else
		{
			ComponentDatas.Add(CreateComponentData(Info.SchemaComponents[SCHEMA_InitialOnly], Object, RepChangeState, SCHEMA_InitialOnly,
												   OutBytesWritten));

			bInitialOnlyDataWritten = true;
		}
	}

	return ComponentDatas;
}

FWorkerComponentData ComponentFactory::CreateComponentData(Worker_ComponentId ComponentId, UObject* Object, const FRepChangeState& Changes,
														   ESchemaComponentType PropertyGroup, uint32& OutBytesWritten)
{
	FWorkerComponentData ComponentData = {};
	ComponentData.component_id = ComponentId;
	ComponentData.schema_type = Schema_CreateComponentData(ComponentId);
	Schema_Object* ComponentObject = Schema_GetComponentDataFields(ComponentData.schema_type);

	// We're currently ignoring ClearedId fields, which is problematic if the initial replicated state
	// is different to what the default state is (the client will have the incorrect data). UNR:959
	OutBytesWritten += FillSchemaObject(ComponentObject, Object, Changes, PropertyGroup, true);

	return ComponentData;
}

FWorkerComponentData ComponentFactory::CreateEmptyComponentData(Worker_ComponentId ComponentId)
{
	FWorkerComponentData ComponentData = {};
	ComponentData.component_id = ComponentId;
	ComponentData.schema_type = Schema_CreateComponentData(ComponentId);

	return ComponentData;
}

TArray<FWorkerComponentUpdate> ComponentFactory::CreateComponentUpdates(UObject* Object, const FClassInfo& Info, Worker_EntityId EntityId,
																		const FRepChangeState* RepChangeState, uint32& OutBytesWritten)
{
	TArray<FWorkerComponentUpdate> ComponentUpdates;

	static_assert(SCHEMA_Count == 4, "Unexpected number of Schema type components, please check the enclosing function is still correct.");

	if (RepChangeState)
	{
		if (Info.SchemaComponents[SCHEMA_Data] != SpatialConstants::INVALID_COMPONENT_ID)
		{
			uint32 BytesWritten = 0;
			FWorkerComponentUpdate MultiClientUpdate =
				CreateComponentUpdate(Info.SchemaComponents[SCHEMA_Data], Object, *RepChangeState, SCHEMA_Data, BytesWritten);
			if (BytesWritten > 0)
			{
				ComponentUpdates.Add(MultiClientUpdate);
				OutBytesWritten += BytesWritten;
			}
		}

		if (Info.SchemaComponents[SCHEMA_OwnerOnly] != SpatialConstants::INVALID_COMPONENT_ID)
		{
			uint32 BytesWritten = 0;
			FWorkerComponentUpdate SingleClientUpdate =
				CreateComponentUpdate(Info.SchemaComponents[SCHEMA_OwnerOnly], Object, *RepChangeState, SCHEMA_OwnerOnly, BytesWritten);
			if (BytesWritten > 0)
			{
				ComponentUpdates.Add(SingleClientUpdate);
				OutBytesWritten += BytesWritten;
			}
		}

		if (Info.SchemaComponents[SCHEMA_ServerOnly] != SpatialConstants::INVALID_COMPONENT_ID)
		{
			uint32 BytesWritten = 0;
			FWorkerComponentUpdate HandoverUpdate =
				CreateComponentUpdate(Info.SchemaComponents[SCHEMA_ServerOnly], Object, *RepChangeState, SCHEMA_ServerOnly, BytesWritten);
			if (BytesWritten > 0)
			{
				ComponentUpdates.Add(HandoverUpdate);
				OutBytesWritten += BytesWritten;
			}
		}

		if (Info.SchemaComponents[SCHEMA_InitialOnly] != SpatialConstants::INVALID_COMPONENT_ID)
		{
			// Initial only data on dynamic subobjects is not currently supported.
			// When initial only replication is enabled, don't allow updates to be sent to initial only components.
			// When initial only replication is disabled, initial only data is replicated per normal COND_None rules, so allow the update
			// through.
			if (!Info.bDynamicSubobject || !bInitialOnlyReplicationEnabled)
			{
				uint32 BytesWritten = 0;
				FWorkerComponentUpdate InitialOnlyUpdate = CreateComponentUpdate(Info.SchemaComponents[SCHEMA_InitialOnly], Object,
																				 *RepChangeState, SCHEMA_InitialOnly, BytesWritten);
				if (BytesWritten > 0)
				{
					ComponentUpdates.Add(InitialOnlyUpdate);
					OutBytesWritten += BytesWritten;
					bInitialOnlyDataWritten = true;
				}
			}
		}
	}

	// Only support Interest for Actors for now.
	if (Object->IsA<AActor>() && bInterestHasChanged)
	{
		ComponentUpdates.Add(NetDriver->InterestFactory->CreateInterestUpdate((AActor*)Object, Info, EntityId));

		// There should not be a need to update the channel's up to date flag here.
		checkSlow(([this, Object]() {
			USpatialActorChannel* Channel = NetDriver->GetOrCreateSpatialActorChannel(Cast<AActor>(Object));

			return Channel && Channel->NeedOwnerInterestUpdate() == !NetDriver->InterestFactory->DoOwnersHaveEntityId(Cast<AActor>(Object));
		}()));
	}

	return ComponentUpdates;
}

FWorkerComponentUpdate ComponentFactory::CreateComponentUpdate(Worker_ComponentId ComponentId, UObject* Object,
															   const FRepChangeState& Changes, ESchemaComponentType PropertyGroup,
															   uint32& OutBytesWritten)
{
	FWorkerComponentUpdate ComponentUpdate = {};

	ComponentUpdate.component_id = ComponentId;
	ComponentUpdate.schema_type = Schema_CreateComponentUpdate();
	Schema_Object* ComponentObject = Schema_GetComponentUpdateFields(ComponentUpdate.schema_type);

	TArray<Schema_FieldId> ClearedIds;

	uint32 BytesWritten = FillSchemaObject(ComponentObject, Object, Changes, PropertyGroup, false, &ClearedIds);

	for (Schema_FieldId Id : ClearedIds)
	{
		Schema_AddComponentUpdateClearedField(ComponentUpdate.schema_type, Id);
		BytesWritten++; // Workaround so we don't drop updates that *only* contain cleared fields - JIRA UNR-3371
	}

	if (BytesWritten == 0)
	{
		Schema_DestroyComponentUpdate(ComponentUpdate.schema_type);
	}

	OutBytesWritten += BytesWritten;

	return ComponentUpdate;
}

} // namespace SpatialGDK
