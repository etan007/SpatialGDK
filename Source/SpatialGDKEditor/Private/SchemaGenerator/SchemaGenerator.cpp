// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "SchemaGenerator.h"

#include "Algo/Reverse.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "UObject/TextProperty.h"

#include "Interop/SpatialClassInfoManager.h"
#include "SpatialGDKEditorSchemaGenerator.h"
#include "SpatialGDKSettings.h"
#include "Utils/CodeWriter.h"
#include "Utils/ComponentIdGenerator.h"
#include "Utils/DataTypeUtilities.h"
#include "Utils/GDKPropertyMacros.h"
#include "SpatialGDKServicesConstants.h"

using namespace SpatialGDKEditor::Schema;

DEFINE_LOG_CATEGORY(LogSchemaGenerator);

namespace
{
ESchemaComponentType PropertyGroupToSchemaComponentType(EReplicatedPropertyGroup Group)
{
	static_assert(REP_Count == 4,
				  "Unexpected number of ReplicatedPropertyGroups, please make sure PropertyGroupToSchemaComponentType is still correct.");
	static_assert(SCHEMA_Count == 4,
				  "Unexpected number of Schema component types, please make sure PropertyGroupToSchemaComponentType is still correct.");

	switch (Group)
	{
	case REP_MultiClient:
		return SCHEMA_Data;
	case REP_SingleClient:
		return SCHEMA_OwnerOnly;
	case REP_InitialOnly:
		return SCHEMA_InitialOnly;
	case REP_ServerOnly:
		return SCHEMA_ServerOnly;
	default:
		checkNoEntry();
		return SCHEMA_Invalid;
	}
}

// Given a RepLayout cmd type (a data type supported by the replication system). Generates the corresponding
// type used in schema.
FString PropertyToSchemaType(GDK_PROPERTY(Property) * Property,bool bPreFix = true)
{
	FString DataType;
    FString PreFix = TEXT("optional ");
	if (Property->IsA(GDK_PROPERTY(StructProperty)::StaticClass()))
	{
		GDK_PROPERTY(StructProperty)* StructProp = GDK_CASTFIELD<GDK_PROPERTY(StructProperty)>(Property);
		UScriptStruct* Struct = StructProp->Struct;
		DataType = TEXT("bytes");
	}
	else if (Property->IsA(GDK_PROPERTY(BoolProperty)::StaticClass()))
	{
		DataType = TEXT("bool");
	}
	else if (Property->IsA(GDK_PROPERTY(FloatProperty)::StaticClass()))
	{
		DataType = TEXT("float");
	}
	else if (Property->IsA(GDK_PROPERTY(DoubleProperty)::StaticClass()))
	{
		DataType = TEXT("double");
	}
	else if (Property->IsA(GDK_PROPERTY(Int8Property)::StaticClass()))
	{
		DataType = TEXT("int32");
	}
	else if (Property->IsA(GDK_PROPERTY(Int16Property)::StaticClass()))
	{
		DataType = TEXT("int32");
	}
	else if (Property->IsA(GDK_PROPERTY(IntProperty)::StaticClass()))
	{
		DataType = TEXT("int32");
	}
	else if (Property->IsA(GDK_PROPERTY(Int64Property)::StaticClass()))
	{
		DataType = TEXT("int64");
	}
	else if (Property->IsA(GDK_PROPERTY(ByteProperty)::StaticClass()))
	{
		DataType = TEXT("uint32"); // uint8 not supported in schema.
	}
	else if (Property->IsA(GDK_PROPERTY(UInt16Property)::StaticClass()))
	{
		DataType = TEXT("uint32");
	}
	else if (Property->IsA(GDK_PROPERTY(UInt32Property)::StaticClass()))
	{
		DataType = TEXT("uint32");
	}
	else if (Property->IsA(GDK_PROPERTY(UInt64Property)::StaticClass()))
	{
		DataType = TEXT("uint64");
	}
	else if (Property->IsA(GDK_PROPERTY(NameProperty)::StaticClass()) || Property->IsA(GDK_PROPERTY(StrProperty)::StaticClass())
			 || Property->IsA(GDK_PROPERTY(TextProperty)::StaticClass()))
	{
		DataType = TEXT("string");
	}
	else if (Property->IsA(GDK_PROPERTY(ObjectPropertyBase)::StaticClass()))
	{
		DataType = TEXT("UnrealObjectRef");
	}
	else if (Property->IsA(GDK_PROPERTY(ArrayProperty)::StaticClass()))
	{
		DataType = PropertyToSchemaType(GDK_CASTFIELD<GDK_PROPERTY(ArrayProperty)>(Property)->Inner,false);
		DataType = FString::Printf(TEXT("repeated %s"), *DataType);
		PreFix = TEXT("");
	}
	else if (Property->IsA(GDK_PROPERTY(EnumProperty)::StaticClass()))
	{
		DataType = GetEnumDataType(GDK_CASTFIELD<GDK_PROPERTY(EnumProperty)>(Property));
	}
	else
	{
		DataType = TEXT("bytes");
	}
	if(bPreFix)
	DataType = PreFix + DataType;
	return DataType;
}

void WriteSchemaRepField(FCodeWriter& Writer, const TSharedPtr<FUnrealProperty> RepProp, const int FieldCounter)
{
	Writer.Printf(" {0} {1} = {2};", *PropertyToSchemaType(RepProp->Property), *SchemaFieldName(RepProp), FieldCounter);
}

// Generates schema for a statically attached subobject on an Actor.
FActorSpecificSubobjectSchemaData GenerateSchemaForStaticallyAttachedSubobject(FCodeWriter& Writer, FComponentIdGenerator& IdGenerator,
																			   FString PropertyName, TSharedPtr<FUnrealType>& TypeInfo,
																			   UClass* ComponentClass, UClass* ActorClass, int MapIndex,
																			   const FActorSpecificSubobjectSchemaData* ExistingSchemaData)
{
	FUnrealFlatRepData RepData = GetFlatRepData(TypeInfo);

	FActorSpecificSubobjectSchemaData SubobjectData;
	SubobjectData.ClassPath = ComponentClass->GetPathName();

	for (EReplicatedPropertyGroup Group : GetAllReplicatedPropertyGroups())
	{
		// Since it is possible to replicate subobjects which have no replicated properties.
		// We need to generate a schema component for every subobject. So if we have no replicated
		// properties, we only generate a schema component if we are REP_MultiClient.
		if (RepData[Group].Num() == 0 && Group != REP_MultiClient)
		{
			continue;
		}

		Worker_ComponentId ComponentId = 0;
		if (ExistingSchemaData != nullptr && ExistingSchemaData->SchemaComponents[PropertyGroupToSchemaComponentType(Group)] != 0)
		{
			ComponentId = ExistingSchemaData->SchemaComponents[PropertyGroupToSchemaComponentType(Group)];
		}
		else
		{
			ComponentId = IdGenerator.Next();
		}

		Writer.PrintNewLine();

		FString ComponentName = PropertyName + GetReplicatedPropertyGroupName(Group);
		Writer.Printf("message {0} {", *ComponentName);
		Writer.Indent();
		//Writer.Printf("id = {0};", ComponentId);
		Writer.Printf("optional uint32 msg_cid = 1[default = {0}];", ComponentId);
		Writer.Printf("optional unreal.generated.{0} data = 2;", *SchemaReplicatedDataName(Group, ComponentClass));
		Writer.Outdent().Print("}");

		AddComponentId(ComponentId, SubobjectData.SchemaComponents, PropertyGroupToSchemaComponentType(Group));
	}

	return SubobjectData;
}

// Output the includes required by this schema file.
void GenerateSubobjectSchemaForActorIncludes(FCodeWriter& Writer, TSharedPtr<FUnrealType>& TypeInfo)
{
	TSet<UStruct*> AlreadyImported;

	for (const auto& OffsetToSubobject : GetAllSubobjects(TypeInfo))
	{
		const TSharedPtr<FUnrealType>& PropertyTypeInfo = OffsetToSubobject.Type;

		UObject* Value = PropertyTypeInfo->Object;

		if (IsSupportedClass(Value->GetClass()))
		{
			UClass* Class = Value->GetClass();
			if (!AlreadyImported.Contains(Class) && SchemaGeneratedClasses.Contains(Class))
			{
				Writer.Printf("import \"unreal/generated/Subobjects/{0}.proto\";", *ClassPathToSchemaName[Class->GetPathName()]);
				AlreadyImported.Add(Class);
			}
		}
	}
}

// Generates schema for all statically attached subobjects on an Actor.
void GenerateSubobjectSchemaForActor(FComponentIdGenerator& IdGenerator, UClass* ActorClass, TSharedPtr<FUnrealType> TypeInfo,
									 FString SchemaPath, FActorSchemaData& ActorSchemaData, const FActorSchemaData* ExistingSchemaData)
{
	FCodeWriter Writer;

	Writer.Printf(R"""(
		syntax = "proto2";
		// Note that this file has been generated automatically
		package unreal.generated.{0}.subobjects;)""",
				  *ClassPathToSchemaName[ActorClass->GetPathName()].ToLower());

	Writer.PrintNewLine();

	GenerateSubobjectSchemaForActorIncludes(Writer, TypeInfo);

	FSubobjects Subobjects = GetAllSubobjects(TypeInfo);

	bool bHasComponents = false;

	for (auto& It : Subobjects)
	{
		TSharedPtr<FUnrealType>& SubobjectTypeInfo = It.Type;
		UClass* SubobjectClass = Cast<UClass>(SubobjectTypeInfo->Type);

		FActorSpecificSubobjectSchemaData SubobjectData;

		if (SchemaGeneratedClasses.Contains(SubobjectClass))
		{
			bHasComponents = true;

			const FActorSpecificSubobjectSchemaData* ExistingSubobjectSchemaData = nullptr;
			if (ExistingSchemaData != nullptr)
			{
				for (auto& SubobjectIt : ExistingSchemaData->SubobjectData)
				{
					if (SubobjectIt.Value.Name == SubobjectTypeInfo->Name)
					{
						ExistingSubobjectSchemaData = &SubobjectIt.Value;
						break;
					}
				}
			}
			SubobjectData = GenerateSchemaForStaticallyAttachedSubobject(
				Writer, IdGenerator, UnrealNameToSchemaComponentName(SubobjectTypeInfo->Name.ToString()), SubobjectTypeInfo, SubobjectClass,
				ActorClass, 0, ExistingSubobjectSchemaData);
		}
		else
		{
			continue;
		}

		SubobjectData.Name = SubobjectTypeInfo->Name;
		uint32 SubobjectOffset = SubobjectData.SchemaComponents[SCHEMA_Data];
		check(SubobjectOffset != 0);
		ActorSchemaData.SubobjectData.Add(SubobjectOffset, SubobjectData);
	}

	if (bHasComponents)
	{
		FString FileName = FString::Printf(TEXT("%sComponents.proto"), *ClassPathToSchemaName[ActorClass->GetPathName()]);
		Writer.WriteToFile(*FPaths::Combine(*SchemaPath, FileName));
	}
}

FString GetRPCFieldPrefix(ERPCType RPCType)
{
	switch (RPCType)
	{
	case ERPCType::ClientReliable:
		return TEXT("server_to_client_reliable");
	case ERPCType::ClientUnreliable:
		return TEXT("server_to_client_unreliable");
	case ERPCType::ServerReliable:
		return TEXT("client_to_server_reliable");
	case ERPCType::ServerUnreliable:
		return TEXT("client_to_server_unreliable");
	case ERPCType::ServerAlwaysWrite:
		return TEXT("client_to_server_always_write");
	case ERPCType::NetMulticast:
		return TEXT("multicast");
	case ERPCType::CrossServer:
		return TEXT("cross_server");
	default:
		checkNoEntry();
	}

	return FString();
}

void GenerateRPCEndpoint(FCodeWriter& Writer, FString EndpointName, Worker_ComponentId ComponentId, TArray<ERPCType> SentRPCTypes,
						 TArray<ERPCType> AckedRPCTypes)
{
	FString ComponentName = TEXT("Unreal") + EndpointName;
	Writer.PrintNewLine();
	Writer.Printf("message {0} {", *ComponentName).Indent();
	//Writer.Printf("id = {0};", ComponentId);
	Writer.Printf("optional uint32 msg_cid = 1[default = {0}];", ComponentId);
	Schema_FieldId FieldId = 2;
	for (ERPCType SentRPCType : SentRPCTypes)
	{
		uint32 RingBufferSize = GetDefault<USpatialGDKSettings>()->GetRPCRingBufferSize(SentRPCType);

		for (uint32 RingBufferIndex = 0; RingBufferIndex < RingBufferSize; RingBufferIndex++)
		{
			Writer.Printf("optional UnrealRPCPayload {0}_rpc_x{1} = {2};", GetRPCFieldPrefix(SentRPCType), RingBufferIndex, FieldId++);
			if (SentRPCType == ERPCType::CrossServer)
			{
				Writer.Printf("optional CrossServerRPCInfo {0}_counterpart_x{1} = {2};", GetRPCFieldPrefix(SentRPCType), RingBufferIndex, FieldId++);
			}
		}
		Writer.Printf("optional uint64 last_sent_{0}_rpc_id = {1};", GetRPCFieldPrefix(SentRPCType), FieldId++);
	}

	for (ERPCType AckedRPCType : AckedRPCTypes)
	{
		uint32 RingBufferSize = GetDefault<USpatialGDKSettings>()->GetRPCRingBufferSize(AckedRPCType);
		if (AckedRPCType == ERPCType::CrossServer)
		{
			for (uint32 RingBufferIndex = 0; RingBufferIndex < RingBufferSize; RingBufferIndex++)
			{
				Writer.Printf("optional ACKItem {0}_ack_rpc_x{1} = {2};", GetRPCFieldPrefix(AckedRPCType), RingBufferIndex, FieldId++);
			}
		}
		else
		{
			Writer.Printf("optional uint64 last_acked_{0}_rpc_id = {1};", GetRPCFieldPrefix(AckedRPCType), FieldId++);
		}
	}

	if (ComponentId == SpatialConstants::MULTICAST_RPCS_COMPONENT_ID)
	{
		// This counter is used to let clients execute initial multicast RPCs when entity is just getting created,
		// while ignoring existing multicast RPCs when an entity enters the interest range.
		Writer.Printf("optional uint32 initially_present_multicast_rpc_count = {0};", FieldId++);
	}

	Writer.Outdent().Print("}");
}

} // anonymous namespace

void GenerateSubobjectSchema(FComponentIdGenerator& IdGenerator, UClass* Class, TSharedPtr<FUnrealType> TypeInfo, FString SchemaPath)
{
	FCodeWriter Writer;

	Writer.Printf(R"""(
		syntax = "proto2";
		// Note that this file has been generated automatically
		package unreal.generated;)""");
	Writer.PrintNewLine();
	bool bShouldIncludeCoreTypes = false;

	// Only include core types if the subobject has replicated references to other UObjects
	FUnrealFlatRepData RepData = GetFlatRepData(TypeInfo);
	for (auto& PropertyGroup : RepData)
	{
		for (auto& PropertyPair : PropertyGroup.Value)
		{
			GDK_PROPERTY(Property)* Property = PropertyPair.Value->Property;
			if (Property->IsA<GDK_PROPERTY(ObjectPropertyBase)>())
			{
				bShouldIncludeCoreTypes = true;
			}

			if (Property->IsA<GDK_PROPERTY(ArrayProperty)>())
			{
				if (GDK_CASTFIELD<GDK_PROPERTY(ArrayProperty)>(Property)->Inner->IsA<GDK_PROPERTY(ObjectPropertyBase)>())
				{
					bShouldIncludeCoreTypes = true;
				}
			}
		}
	}

	if (bShouldIncludeCoreTypes)
	{
		Writer.PrintNewLine();
		Writer.Printf("import \"unreal/gdk/core_types.proto\";");
	}

	for (EReplicatedPropertyGroup Group : GetAllReplicatedPropertyGroups())
	{
		// Since it is possible to replicate subobjects which have no replicated properties.
		// We need to generate a schema component for every subobject. So if we have no replicated
		// properties, we only generate a schema component if we are REP_MultiClient.
		if (RepData[Group].Num() == 0 && Group != REP_MultiClient)
		{
			continue;
		}

		// If this class is an Actor Component, it MUST have bReplicates at field ID 1.
		if (Group == REP_MultiClient && Class->IsChildOf<UActorComponent>())
		{
			TSharedPtr<FUnrealProperty> ExpectedReplicatesPropData =
				RepData[Group].FindRef(SpatialConstants::ACTOR_COMPONENT_REPLICATES_ID);
			const GDK_PROPERTY(Property)* ReplicatesProp = UActorComponent::StaticClass()->FindPropertyByName("bReplicates");

			if (!(ExpectedReplicatesPropData.IsValid() && ExpectedReplicatesPropData->Property == ReplicatesProp))
			{
				UE_LOG(LogSchemaGenerator, Error,
					   TEXT("Did not find ActorComponent->bReplicates at field %d for class %s. Modifying the base Actor Component class "
							"is currently not supported."),
					   SpatialConstants::ACTOR_COMPONENT_REPLICATES_ID, *Class->GetName());
			}
		}

		Writer.PrintNewLine();
		Writer.Printf("message {0} {", *SchemaReplicatedDataName(Group, Class));
		Writer.Indent();
		for (auto& RepProp : RepData[Group])
		{
			WriteSchemaRepField(Writer, RepProp.Value, RepProp.Value->ReplicationData->Handle);
		}

		Writer.Outdent().Print("}");
	}

	// Use the max number of dynamically attached subobjects per class to generate
	// that many schema components for this subobject.
	const uint32 DynamicComponentsPerClass = GetDefault<USpatialGDKSettings>()->MaxDynamicallyAttachedSubobjectsPerClass;

	FSubobjectSchemaData SubobjectSchemaData;

	// Use previously generated component IDs when possible.
	const FSubobjectSchemaData* const ExistingSchemaData = SubobjectClassPathToSchema.Find(Class->GetPathName());
	if (ExistingSchemaData != nullptr && !ExistingSchemaData->GeneratedSchemaName.IsEmpty()
		&& ExistingSchemaData->GeneratedSchemaName != ClassPathToSchemaName[Class->GetPathName()])
	{
		UE_LOG(LogSchemaGenerator, Error,
			   TEXT("Saved generated schema name does not match in-memory version for class %s - schema %s : %s"), *Class->GetPathName(),
			   *ExistingSchemaData->GeneratedSchemaName, *ClassPathToSchemaName[Class->GetPathName()]);
		UE_LOG(LogSchemaGenerator, Error,
			   TEXT("Schema generation may have resulted in component name clash, recommend you perform a full schema generation"));
	}

	for (uint32 i = 1; i <= DynamicComponentsPerClass; i++)
	{
		FDynamicSubobjectSchemaData DynamicSubobjectComponents;

		for (EReplicatedPropertyGroup Group : GetAllReplicatedPropertyGroups())
		{
			// Since it is possible to replicate subobjects which have no replicated properties.
			// We need to generate a schema component for every subobject. So if we have no replicated
			// properties, we only generate a schema component if we are REP_MultiClient.
			if (RepData[Group].Num() == 0 && Group != REP_MultiClient)
			{
				continue;
			}

			Writer.PrintNewLine();

			Worker_ComponentId ComponentId = 0;
			if (ExistingSchemaData != nullptr)
			{
				ComponentId = ExistingSchemaData->GetDynamicSubobjectComponentId(i - 1, PropertyGroupToSchemaComponentType(Group));
			}

			if (ComponentId == 0)
			{
				ComponentId = IdGenerator.Next();
			}
			FString ComponentName = SchemaReplicatedDataName(Group, Class) + TEXT("Dynamic") + FString::FromInt(i);

			Writer.Printf("message {0} {", *ComponentName);
			Writer.Indent();
			//Writer.Printf("id = {0};", ComponentId);
			Writer.Printf("optional uint32 msg_cid = 1[default = {0}];", ComponentId);
			Writer.Printf("optional {0} data = 2;", *SchemaReplicatedDataName(Group, Class));
			Writer.Outdent().Print("}");

			AddComponentId(ComponentId, DynamicSubobjectComponents.SchemaComponents, PropertyGroupToSchemaComponentType(Group));
		}

		SubobjectSchemaData.DynamicSubobjectComponents.Add(MoveTemp(DynamicSubobjectComponents));
	}

	FString FileName = FString::Printf(TEXT("%s.proto"), *ClassPathToSchemaName[Class->GetPathName()]);
	Writer.WriteToFile(FPaths::Combine(*SchemaPath, *FileName));
	SubobjectSchemaData.GeneratedSchemaName = ClassPathToSchemaName[Class->GetPathName()];
	SubobjectClassPathToSchema.Add(Class->GetPathName(), SubobjectSchemaData);
}

EReplicatedPropertyGroup SchemaComponentTypeToPropertyGroup(ESchemaComponentType SchemaType)
{
	static_assert(REP_Count == 4,
				  "Unexpected number of ReplicatedPropertyGroups, please make sure SchemaComponentTypeToPropertyGroup is still correct.");
	static_assert(SCHEMA_Count == 4,
				  "Unexpected number of Schema component types, please make sure SchemaComponentTypeToPropertyGroup is still correct.");

	switch (SchemaType)
	{
	case SCHEMA_Data:
		return REP_MultiClient;
	case SCHEMA_OwnerOnly:
		return REP_SingleClient;
	case SCHEMA_InitialOnly:
		return REP_InitialOnly;
	case SCHEMA_ServerOnly:
		return REP_ServerOnly;
	default:
		checkNoEntry();
		return REP_MultiClient;
	}
}

void GenerateActorSchema(FComponentIdGenerator& IdGenerator, UClass* Class, TSharedPtr<FUnrealType> TypeInfo, FString SchemaPath)
{

	const FActorSchemaData* const SchemaData = ActorClassPathToSchema.Find(Class->GetPathName());
    //if("/Game/Blueprints/ShieldPickup.ShieldPickup_C")
	if(Class->GetPathName().Find("WXPlayer")>0)
	{
		int a = 1;
	}
	FCodeWriter Writer;

	Writer.Printf(R"""(
		syntax = "proto2";
		// Note that this file has been generated automatically
		package unreal.generated.{0};)""",
				  *ClassPathToSchemaName[Class->GetPathName()].ToLower());

	Writer.PrintNewLine();
	// Will always be included since AActor has replicated pointers to other actors
	Writer.PrintNewLine();
	Writer.Printf("import \"unreal/gdk/core_types.proto\";");

	FActorSchemaData ActorSchemaData;
	ActorSchemaData.GeneratedSchemaName = ClassPathToSchemaName[Class->GetPathName()];

	FUnrealFlatRepData RepData = GetFlatRepData(TypeInfo);

	// Client-server replicated properties.
	for (EReplicatedPropertyGroup Group : GetAllReplicatedPropertyGroups())
	{
		if (RepData[Group].Num() == 0)
		{
			continue;
		}

		// If this class is an Actor, it MUST have bTearOff at field ID 3.
		if (Group == REP_MultiClient && Class->IsChildOf<AActor>())
		{
			TSharedPtr<FUnrealProperty> ExpectedReplicatesPropData = RepData[Group].FindRef(SpatialConstants::ACTOR_TEAROFF_ID-1);
			const GDK_PROPERTY(Property)* ReplicatesProp = AActor::StaticClass()->FindPropertyByName("bTearOff");

			if (!(ExpectedReplicatesPropData.IsValid() && ExpectedReplicatesPropData->Property == ReplicatesProp))
			{
				UE_LOG(LogSchemaGenerator, Error,
					   TEXT("Did not find Actor->bTearOff at field %d for class %s. Modifying the base Actor class is currently not "
							"supported."),
					   SpatialConstants::ACTOR_TEAROFF_ID-1, *Class->GetName());
			}
		}

		Worker_ComponentId ComponentId = 0;
		if (SchemaData != nullptr && SchemaData->SchemaComponents[PropertyGroupToSchemaComponentType(Group)] != 0)
		{
			ComponentId = SchemaData->SchemaComponents[PropertyGroupToSchemaComponentType(Group)];
		}
		else
		{
			ComponentId = IdGenerator.Next();
		}

		Writer.PrintNewLine();

		FString ComponentName = SchemaReplicatedDataName(Group, Class);
		Writer.Printf("message {0} {", *ComponentName);
		Writer.Indent();
		//Writer.Printf("id = {0};", ComponentId);
		Writer.Printf("optional uint32 msg_cid = 1[default = {0}];", ComponentId);
		AddComponentId(ComponentId, ActorSchemaData.SchemaComponents, PropertyGroupToSchemaComponentType(Group));

        /*if(Group == REP_MultiClient)
        {
	        TMap<uint16,const TSharedPtr<FUnrealProperty>> map_rep;
        	for(EReplicatedPropertyGroup Group2 : GetAllReplicatedPropertyGroups())
        	{
        		for (auto& RepProp : RepData[Group2])
        		{
        			map_rep.Emplace( RepProp.Value->ReplicationData->Handle + 1,RepProp.Value);
        			//WriteSchemaRepField(Writer, RepProp.Value, RepProp.Value->ReplicationData->Handle + 1);
        		}
        	}

        	map_rep.KeySort([](uint16 A, uint16 B) {
				return A < B;
			});
        	for(auto iter:map_rep)
        	{
        		WriteSchemaRepField(Writer, iter.Value, iter.Key);
        	}
        }
        else
        {
        	for (auto& RepProp : RepData[Group])
        	{

        		WriteSchemaRepField(Writer, RepProp.Value, RepProp.Value->ReplicationData->Handle + 1);
        	}
        }*/

		for (auto& RepProp : RepData[Group])
		{

			WriteSchemaRepField(Writer, RepProp.Value, RepProp.Value->ReplicationData->Handle + 1);
		}

		Writer.Outdent().Print("}");
	}

	GenerateSubobjectSchemaForActor(IdGenerator, Class, TypeInfo, SchemaPath, ActorSchemaData,
									ActorClassPathToSchema.Find(Class->GetPathName()));

	ActorClassPathToSchema.Add(Class->GetPathName(), ActorSchemaData);

	// Cache the NCD for this Actor
	if (AActor* CDO = Class->GetDefaultObject<AActor>())
	{
		const float NCD = CDO->NetCullDistanceSquared;
		if (NetCullDistanceToComponentId.Find(NCD) == nullptr)
		{
			if (FMath::FloorToFloat(NCD) != NCD)
			{
				UE_LOG(LogSchemaGenerator, Warning,
					   TEXT("Fractional Net Cull Distance values are not supported and may result in incorrect behaviour. "
							"Please modify class's (%s) Net Cull Distance Squared value (%f)"),
					   *Class->GetPathName(), NCD);
			}

			NetCullDistanceToComponentId.Add(NCD, 0);
		}
	}
	FString FileName = FString::Printf(TEXT("%s.proto"), *ClassPathToSchemaName[Class->GetPathName()]);
	Writer.WriteToFile(*FPaths::Combine(*SchemaPath, FileName));
}

void GenerateRPCEndpointsSchema(FString SchemaPath)
{
	FCodeWriter Writer;

	Writer.Print(R"""(
		syntax = "proto2";
		// Note that this file has been generated automatically
		package unreal.generated;)""");
	Writer.PrintNewLine();
	Writer.PrintNewLine();
	Writer.Print("import \"unreal/gdk/core_types.proto\";");
	Writer.Print("import \"unreal/gdk/rpc_payload.proto\";");

	GenerateRPCEndpoint(Writer, TEXT("ClientEndpoint"), SpatialConstants::CLIENT_ENDPOINT_COMPONENT_ID,
						{ ERPCType::ServerReliable, ERPCType::ServerUnreliable, ERPCType::ServerAlwaysWrite },
						{ ERPCType::ClientReliable, ERPCType::ClientUnreliable });
	GenerateRPCEndpoint(Writer, TEXT("ServerEndpoint"), SpatialConstants::SERVER_ENDPOINT_COMPONENT_ID,
						{ ERPCType::ClientReliable, ERPCType::ClientUnreliable },
						{ ERPCType::ServerReliable, ERPCType::ServerUnreliable, ERPCType::ServerAlwaysWrite });
	GenerateRPCEndpoint(Writer, TEXT("MulticastRPCs"), SpatialConstants::MULTICAST_RPCS_COMPONENT_ID, { ERPCType::NetMulticast }, {});
	GenerateRPCEndpoint(Writer, TEXT("CrossServerSenderRPCs"), SpatialConstants::CROSS_SERVER_SENDER_ENDPOINT_COMPONENT_ID,
						{ ERPCType::CrossServer }, {});
	GenerateRPCEndpoint(Writer, TEXT("CrossServerReceiverRPCs"), SpatialConstants::CROSS_SERVER_RECEIVER_ENDPOINT_COMPONENT_ID,
						{ ERPCType::CrossServer }, {});
	GenerateRPCEndpoint(Writer, TEXT("CrossServerSenderACKRPCs"), SpatialConstants::CROSS_SERVER_SENDER_ACK_ENDPOINT_COMPONENT_ID, {},
						{ ERPCType::CrossServer });
	GenerateRPCEndpoint(Writer, TEXT("CrossServerReceiverACKRPCs"), SpatialConstants::CROSS_SERVER_RECEIVER_ACK_ENDPOINT_COMPONENT_ID, {},
						{ ERPCType::CrossServer });

	Writer.WriteToFile(*FPaths::Combine(*SchemaPath, TEXT("rpc_endpoints.proto")));
}

// Add the component ID to the passed schema components array.
void AddComponentId(const Worker_ComponentId ComponentId, ComponentIdPerType& SchemaComponents, const ESchemaComponentType ComponentType)
{
	SchemaComponents[ComponentType] = ComponentId;
}


