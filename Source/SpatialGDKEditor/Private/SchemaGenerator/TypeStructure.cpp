// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "TypeStructure.h"

#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "SpatialGDKEditorSchemaGenerator.h"
#include "Utils/GDKPropertyMacros.h"
#include "Utils/RepLayoutUtils.h"

using namespace SpatialGDKEditor::Schema;

TArray<EReplicatedPropertyGroup> GetAllReplicatedPropertyGroups()
{
	static TArray<EReplicatedPropertyGroup> ReplicatedPropertyGroups = []() {
		TArray<EReplicatedPropertyGroup> Temp;
		for (uint32 i = EReplicatedPropertyGroup::REP_First; i < EReplicatedPropertyGroup::REP_Count; i++)
		{
			Temp.Add(static_cast<EReplicatedPropertyGroup>(i));
		}
		return Temp;
	}();

	return ReplicatedPropertyGroups;
}

FString GetReplicatedPropertyGroupName(EReplicatedPropertyGroup Group)
{
	static_assert(REP_Count == 4, "Unexpected number of ReplicatedPropertyGroups, please update this function.");

	switch (Group)
	{
	case REP_SingleClient:
		return TEXT("OwnerOnly");
	case REP_InitialOnly:
		return TEXT("InitialOnly");
	case REP_ServerOnly:
		return TEXT("ServerOnly");
	default:
		return TEXT("");
	}
}

void VisitAllObjects(TSharedPtr<FUnrealType> TypeNode, TFunction<bool(TSharedPtr<FUnrealType>)> Visitor)
{
	bool bShouldRecurseFurther = Visitor(TypeNode);
	for (auto& PropertyPair : TypeNode->Properties)
	{
		if (bShouldRecurseFurther && PropertyPair.Value->Type.IsValid())
		{
			// Recurse into subobjects.
			VisitAllObjects(PropertyPair.Value->Type, Visitor);
		}
	}
}

void VisitAllProperties(TSharedPtr<FUnrealType> TypeNode, TFunction<bool(TSharedPtr<FUnrealProperty>)> Visitor)
{
	for (auto& PropertyPair : TypeNode->Properties)
	{
		bool bShouldRecurseFurther = Visitor(PropertyPair.Value);
		if (bShouldRecurseFurther && PropertyPair.Value->Type.IsValid())
		{
			// Recurse into properties if they're structs.
			if (PropertyPair.Value->Property->IsA<GDK_PROPERTY(StructProperty)>())
			{
				VisitAllProperties(PropertyPair.Value->Type, Visitor);
			}
		}
	}
}

// GenerateChecksum is a method which replicates how Unreal generates it's own CompatibleChecksum for RepLayout Cmds.
// The original code can be found in the Unreal Engine's RepLayout. We use this to ensure we have the correct property at run-time.
uint32 GenerateChecksum(GDK_PROPERTY(Property) * Property, uint32 ParentChecksum, int32 StaticArrayIndex)
{
	/*uint32 Checksum = 0;
	Checksum = FCrc::StrCrc32(*Property->GetName().ToLower(), ParentChecksum);		  // Evolve checksum on name
	Checksum = FCrc::StrCrc32(*Property->GetCPPType(nullptr, 0).ToLower(), Checksum); // Evolve by property type
	Checksum = FCrc::MemCrc32(&StaticArrayIndex, sizeof(StaticArrayIndex),
							  Checksum); // Evolve by StaticArrayIndex (to make all unrolled static array elements unique)
	return Checksum;*/
	// Evolve checksum on name
	uint32 CompatibleChecksum = FCrc::StrCrc32(*Property->GetName().ToLower(), ParentChecksum);

	// Evolve by property type
	const FObjectPtrProperty* const ObjectPtrProperty = CastField<const FObjectPtrProperty>(Property);

	FString CPPType;
	if (ObjectPtrProperty)
	{
		// To remain compatible with TObjectPtr, use the underlying pointer type in the checksum since the net-serialized data is compatible.
		CPPType = ObjectPtrProperty->FObjectProperty::GetCPPType(nullptr, 0).ToLower();
	}
	else
	{
		CPPType = Property->GetCPPType(nullptr, 0).ToLower();
	}
	CompatibleChecksum = FCrc::StrCrc32(*CPPType, CompatibleChecksum);
	// Evolve by StaticArrayIndex (to make all unrolled static array elements unique)
	CompatibleChecksum = FCrc::MemCrc32(&StaticArrayIndex, sizeof(StaticArrayIndex), CompatibleChecksum);
	// Evolve by enum max value bits required

	if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		const uint64 MaxBits = EnumProp->GetMaxNetSerializeBits();

		CompatibleChecksum = FCrc::MemCrc32(&MaxBits, sizeof(MaxBits), CompatibleChecksum);
	}
	else if (const FByteProperty* ByteProp = CastField<FByteProperty>(Property))
	{
		const uint64 MaxBits = ByteProp->GetMaxNetSerializeBits();

		CompatibleChecksum = FCrc::MemCrc32(&MaxBits, sizeof(MaxBits), CompatibleChecksum);
	}

	return CompatibleChecksum;

}

TSharedPtr<FUnrealProperty> CreateUnrealProperty(TSharedPtr<FUnrealType> TypeNode, GDK_PROPERTY(Property) * Property, uint32 ParentChecksum,
												 uint32 StaticArrayIndex)
{
	TSharedPtr<FUnrealProperty> PropertyNode = MakeShared<FUnrealProperty>();
	PropertyNode->Property = Property;
	PropertyNode->ContainerType = TypeNode;
	PropertyNode->ParentChecksum = ParentChecksum;
	PropertyNode->StaticArrayIndex = StaticArrayIndex;

	// Generate a checksum for this PropertyNode to be used to match properties with the RepLayout Cmds later.
	PropertyNode->CompatibleChecksum = GenerateChecksum(Property, ParentChecksum, StaticArrayIndex);
	TypeNode->Properties.Add(Property, PropertyNode);
	return PropertyNode;
}

TSharedPtr<FUnrealType> CreateUnrealTypeInfo(UStruct* Type, uint32 ParentChecksum, int32 StaticArrayIndex)
{
	// Struct types will set this to nullptr.
	UClass* Class = Cast<UClass>(Type);


	// Create type node.
	TSharedPtr<FUnrealType> TypeNode = MakeShared<FUnrealType>();
	TypeNode->Type = Type;

	// Iterate through each property in the struct.
	// 迭代类属性
	for (TFieldIterator<GDK_PROPERTY(Property)> It(Type); It; ++It)
	{
		GDK_PROPERTY(Property)* Property = *It;

		// Create property node and add it to the AST.
		// 1.创建并添加属性节点到AST中
		TSharedPtr<FUnrealProperty> PropertyNode = CreateUnrealProperty(TypeNode, Property, ParentChecksum, StaticArrayIndex);

		// If this property not a struct or object (which can contain more properties), stop here.
		// 2.非多属性节点处理
		if (!Property->IsA<GDK_PROPERTY(StructProperty)>() && !Property->IsA<GDK_PROPERTY(ObjectProperty)>())
		{
			for (int i = 1; i < Property->ArrayDim; i++)
			{
				CreateUnrealProperty(TypeNode, Property, ParentChecksum, i);
			}
			continue;
		}

		// If this is a struct property, then get the struct type and recurse into it.
		// 3.结构体属性处理
		if (Property->IsA<GDK_PROPERTY(StructProperty)>())
		{
			GDK_PROPERTY(StructProperty)* StructProperty = GDK_CASTFIELD<GDK_PROPERTY(StructProperty)>(Property);

			// This is the property for the 0th struct array member.
			uint32 ParentPropertyNodeChecksum = PropertyNode->CompatibleChecksum;
			PropertyNode->Type = CreateUnrealTypeInfo(StructProperty->Struct, ParentPropertyNodeChecksum, 0);
			PropertyNode->Type->ParentProperty = PropertyNode;

			// For static arrays we need to make a new struct array member node.
			for (int i = 1; i < Property->ArrayDim; i++)
			{
				// Create a new PropertyNode.
				TSharedPtr<FUnrealProperty> StaticStructArrayPropertyNode = CreateUnrealProperty(TypeNode, Property, ParentChecksum, i);

				// Generate Type information on the inner struct.
				// Note: The parent checksum of the properties within a struct that is a member of a static struct array, is the checksum
				// for the struct itself after index modification.
				StaticStructArrayPropertyNode->Type =
					CreateUnrealTypeInfo(StructProperty->Struct, StaticStructArrayPropertyNode->CompatibleChecksum, 0);
				StaticStructArrayPropertyNode->Type->ParentProperty = StaticStructArrayPropertyNode;
			}
			continue;
		}

		// If this is an object property, then we need to do two things:
		//
		// 1) Determine whether this property is a strong or weak reference to the object. Some subobjects (such as the
		// CharacterMovementComponent) are in fact owned by the character, and can be stored in the same entity as the character itself.
		// Some subobjects (such as the Controller field in AActor) is a weak reference, and should just store a reference to the real
		// object. We inspect the CDO to determine whether the owner of the property value is equal to itself. As structs don't have CDOs,
		// we assume that all object properties in structs are weak references.
		//
		// 2) Obtain the concrete object type stored in this property. For example, the property containing the CharacterMovementComponent
		// might be a property which stores a MovementComponent pointer, so we'd need to somehow figure out the real type being stored
		// there during runtime. This is determined by getting the CDO of this class to determine what is stored in that property.

		// 4.对象属性处理
		// 1) 判断该属性是对该对象的强引用还是弱引用。一些子对象（例如CharacterMovementComponent) 实际上由角色拥有，并且可以与角色本身存储在同一实体中。
		// 一些子对象（例如 AActor 中的 Controller 字段）是弱引用，应该只存储对真实对象的引用目的。我们检查 CDO 以确定属性值的所有者是否等于它自己。由于结构没有 CDO，
		// 我们假设结构中的所有对象属性都是弱引用。
		//
		// 2) 获取存储在该属性中的具体对象类型。例如，包含 CharacterMovementComponent 的属性可能是一个存储 MovementComponent 指针的属性，所以我们需要以某种方式找出存储的真实类型
		// 在运行时在那里。这是通过获取此类的 CDO 来确定该属性中存储的内容。
		GDK_PROPERTY(ObjectProperty)* ObjectProperty = GDK_CASTFIELD<GDK_PROPERTY(ObjectProperty)>(Property);
		check(ObjectProperty);

		// If this is a property of a struct, assume it's a weak reference.
		// 如果这是一个结构的属性，假设它是一个弱引用。
		if (!Class)
		{
			continue;
		}

		UObject* ContainerCDO = Class->GetDefaultObject();
		check(ContainerCDO);

		// This is to ensure we handle static array properties only once.
		// 这是为了确保我们只处理一次静态数组属性。
		bool bHandleStaticArrayProperties = true;

		// Obtain the properties actual value from the CDO, so we can figure out its true type.
		// 从CDO中获取properties的实际值，从而判断它的真实类型。
		UObject* Value = ObjectProperty->GetPropertyValue_InContainer(ContainerCDO);
		if (Value)
		{
			// If this is an editor-only property, skip it. As we've already added to the property list at this stage, just remove it.
			// 如果这是一个仅限编辑器的属性，请跳过它。 由于我们已在此阶段添加到属性列表中，因此只需将其删除即可。
			if (Value->IsEditorOnly())
			{
				UE_LOG(LogSpatialGDKSchemaGenerator, Verbose, TEXT("%s - editor only, skipping"), *Property->GetName());
				TypeNode->Properties.Remove(Property);
				continue;
			}

			// Check whether the outer is the CDO of the class we're generating for
			// or the CDO of any of its parent classes.
			// (this also covers generating schema for a Blueprint derived from the outer's class)
			// 检查外部是否是我们正在为其生成的类的 CDO或其任何父类的 CDO。
			//（这也包括为从外部类派生的蓝图生成schema）
			UObject* Outer = Value->GetOuter();
			if ((Outer != nullptr) && Outer->HasAnyFlags(RF_ClassDefaultObject) && ContainerCDO->IsA(Outer->GetClass()))
			{
				UE_LOG(LogSpatialGDKSchemaGenerator, Verbose, TEXT("Property Class: %s Instance Class: %s"),
					   *ObjectProperty->PropertyClass->GetName(), *Value->GetClass()->GetName());

				// This property is definitely a strong reference, recurse into it.
				// 这个属性肯定是强引用，递归进去。
				PropertyNode->Type = CreateUnrealTypeInfo(Value->GetClass(), ParentChecksum, 0);
				PropertyNode->Type->ParentProperty = PropertyNode;
				PropertyNode->Type->Object = Value;
				PropertyNode->Type->Name = Value->GetFName();

				// For static arrays we need to make a new object array member node.
				// 对于静态数组，我们需要创建一个新的对象数组成员节点。
				for (int i = 1; i < Property->ArrayDim; i++)
				{
					TSharedPtr<FUnrealProperty> StaticObjectArrayPropertyNode = CreateUnrealProperty(TypeNode, Property, ParentChecksum, i);

					// Note: The parent checksum of static arrays of strong object references will be the parent checksum of this class.
					StaticObjectArrayPropertyNode->Type = CreateUnrealTypeInfo(Value->GetClass(), ParentChecksum, 0);
					StaticObjectArrayPropertyNode->Type->ParentProperty = StaticObjectArrayPropertyNode;
				}
				bHandleStaticArrayProperties = false;
			}
			else
			{
				// The values outer is not us, store as weak reference.
				// 外部的值不是我们，存储为弱引用。
				UE_LOG(LogSpatialGDKSchemaGenerator, Verbose, TEXT("%s - %s weak reference (outer not this)"), *Property->GetName(),
					   *ObjectProperty->PropertyClass->GetName());
			}
		}
		else
		{
			// If value is just nullptr, then we clearly don't own it.
			// 如果 value 只是 nullptr，那么我们显然不拥有它。
			UE_LOG(LogSpatialGDKSchemaGenerator, Verbose, TEXT("%s - %s weak reference (null init)"), *Property->GetName(),
				   *ObjectProperty->PropertyClass->GetName());
		}

		// Weak reference static arrays are handled as a single UObjectRef per static array member.
		// 弱引用静态数组作为每个静态数组成员的单个 UObjectRef 处理。
		if (bHandleStaticArrayProperties)
		{
			for (int i = 1; i < Property->ArrayDim; i++)
			{
				CreateUnrealProperty(TypeNode, Property, ParentChecksum, i);
			}
		}
	} // END TFieldIterator<GDK_PROPERTY(Property)>

	// Blueprint components don't exist on the CDO so we need to iterate over the
	// BlueprintGeneratedClass (and all of its blueprint parents) to find all blueprint components
	// 蓝图组件在 CDO 上不存在，因此我们需要迭代
	// BlueprintGeneratedClass（及其所有蓝图父级）以查找所有蓝图组件
	UClass* BlueprintClass = Class;
	while (UBlueprintGeneratedClass* BGC = Cast<UBlueprintGeneratedClass>(BlueprintClass))
	{
		if (USimpleConstructionScript* SCS = BGC->SimpleConstructionScript)
		{
			for (USCS_Node* Node : SCS->GetAllNodes())
			{
				if (Node->ComponentTemplate == nullptr)
				{
					continue;
				}

				for (auto& PropertyPair : TypeNode->Properties)
				{
					GDK_PROPERTY(ObjectProperty)* ObjectProperty = GDK_CASTFIELD<GDK_PROPERTY(ObjectProperty)>(PropertyPair.Key);
					if (ObjectProperty == nullptr)
						continue;
					TSharedPtr<FUnrealProperty> PropertyNode = PropertyPair.Value;

					if (ObjectProperty->GetName().Equals(Node->GetVariableName().ToString()))
					{
						PropertyNode->Type = CreateUnrealTypeInfo(ObjectProperty->PropertyClass, ParentChecksum, 0);
						PropertyNode->Type->ParentProperty = PropertyNode;
						PropertyNode->Type->Object = Node->ComponentTemplate;
						PropertyNode->Type->Name = ObjectProperty->GetFName();
					}
				}
			}
		}

		BlueprintClass = BlueprintClass->GetSuperClass();
	}

	// If this is not a class, exit now, as structs cannot have replicated properties.
	// 如果这不是一个类，现在退出，因为结构不能有复制的属性。
	if (!Class)
	{
		return TypeNode;
	}

	if (Class->IsChildOf<AActor>())
	{
		// Handle components attached to the actor; some of them may not have properties pointing to them.
		// 处理附加到actor的组件； 其中一些可能没有指向它们的属性。
		const AActor* CDO = Cast<AActor>(Class->GetDefaultObject());

		for (UActorComponent* Component : CDO->GetComponents())
		{
			if (Component->IsEditorOnly())
			{
				continue;
			}

			if (!Component->IsSupportedForNetworking())
			{
				continue;
			}

			//  is definitely a strong reference, recurse into it.
			// 绝对是强引用，递归进去。
			TSharedPtr<FUnrealType> SubobjectType = CreateUnrealTypeInfo(Component->GetClass(), ParentChecksum, 0);
			SubobjectType->Object = Component;
			SubobjectType->Name = Component->GetFName();

			FUnrealSubobject Subobject;
			Subobject.Type = SubobjectType;

			TypeNode->NoPropertySubobjects.Add(Subobject);
		}
	}

	// Set up replicated properties by reading the rep layout and matching the properties with the ones in the type node.
	// Based on inspection in InitFromObjectClass, the RepLayout will always replicate object properties using NetGUIDs, regardless of
	// ownership. However, the rep layout will recurse into structs and allocate rep handles for their properties, unless the condition
	// "Struct->StructFlags & STRUCT_NetSerializeNative" is true. In this case, the entire struct is replicated as a whole.
	// 通过读取 rep 布局并将属性与类型节点中的属性匹配来设置复制属性。
	// 根据 InitFromObjectClass 中的检查，RepLayout 将始终使用 NetGUID 复制对象属性，而不管所有权。 但是，rep 布局将递归到结构中并为其属性分配代表句柄，除非条件
	// "Struct->StructFlags & STRUCT_NetSerializeNative" 为真。 在这种情况下，整个结构被复制为一个整体。
	TSharedPtr<FRepLayout> RepLayoutPtr = FRepLayout::CreateFromClass(Class, nullptr /*ServerConnection*/, ECreateRepLayoutFlags::None);
	FRepLayout& RepLayout = *RepLayoutPtr.Get();
	for (int CmdIndex = 0; CmdIndex < RepLayout.Cmds.Num(); ++CmdIndex)
	{
		FRepLayoutCmd& Cmd = RepLayout.Cmds[CmdIndex];
		if (Cmd.Type == ERepLayoutCmdType::Return || Cmd.Property == nullptr)
		{
			continue;
		}

		// Jump over invalid replicated property types
		if (Cmd.Property->IsA<GDK_PROPERTY(DelegateProperty)>() || Cmd.Property->IsA<GDK_PROPERTY(MulticastDelegateProperty)>()
			|| Cmd.Property->IsA<GDK_PROPERTY(InterfaceProperty)>())
		{
			continue;
		}

		FRepParentCmd& Parent = RepLayout.Parents[Cmd.ParentIndex];

		// In a FRepLayout, all the root level replicated properties in a class are stored in the Parents array.
		// The Cmds array is an expanded version of the Parents array. This usually maps 1:1 with the Parents array (as most properties
		// don't contain other properties). The main exception are structs which don't have a native serialize function. In this case
		// multiple Cmds map to the structs properties, but they all have the same ParentIndex (which points to the root replicated property
		// which contains them.
		// 在 FRepLayout 中，类中的所有根级复制属性都存储在Parents 数组中。
        // Cmds 数组是Parents 数组的扩展版本。这通常与 Parents 数组 1:1 映射（与大多数属性一样不包含其他属性）。主要的例外是没有本机序列化功能的结构。在这种情况下
        // 多个 Cmds 映射到结构属性，但它们都具有相同的 ParentIndex（指向其中包含它们的根复制属性。
		//
		// This might be problematic if we have a property which is inside a struct, nested in another struct which is replicated.
		//如果我们有一个位于结构内部的属性，嵌套在另一个被复制的结构中，这可能会出现问题。
		// For example:
		//
		//	class Foo
		//	{
		//		struct Bar
		//		{
		// 			struct Baz
		// 			{
		// 				int Nested;
		// 			} Baz;
		// 		} Bar;
		//	}
		//
		// The parents array will contain "Bar", and the cmds array will contain "Nested", but we have no reference to "Baz" anywhere in the
		// RepLayout. What we do here is recurse into all of Bar's properties in the AST until we find Baz.
		// parents 数组将包含“Bar”，cmds 数组将包含“Nested”，但我们在任何地方都没有引用“Baz”的复制布局。 我们在这里所做的是递归到 AST 中 Bar 的所有属性，直到找到 Baz。

		TSharedPtr<FUnrealProperty> PropertyNode = nullptr;
		// Simple case: Cmd is a root property in the object.
		// 简单情况：Cmd 是对象中的根属性。
		if (Parent.Property == Cmd.Property)
		{

			// Make sure we have the correct property via the checksums.
			// 通过校验和确保我们拥有正确的属性。
			for (auto& PropertyPair : TypeNode->Properties)
			{
				if (PropertyPair.Value->CompatibleChecksum == Cmd.CompatibleChecksum)
				{
					PropertyNode = PropertyPair.Value;
				}
			}
			/*// 校验和没有找到就直接找名字
			if(PropertyNode == nullptr)
			{
				for (auto& PropertyPair : TypeNode->Properties)
				{
					if (PropertyPair.Key == Cmd.Property)
					{
						PropertyNode = PropertyPair.Value;
					}
				}
			}*/
		}
		else
		{
			// It's possible to have duplicate parent properties (they are distinguished by ArrayIndex), so we make sure to look at them all.
			// 可能有重复的父属性（它们由 ArrayIndex 区分），所以我们确保查看它们。
			TArray<TSharedPtr<FUnrealProperty>> RootProperties;
			TypeNode->Properties.MultiFind(Parent.Property, RootProperties);

			for (TSharedPtr<FUnrealProperty>& RootProperty : RootProperties)
			{
				checkf(RootProperty->Type.IsValid(),
					   TEXT("Properties in the AST which are parent properties in the rep layout must have child properties"));
				VisitAllProperties(RootProperty->Type, [&PropertyNode, &Cmd](TSharedPtr<FUnrealProperty> Property) {
					if (Property->CompatibleChecksum == Cmd.CompatibleChecksum)
					{
						checkf(!PropertyNode.IsValid(), TEXT("We've already found a previous property node with the same property. This "
															 "indicates that we have a 'diamond of death' style situation.")) PropertyNode =
							Property;
					}
					return true;
				});
			}
		}
		if (!PropertyNode.IsValid()) {

			//checkf(PropertyNode.IsValid(), TEXT("xxx Couldn't find the Cmd property inside the Parent's sub-properties. This shouldn't happen."));
			continue;  // IMPROBABLE-ADD
		}
		checkf(PropertyNode.IsValid(), TEXT("Couldn't find the Cmd property inside the Parent's sub-properties. This shouldn't happen."));

		// We now have the right property node. Fill in the rep data.
		// 我们现在有了正确的属性节点。 填写rep数据。
		TSharedPtr<FUnrealRepData> RepDataNode = MakeShared<FUnrealRepData>();
		RepDataNode->RepLayoutType = (ERepLayoutCmdType)Cmd.Type;
		RepDataNode->Condition = Parent.Condition;
		RepDataNode->RepNotifyCondition = Parent.RepNotifyCondition;
		RepDataNode->ArrayIndex = PropertyNode->StaticArrayIndex;

		if (Class->IsChildOf(AActor::StaticClass()))
		{
			// Uses the same pattern as ComponentReader::ApplySchemaObject and ReceivePropertyHelper
			if (UNLIKELY((int32)AActor::ENetFields_Private::RemoteRole == Cmd.ParentIndex))
			{
				const int32 SwappedCmdIndex = RepLayout.Parents[(int32)AActor::ENetFields_Private::Role].CmdStart;
				RepDataNode->RoleSwapHandle = static_cast<int32>(RepLayout.Cmds[SwappedCmdIndex].RelativeHandle);
			}
			else if (UNLIKELY((int32)AActor::ENetFields_Private::Role == Cmd.ParentIndex))
			{
				const int32 SwappedCmdIndex = RepLayout.Parents[(int32)AActor::ENetFields_Private::RemoteRole].CmdStart;
				RepDataNode->RoleSwapHandle = static_cast<int32>(RepLayout.Cmds[SwappedCmdIndex].RelativeHandle);
			}
		}
		else
		{
			RepDataNode->RoleSwapHandle = -1;
		}
		PropertyNode->ReplicationData = RepDataNode;
		PropertyNode->ReplicationData->Handle = Cmd.RelativeHandle;

		if (Cmd.Type == ERepLayoutCmdType::DynamicArray)
		{
			// Bypass the inner properties and null terminator cmd when processing dynamic arrays.
			CmdIndex = Cmd.EndCmd - 1;
		}
	} // END CMD FOR LOOP

	return TypeNode;
}

FUnrealFlatRepData GetFlatRepData(TSharedPtr<FUnrealType> TypeInfo)
{
	FUnrealFlatRepData RepData;
	for (EReplicatedPropertyGroup Group : GetAllReplicatedPropertyGroups())
	{
		RepData.Add(Group);
	}

	VisitAllProperties(TypeInfo, [&RepData, &TypeInfo](TSharedPtr<FUnrealProperty> PropertyInfo) {
		if (PropertyInfo->ReplicationData.IsValid())
		{
			EReplicatedPropertyGroup Group = REP_MultiClient;
			static_assert(REP_Count == 4,
						  "Unexpected number of ReplicatedPropertyGroups. Please make sure the GetFlatRepData function is still correct.");
			switch (PropertyInfo->ReplicationData->Condition)
			{
			case COND_AutonomousOnly:
			case COND_ReplayOrOwner:
			case COND_OwnerOnly:
				Group = REP_SingleClient;
				break;
			case COND_InitialOnly:
				Group = REP_InitialOnly;
				break;
			case COND_ServerOnly:
				Group = REP_ServerOnly;
				break;
			case COND_InitialOrOwner:
				UE_LOG(LogSpatialGDKSchemaGenerator, Error,
					   TEXT("COND_InitialOrOwner not supported. COND_None will be used instead. %s::%s"), *TypeInfo->Type->GetName(),
					   *PropertyInfo->Property->GetName());
				break;
			}
			RepData[Group].Add(PropertyInfo->ReplicationData->Handle, PropertyInfo);
		}
		return true;
	});

	// Sort by replication handle.
	for (EReplicatedPropertyGroup Group : GetAllReplicatedPropertyGroups())
	{
		RepData[Group].KeySort([](uint16 A, uint16 B) {
			return A < B;
		});
	}
	return RepData;
}

TArray<TSharedPtr<FUnrealProperty>> GetPropertyChain(TSharedPtr<FUnrealProperty> LeafProperty)
{
	TArray<TSharedPtr<FUnrealProperty>> OutputChain;
	TSharedPtr<FUnrealProperty> CurrentProperty = LeafProperty;
	while (CurrentProperty.IsValid())
	{
		OutputChain.Add(CurrentProperty);
		if (CurrentProperty->ContainerType.IsValid())
		{
			TSharedPtr<FUnrealType> EnclosingType = CurrentProperty->ContainerType.Pin();
			CurrentProperty = EnclosingType->ParentProperty.Pin();
		}
		else
		{
			CurrentProperty.Reset();
		}
	}

	// As we started at the leaf property and worked our way up, we need to reverse the list at the end.
	Algo::Reverse(OutputChain);
	return OutputChain;
}

FSubobjects GetAllSubobjects(TSharedPtr<FUnrealType> TypeInfo)
{
	FSubobjects Subobjects;

	TSet<UObject*> SeenComponents;
	auto AddSubobject = [&SeenComponents, &Subobjects](TSharedPtr<FUnrealType> PropertyTypeInfo) {
		UObject* Value = PropertyTypeInfo->Object;

		if (Value != nullptr && IsSupportedClass(Value->GetClass()))
		{
			if (!SeenComponents.Contains(Value))
			{
				SeenComponents.Add(Value);
				Subobjects.Add({ PropertyTypeInfo });
			}
		}
	};

	for (auto& PropertyPair : TypeInfo->Properties)
	{
		GDK_PROPERTY(Property)* Property = PropertyPair.Key;
		TSharedPtr<FUnrealType>& PropertyTypeInfo = PropertyPair.Value->Type;

		if (Property->IsA<GDK_PROPERTY(ObjectProperty)>() && PropertyTypeInfo.IsValid())
		{
			AddSubobject(PropertyTypeInfo);
		}
	}

	for (const FUnrealSubobject& NonPropertySubobject : TypeInfo->NoPropertySubobjects)
	{
		if (NonPropertySubobject.Type->Object->IsSupportedForNetworking())
		{
			AddSubobject(NonPropertySubobject.Type);
		}
	}

	return Subobjects;
}
