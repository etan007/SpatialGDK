// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "SpatialGDKEditorSchemaGenerator.h"

#include "AssetRegistryModule.h"
#include "Async/Async.h"
#include "Components/SceneComponent.h"
#include "Editor.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "GeneralProjectSettings.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "HAL/PlatformFilemanager.h"
#include "Hash/CityHash.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/MonitoredProcess.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Templates/SharedPointer.h"
#include "UObject/UObjectIterator.h"

#include "Engine/WorldComposition.h"
#include "Interop/SpatialClassInfoManager.h"
#include "Misc/ScopedSlowTask.h"
#include "SchemaGenerator.h"
#include "Settings/ProjectPackagingSettings.h"
#include "SpatialConstants.h"
#include "SpatialGDKEditorSettings.h"
#include "SpatialGDKServicesConstants.h"
#include "SpatialGDKServicesModule.h"
#include "SpatialGDKSettings.h"
#include "TypeStructure.h"
#include "UObject/StrongObjectPtr.h"
#include "Utils/CodeWriter.h"
#include "Utils/ComponentIdGenerator.h"
#include "Utils/DataTypeUtilities.h"
#include "Utils/RepLayoutUtils.h"
#include "Utils/SchemaDatabase.h"

#include <WorkerSDK/improbable/c_worker.h>
#define GDK_CREATE_PACKAGE(PackagePath) CreatePackage((PackagePath));


// clang-format off
#define SAFE_TRYGET(Value, Type, OutParam)                                                                                                 \
	do                                                                                                                                     \
	{                                                                                                                                      \
		if (!Value->TryGet##Type(OutParam))                                                                                                \
		{                                                                                                                                  \
			UE_LOG(LogSpatialGDKSchemaGenerator, Error, TEXT("Failed to get %s as type %s"), TEXT(#Value), TEXT(#Type));                   \
			return false;                                                                                                                  \
		}                                                                                                                                  \
	} while (false)

#define SAFE_TRYGETFIELD(Value, Type, FieldName, OutParam)                                                                                 \
	do                                                                                                                                     \
	{                                                                                                                                      \
		if (!Value->TryGet##Type##Field(TEXT(FieldName), OutParam))                                                                        \
		{                                                                                                                                  \
			UE_LOG(LogSpatialGDKSchemaGenerator, Error, TEXT("Failed to get field %s of type %s from %s"), TEXT(FieldName), TEXT(#Type), TEXT(#Value)); \
			return false;                                                                                                                  \
		}                                                                                                                                  \
	} while (false)

#define COND_SCHEMA_GEN_ERROR_AND_RETURN(Condition, Format, ...)                                                                                           \
	if (UNLIKELY(Condition))                                                                                                               \
	{                                                                                                                                      \
		UE_LOG(LogSpatialGDKSchemaGenerator, Error, Format, ##__VA_ARGS__);                                                                \
		return false;                                                                                                                      \
	}
// clang-format on

DEFINE_LOG_CATEGORY(LogSpatialGDKSchemaGenerator);
#define LOCTEXT_NAMESPACE "SpatialGDKSchemaGenerator"

TArray<UClass*> SchemaGeneratedClasses;
TMap<FString, FActorSchemaData> ActorClassPathToSchema;
TMap<FString, FSubobjectSchemaData> SubobjectClassPathToSchema;
Worker_ComponentId NextAvailableComponentId = SpatialConstants::STARTING_GENERATED_COMPONENT_ID;

// LevelStreaming
TMap<FString, Worker_ComponentId> LevelPathToComponentId;

// Prevent name collisions.
TMap<FString, FString> ClassPathToSchemaName;
TMap<FString, FString> SchemaNameToClassPath;
TMap<FString, TSet<FString>> PotentialSchemaNameCollisions;

// QBI
TMap<float, Worker_ComponentId> NetCullDistanceToComponentId;

namespace
{
const FString& GetRelativeSchemaDatabaseFilePath()
{
	static const FString s_RelativeFilePath =
		FPaths::SetExtension(FPaths::Combine(FPaths::ProjectContentDir(), SpatialConstants::SCHEMA_DATABASE_FILE_PATH),
							 FPackageName::GetAssetPackageExtension());

	return s_RelativeFilePath;
}
} // namespace

namespace SpatialGDKEditor
{
namespace Schema
{
void AddPotentialNameCollision(const FString& DesiredSchemaName, const FString& ClassPath, const FString& GeneratedSchemaName)
{
	PotentialSchemaNameCollisions.FindOrAdd(DesiredSchemaName).Add(FString::Printf(TEXT("%s(%s)"), *ClassPath, *GeneratedSchemaName));
}

void OnStatusOutput(const FString& Message)
{
	UE_LOG(LogSpatialGDKSchemaGenerator, Log, TEXT("%s"), *Message);
}

void GenerateCompleteSchemaFromClass(const FString& SchemaPath, FComponentIdGenerator& IdGenerator, TSharedPtr<FUnrealType> TypeInfo)
{
	UClass* Class = Cast<UClass>(TypeInfo->Type);

	if (Class->IsChildOf<AActor>())
	{
		GenerateActorSchema(IdGenerator, Class, TypeInfo, SchemaPath);
	}
	else
	{
		GenerateSubobjectSchema(IdGenerator, Class, TypeInfo, FPaths::Combine(SchemaPath, TEXT("Subobjects")));
	}
}

bool CheckSchemaNameValidity(const FString& Name, const FString& Identifier, const FString& Category)
{
	if (Name.IsEmpty())
	{
		UE_LOG(LogSpatialGDKSchemaGenerator, Error,
			   TEXT("%s %s is empty after removing non-alphanumeric characters, schema not generated."), *Category, *Identifier);
		return false;
	}

	if (FChar::IsDigit(Name[0]))
	{
		UE_LOG(LogSpatialGDKSchemaGenerator, Error,
			   TEXT("%s names should not start with digits. %s %s (%s) has leading digits (potentially after removing non-alphanumeric "
					"characters), schema not generated."),
			   *Category, *Category, *Name, *Identifier);
		return false;
	}

	return true;
}

void CheckIdentifierNameValidity(TSharedPtr<FUnrealType> TypeInfo, bool& bOutSuccess)
{
	// Check Replicated data.
	FUnrealFlatRepData RepData = GetFlatRepData(TypeInfo);
	for (EReplicatedPropertyGroup Group : GetAllReplicatedPropertyGroups())
	{
		TMap<FString, TSharedPtr<FUnrealProperty>> SchemaReplicatedDataNames;
		for (auto& RepProp : RepData[Group])
		{
			FString NextSchemaReplicatedDataName = SchemaFieldName(RepProp.Value);

			if (!CheckSchemaNameValidity(NextSchemaReplicatedDataName, RepProp.Value->Property->GetPathName(), TEXT("Replicated property")))
			{
				bOutSuccess = false;
			}

			if (TSharedPtr<FUnrealProperty>* ExistingReplicatedProperty = SchemaReplicatedDataNames.Find(NextSchemaReplicatedDataName))
			{
				UE_LOG(LogSpatialGDKSchemaGenerator, Error,
					   TEXT("Replicated property name collision after removing non-alphanumeric characters, schema not generated. Name "
							"'%s' collides for '%s' and '%s'"),
					   *NextSchemaReplicatedDataName, *ExistingReplicatedProperty->Get()->Property->GetPathName(),
					   *RepProp.Value->Property->GetPathName());
				bOutSuccess = false;
			}
			else
			{
				SchemaReplicatedDataNames.Add(NextSchemaReplicatedDataName, RepProp.Value);
			}
		}
	}

	// Check subobject name validity.
	FSubobjects Subobjects = GetAllSubobjects(TypeInfo);
	TMap<FString, TSharedPtr<FUnrealType>> SchemaSubobjectNames;
	for (auto& It : Subobjects)
	{
		const TSharedPtr<FUnrealType>& SubobjectTypeInfo = It.Type;
		FString NextSchemaSubobjectName = UnrealNameToSchemaComponentName(SubobjectTypeInfo->Name.ToString());

		if (!CheckSchemaNameValidity(NextSchemaSubobjectName, SubobjectTypeInfo->Object->GetPathName(), TEXT("Subobject")))
		{
			bOutSuccess = false;
		}

		if (TSharedPtr<FUnrealType>* ExistingSubobject = SchemaSubobjectNames.Find(NextSchemaSubobjectName))
		{
			UE_LOG(LogSpatialGDKSchemaGenerator, Error,
				   TEXT("Subobject name collision after removing non-alphanumeric characters, schema not generated. Name '%s' collides for "
						"'%s' and '%s'"),
				   *NextSchemaSubobjectName, *ExistingSubobject->Get()->Object->GetPathName(), *SubobjectTypeInfo->Object->GetPathName());
			bOutSuccess = false;
		}
		else
		{
			SchemaSubobjectNames.Add(NextSchemaSubobjectName, SubobjectTypeInfo);
		}
	}
}

bool ValidateIdentifierNames(TArray<TSharedPtr<FUnrealType>>& TypeInfos)
{
	bool bSuccess = true;

	// Remove all underscores from the class names, check for duplicates or invalid schema names.
	for (const auto& TypeInfo : TypeInfos)
	{
		UClass* Class = Cast<UClass>(TypeInfo->Type);
		check(Class);
		const FString& ClassName = Class->GetName();
		const FString& ClassPath = Class->GetPathName();
		FString SchemaName = UnrealNameToSchemaName(ClassName, true);

		if (!CheckSchemaNameValidity(SchemaName, ClassPath, TEXT("Class")))
		{
			bSuccess = false;
		}

		FString DesiredSchemaName = SchemaName;

		if (ClassPathToSchemaName.Contains(ClassPath))
		{
			continue;
		}

		int Suffix = 0;
		while (SchemaNameToClassPath.Contains(SchemaName))
		{
			SchemaName = UnrealNameToSchemaName(ClassName) + FString::Printf(TEXT("%d"), ++Suffix);
		}

		ClassPathToSchemaName.Add(ClassPath, SchemaName);
		SchemaNameToClassPath.Add(SchemaName, ClassPath);

		if (DesiredSchemaName != SchemaName)
		{
			AddPotentialNameCollision(DesiredSchemaName, ClassPath, SchemaName);
		}
		AddPotentialNameCollision(SchemaName, ClassPath, SchemaName);
	}

	for (const auto& Collision : PotentialSchemaNameCollisions)
	{
		if (Collision.Value.Num() > 1)
		{
			UE_LOG(LogSpatialGDKSchemaGenerator, Display,
				   TEXT("Class name collision after removing non-alphanumeric characters. Name '%s' collides for classes [%s]"),
				   *Collision.Key, *FString::Join(Collision.Value, TEXT(", ")));
		}
	}

	// Check for invalid/duplicate names in the generated type info.
	for (auto& TypeInfo : TypeInfos)
	{
		CheckIdentifierNameValidity(TypeInfo, bSuccess);
	}

	return bSuccess;
}

bool ValidateAlwaysWriteRPCs(const TArray<TSharedPtr<FUnrealType>>& TypeInfos)
{
	bool bSuccess = true;

	for (const auto& TypeInfo : TypeInfos)
	{
		UClass* Class = Cast<UClass>(TypeInfo->Type);
		check(Class);

		TArray<UFunction*> RPCs = SpatialGDK::GetClassRPCFunctions(Class);
		TArray<UFunction*> AlwaysWriteRPCs;

		for (UFunction* RPC : RPCs)
		{
			if (RPC->SpatialFunctionFlags & SPATIALFUNC_AlwaysWrite)
			{
				AlwaysWriteRPCs.Add(RPC);
			}
		}

		if (!Class->IsChildOf<AActor>() && AlwaysWriteRPCs.Num() > 0)
		{
			UE_LOG(LogSpatialGDKSchemaGenerator, Error,
				   TEXT("Found AlwaysWrite RPC(s) on a subobject class. This is not supported. Please route it through the owning actor if "
						"AlwaysWrite behavior is necessary. Class: %s, function(s):"),
				   *Class->GetPathName());
			for (UFunction* RPC : AlwaysWriteRPCs)
			{
				UE_LOG(LogSpatialGDKSchemaGenerator, Error, TEXT("%s"), *RPC->GetName());
			}
			bSuccess = false;
		}
		else if (AlwaysWriteRPCs.Num() > 1)
		{
			UE_LOG(LogSpatialGDKSchemaGenerator, Error,
				   TEXT("Found more than 1 function with AlwaysWrite for class. This is not supported. Class: %s, functions:"),
				   *Class->GetPathName());
			for (UFunction* RPC : AlwaysWriteRPCs)
			{
				UE_LOG(LogSpatialGDKSchemaGenerator, Error, TEXT("%s"), *RPC->GetName());
			}
			bSuccess = false;
		}
	}

	return bSuccess;
}

void GenerateSchemaFromClasses(const TArray<TSharedPtr<FUnrealType>>& TypeInfos, const FString& CombinedSchemaPath,
							   FComponentIdGenerator& IdGenerator)
{
	// Generate the actual schema.
	FScopedSlowTask Progress((float)TypeInfos.Num(), LOCTEXT("GenerateSchemaFromClasses", "Generating schema..."));
	for (const auto& TypeInfo : TypeInfos)
	{
		Progress.EnterProgressFrame(1.f);
		GenerateCompleteSchemaFromClass(CombinedSchemaPath, IdGenerator, TypeInfo);
	}
}

void WriteLevelComponent(FCodeWriter& Writer, const FString& LevelName, Worker_ComponentId ComponentId, const FString& ClassPath)
{
	FString ComponentName = UnrealNameToSchemaComponentName(LevelName);
	Writer.PrintNewLine();
	Writer.Printf("// {0}", *ClassPath);
	Writer.Printf("message {0} {", *ComponentName);
	Writer.Indent();
	Writer.Printf("optional uint32 id = 1[default = {0}];", ComponentId);
	Writer.Outdent().Print("}");
}

TMultiMap<FName, FName> GetLevelNamesToPathsMap()
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

	TArray<FAssetData> WorldAssets;
	AssetRegistryModule.Get().GetAllAssets(WorldAssets, true);

	// Filter assets to game maps.
	WorldAssets = WorldAssets.FilterByPredicate([](FAssetData Data) {
		return (Data.AssetClass == UWorld::StaticClass()->GetFName() && Data.PackagePath.ToString().StartsWith("/Game"));
	});

	TMultiMap<FName, FName> LevelNamesToPaths;

	for (FAssetData World : WorldAssets)
	{
		LevelNamesToPaths.Add(World.AssetName, World.PackageName);
	}

	return LevelNamesToPaths;
}

void GenerateSchemaForSublevels()
{
	const FString SchemaOutputPath = GetDefault<USpatialGDKEditorSettings>()->GetGeneratedSchemaOutputFolder();
	TMultiMap<FName, FName> LevelNamesToPaths = GetLevelNamesToPathsMap();
	GenerateSchemaForSublevels(SchemaOutputPath, LevelNamesToPaths);
}

void GenerateSchemaForSublevels(const FString& SchemaOutputPath, const TMultiMap<FName, FName>& LevelNamesToPaths)
{
	FCodeWriter Writer;
	Writer.Printf(R"""(
		syntax = "proto2";
		// Note that this file has been generated automatically
		package unreal.sublevels;)""");

	FComponentIdGenerator IdGenerator = FComponentIdGenerator(NextAvailableComponentId);

	TArray<FName> Keys;
	LevelNamesToPaths.GetKeys(Keys);

	for (FName LevelName : Keys)
	{
		if (LevelNamesToPaths.Num(LevelName) > 1)
		{
			// Write multiple numbered components.
			TArray<FName> LevelPaths;
			LevelNamesToPaths.MultiFind(LevelName, LevelPaths);
			FString LevelNameString = LevelName.ToString();

			for (int i = 0; i < LevelPaths.Num(); i++)
			{
				Worker_ComponentId ComponentId = LevelPathToComponentId.FindRef(LevelPaths[i].ToString());
				if (ComponentId == 0)
				{
					ComponentId = IdGenerator.Next();
					LevelPathToComponentId.Add(LevelPaths[i].ToString(), ComponentId);
				}
				WriteLevelComponent(Writer, FString::Printf(TEXT("%sInd%d"), *LevelNameString, i), ComponentId, LevelPaths[i].ToString());
			}
		}
		else
		{
			// Write a single component.
			FString LevelPath = LevelNamesToPaths.FindRef(LevelName).ToString();
			Worker_ComponentId ComponentId = LevelPathToComponentId.FindRef(LevelPath);
			if (ComponentId == 0)
			{
				ComponentId = IdGenerator.Next();
				LevelPathToComponentId.Add(LevelPath, ComponentId);
			}
			WriteLevelComponent(Writer, LevelName.ToString(), ComponentId, LevelPath);
		}
	}

	NextAvailableComponentId = IdGenerator.Peek();

	Writer.WriteToFile(FPaths::Combine(*SchemaOutputPath, TEXT("Sublevels/sublevels.proto")));
}

void GenerateSchemaForRPCEndpoints()
{
	GenerateSchemaForRPCEndpoints(GetDefault<USpatialGDKEditorSettings>()->GetGeneratedSchemaOutputFolder());
}

void GenerateSchemaForRPCEndpoints(const FString& SchemaOutputPath)
{
	GenerateRPCEndpointsSchema(SchemaOutputPath);
}

void GenerateSchemaForNCDs()
{
	GenerateSchemaForNCDs(GetDefault<USpatialGDKEditorSettings>()->GetGeneratedSchemaOutputFolder());
}

void GenerateSchemaForNCDs(const FString& SchemaOutputPath)
{
	FCodeWriter Writer;
	Writer.Printf(R"""(
		syntax = "proto2";
		// Note that this file has been generated automatically
		package unreal.ncdcomponents;)""");

	FComponentIdGenerator IdGenerator = FComponentIdGenerator(NextAvailableComponentId);

	for (auto& NCDComponent : NetCullDistanceToComponentId)
	{
		const FString ComponentName = FString::Printf(TEXT("NetCullDistanceSquared%lld"), static_cast<uint64>(NCDComponent.Key));
		if (NCDComponent.Value == 0)
		{
			NCDComponent.Value = IdGenerator.Next();
		}

		FString SchemaComponentName = UnrealNameToSchemaComponentName(ComponentName);
		Worker_ComponentId ComponentId = NCDComponent.Value;

		Writer.PrintNewLine();
		Writer.Printf("// distance {0}", NCDComponent.Key);
		Writer.Printf("message {0} {", *SchemaComponentName);
		Writer.Indent();
		Writer.Printf("optional uint32 id = 1[default = {0}];", ComponentId);
		Writer.Outdent().Print("}");
	}

	NextAvailableComponentId = IdGenerator.Peek();

	Writer.WriteToFile(FPaths::Combine(*SchemaOutputPath, TEXT("NetCullDistance/ncdcomponents.proto")));
}

FString GenerateIntermediateDirectory()
{
	const FString CombinedIntermediatePath = FPaths::Combine(*FPaths::GetPath(FPaths::GetProjectFilePath()),
															 TEXT("Intermediate/Improbable/"), *FGuid::NewGuid().ToString(), TEXT("/"));
	FString AbsoluteCombinedIntermediatePath = FPaths::ConvertRelativePathToFull(CombinedIntermediatePath);
	FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*AbsoluteCombinedIntermediatePath);

	return AbsoluteCombinedIntermediatePath;
}

TMap<Worker_ComponentId, FString> CreateComponentIdToClassPathMap()
{
	TMap<Worker_ComponentId, FString> ComponentIdToClassPath;

	for (const auto& ActorSchemaData : ActorClassPathToSchema)
	{
		ForAllSchemaComponentTypes([&](ESchemaComponentType Type) {
			ComponentIdToClassPath.Add(ActorSchemaData.Value.SchemaComponents[Type], ActorSchemaData.Key);
		});

		for (const auto& SubobjectSchemaData : ActorSchemaData.Value.SubobjectData)
		{
			ForAllSchemaComponentTypes([&](ESchemaComponentType Type) {
				ComponentIdToClassPath.Add(SubobjectSchemaData.Value.SchemaComponents[Type], SubobjectSchemaData.Value.ClassPath);
			});
		}
	}

	for (const auto& SubobjectSchemaData : SubobjectClassPathToSchema)
	{
		for (const auto& DynamicSubobjectData : SubobjectSchemaData.Value.DynamicSubobjectComponents)
		{
			ForAllSchemaComponentTypes([&](ESchemaComponentType Type) {
				ComponentIdToClassPath.Add(DynamicSubobjectData.SchemaComponents[Type], SubobjectSchemaData.Key);
			});
		}
	}

	ComponentIdToClassPath.Remove(SpatialConstants::INVALID_COMPONENT_ID);

	return ComponentIdToClassPath;
}

FString GetComponentSetNameBySchemaType(ESchemaComponentType SchemaType)
{
	static_assert(SCHEMA_Count == 4, "Unexpected number of Schema type components, please check the enclosing function is still correct.");

	switch (SchemaType)
	{
	case SCHEMA_Data:
		return SpatialConstants::DATA_COMPONENT_SET_NAME;
	case SCHEMA_OwnerOnly:
		return SpatialConstants::OWNER_ONLY_COMPONENT_SET_NAME;
	case SCHEMA_ServerOnly:
		return SpatialConstants::SERVER_ONLY_COMPONENT_SET_NAME;
	case SCHEMA_InitialOnly:
		return SpatialConstants::INITIAL_ONLY_COMPONENT_SET_NAME;
	default:
		// For some reason these statements, if formatted cause a bug in VS where the lines reported by the compiler and debugger are wrong.
		// clang-format off
		UE_LOG(LogSpatialGDKSchemaGenerator, Error, TEXT("Could not return component set name. Schema component type was invalid: %d"), SchemaType);
		// clang-format on
		return FString();
	}
}

Worker_ComponentId GetComponentSetIdBySchemaType(ESchemaComponentType SchemaType)
{
	static_assert(SCHEMA_Count == 4, "Unexpected number of Schema type components, please check the enclosing function is still correct.");

	switch (SchemaType)
	{
	case SCHEMA_Data:
		return SpatialConstants::DATA_COMPONENT_SET_ID;
	case SCHEMA_OwnerOnly:
		return SpatialConstants::OWNER_ONLY_COMPONENT_SET_ID;
	case SCHEMA_ServerOnly:
		return SpatialConstants::HANDOVER_COMPONENT_SET_ID;
	case SCHEMA_InitialOnly:
		return SpatialConstants::INITIAL_ONLY_COMPONENT_SET_ID;
	default:
		// clang-format off
		UE_LOG(LogSpatialGDKSchemaGenerator, Error, TEXT("Could not return component set ID. Schema component type was invalid: %d"), SchemaType);
		// clang-format on
		return SpatialConstants::INVALID_COMPONENT_ID;
	}
}

FString GetComponentSetOutputPathBySchemaType(const FString& BasePath, ESchemaComponentType SchemaType)
{
	const FString ComponentSetName = GetComponentSetNameBySchemaType(SchemaType);
	FString FileName = FString::Printf(TEXT("%s.proto"), *ComponentSetName);
	return FPaths::Combine(*BasePath, FPaths::Combine(TEXT("ComponentSets"), *FileName));
}

void WriteServerAuthorityComponentSet(const USchemaDatabase* SchemaDatabase, const FString& SchemaOutputPath)
{
	FCodeWriter Writer;
	Writer.Printf(R"""(
		syntax = "proto2";
		// Note that this file has been generated automatically
		package unreal.generated;)""");
	Writer.PrintNewLine();
	// Write all import statements.
	{
		// Well-known SpatialOS and handwritten GDK schema files.
		for (const auto& WellKnownSchemaImport : SpatialConstants::ServerAuthorityWellKnownSchemaImports)
		{
			Writer.Printf("import \"{0}\";", WellKnownSchemaImport);
		}

		const FString IncludePath = TEXT("unreal/generated");
		for (const auto& GeneratedActorClass : SchemaDatabase->ActorClassPathToSchema)
		{
			const FString ActorClassName = UnrealNameToSchemaName(GeneratedActorClass.Value.GeneratedSchemaName);
			Writer.Printf("import \"{0}/{1}.proto\";", IncludePath, ActorClassName);
			if (GeneratedActorClass.Value.SubobjectData.Num() > 0)
			{
				Writer.Printf("import \"{0}/{1}Components.proto\";", IncludePath, ActorClassName);
			}
		}

		for (const auto& GeneratedSubObjectClass : SchemaDatabase->SubobjectClassPathToSchema)
		{
			const FString SubObjectClassName = UnrealNameToSchemaName(GeneratedSubObjectClass.Value.GeneratedSchemaName);
			Writer.Printf("import \"{0}/Subobjects/{1}.proto\";", IncludePath, SubObjectClassName);
		}
	}

	Writer.PrintNewLine();
	Writer.Printf("message {0} {", SpatialConstants::SERVER_AUTH_COMPONENT_SET_NAME).Indent();
	Writer.Printf("optional uint32 id = 1[default = {0}];", SpatialConstants::SERVER_AUTH_COMPONENT_SET_ID);
	Writer.Printf("message Components{").Indent();

	// Write all components.
	{
		int nIndex = 1;
		// Well-known SpatialOS and handwritten GDK components.
		for (const auto& WellKnownComponent : SpatialConstants::ServerAuthorityWellKnownComponents)
		{
			Writer.Printf("optional {0} cpts_x{1} = {2};", WellKnownComponent.Value,nIndex,nIndex);
			nIndex++;
		}

		// NCDs.
		for (auto& NCDComponent : NetCullDistanceToComponentId)
		{
			const FString NcdComponentName = FString::Printf(TEXT("NetCullDistanceSquared%lld"), static_cast<uint64>(NCDComponent.Key));
			Writer.Printf("optional unreal.ncdcomponents.{0} cpts_x{1} = {2};", NcdComponentName,nIndex,nIndex);
			nIndex++;
		}

		for (const auto& GeneratedActorClass : SchemaDatabase->ActorClassPathToSchema)
		{
			// Actor components.
			const FString& ActorClassName = UnrealNameToSchemaComponentName(GeneratedActorClass.Value.GeneratedSchemaName);
			ForAllSchemaComponentTypes([&](ESchemaComponentType SchemaType) {
				const Worker_ComponentId ComponentId = GeneratedActorClass.Value.SchemaComponents[SchemaType];
				if (ComponentId != 0)
				{
					Writer.Printf("optional unreal.generated.{0}.{1}{2} cpts_x{3} = {4};", ActorClassName.ToLower(), ActorClassName,
								  GetReplicatedPropertyGroupName(SchemaComponentTypeToPropertyGroup(SchemaType)),nIndex,nIndex);
					nIndex++;
				}
			});

			// Actor static subobjects.
			for (const auto& ActorSubObjectData : GeneratedActorClass.Value.SubobjectData)
			{
				const FString ActorSubObjectName = UnrealNameToSchemaComponentName(ActorSubObjectData.Value.Name.ToString());
				ForAllSchemaComponentTypes([&](ESchemaComponentType SchemaType) {
					const Worker_ComponentId& ComponentId = ActorSubObjectData.Value.SchemaComponents[SchemaType];
					if (ComponentId != 0)
					{
						Writer.Printf("optional unreal.generated.{0}.subobjects.{1}{2} cpts_x{3} = {4};", ActorClassName.ToLower(), ActorSubObjectName,
									  GetReplicatedPropertyGroupName(SchemaComponentTypeToPropertyGroup(SchemaType)),nIndex,nIndex);
						nIndex++;
					}
				});
			}
		}

		// Dynamic subobjects.
		for (const auto& GeneratedSubObjectClass : SchemaDatabase->SubobjectClassPathToSchema)
		{
			const FString& SubObjectClassName = UnrealNameToSchemaComponentName(GeneratedSubObjectClass.Value.GeneratedSchemaName);
			for (auto SubObjectNumber = 0; SubObjectNumber < GeneratedSubObjectClass.Value.DynamicSubobjectComponents.Num();
				 ++SubObjectNumber)
			{
				const FDynamicSubobjectSchemaData& SubObjectSchemaData =
					GeneratedSubObjectClass.Value.DynamicSubobjectComponents[SubObjectNumber];
				ForAllSchemaComponentTypes([&](ESchemaComponentType SchemaType) {
					const Worker_ComponentId& ComponentId = SubObjectSchemaData.SchemaComponents[SchemaType];
					if (ComponentId != 0)
					{
						Writer.Printf("optional unreal.generated.{0}{1}Dynamic{2} cpts_x{3} = {4};", SubObjectClassName,
									  GetReplicatedPropertyGroupName(SchemaComponentTypeToPropertyGroup(SchemaType)), SubObjectNumber + 1,nIndex,nIndex);
						nIndex++;
					}
				});
			}
		}
	}

	Writer.RemoveTrailingComma();

	Writer.Outdent().Print("}");
	Writer.Outdent().Print("}");

	Writer.WriteToFile(FPaths::Combine(*SchemaOutputPath, TEXT("ComponentSets/ServerAuthoritativeComponentSet.proto")));
}

void WriteRoutingWorkerAuthorityComponentSet(const FString& SchemaOutputPath)
{
	FCodeWriter Writer;
	Writer.Printf(R"""(
		syntax = "proto2";
		// Note that this file has been generated automatically
		package unreal.generated;)""");
	Writer.PrintNewLine();
	// Write all import statements.
	for (const auto& WellKnownSchemaImport : SpatialConstants::RoutingWorkerSchemaImports)
	{
		Writer.Printf("import \"{0}\";", WellKnownSchemaImport);
	}

	Writer.PrintNewLine();
	Writer.Printf("message {0} {", SpatialConstants::ROUTING_WORKER_COMPONENT_SET_NAME).Indent();
	Writer.Printf("optional uint32 id = 1[default = {0}];", SpatialConstants::ROUTING_WORKER_AUTH_COMPONENT_SET_ID);
	Writer.Printf("message Components{").Indent();

	int nIndex = 1;
	// Write all import components.
	for (const auto& WellKnownComponent : SpatialConstants::RoutingWorkerComponents)
	{
		Writer.Printf("optional {0} cpts_x{1} = {2};", WellKnownComponent.Value,nIndex,nIndex);
		nIndex++;
	}

	Writer.RemoveTrailingComma();

	Writer.Outdent().Print("}");
	Writer.Outdent().Print("}");

	Writer.WriteToFile(FPaths::Combine(*SchemaOutputPath, TEXT("ComponentSets/RoutingWorkerAuthoritativeComponentSet.proto")));
}

void WriteClientAuthorityComponentSet(const FString& SchemaOutputPath)
{
	FCodeWriter Writer;
	Writer.Printf(R"""(
		syntax = "proto2";
		// Note that this file has been generated automatically
		package unreal.generated;)""");
	Writer.PrintNewLine();
	// Write all import statements.
	for (const auto& WellKnownSchemaImport : SpatialConstants::ClientAuthorityWellKnownSchemaImports)
	{
		Writer.Printf("import \"{0}\";", WellKnownSchemaImport);
	}

	Writer.PrintNewLine();
	Writer.Printf("message {0} {", SpatialConstants::CLIENT_AUTH_COMPONENT_SET_NAME).Indent();
	Writer.Printf("optional uint32 id = 1[default = {0}];", SpatialConstants::CLIENT_AUTH_COMPONENT_SET_ID);
	Writer.Printf("message Components{").Indent();
    int nIndex = 1;
	// Write all import components.
	for (const auto& WellKnownComponent : SpatialConstants::ClientAuthorityWellKnownComponents)
	{
		Writer.Printf("optional {0} cpts_x{1} = {2};", WellKnownComponent.Value,nIndex,nIndex);
		nIndex++;
	}

	Writer.RemoveTrailingComma();

	Writer.Outdent().Print("}");
	Writer.Outdent().Print("}");

	Writer.WriteToFile(FPaths::Combine(*SchemaOutputPath, TEXT("ComponentSets/ClientAuthoritativeComponentSet.proto")));
}

void WriteComponentSetBySchemaType(const USchemaDatabase* SchemaDatabase, ESchemaComponentType SchemaType, const FString& SchemaOutputPath)
{
	FCodeWriter Writer;
	Writer.Printf(R"""(
		syntax = "proto2";
		// Note that this file has been generated automatically
		package unreal.generated;)""");
	Writer.PrintNewLine();
	// Write all import statements.
	{
		const FString IncludePath = TEXT("unreal/generated");
		for (const auto& GeneratedActorClass : SchemaDatabase->ActorClassPathToSchema)
		{
			const FString ActorClassName = UnrealNameToSchemaName(GeneratedActorClass.Value.GeneratedSchemaName);
			if (GeneratedActorClass.Value.SchemaComponents[SchemaType] != 0)
			{
				Writer.Printf("import \"{0}/{1}.proto\";", IncludePath, ActorClassName);
			}
			for (const auto& SubObjectData : GeneratedActorClass.Value.SubobjectData)
			{
				if (SubObjectData.Value.SchemaComponents[SchemaType] != 0)
				{
					Writer.Printf("import \"{0}/{1}Components.proto\";", IncludePath, ActorClassName);
					break;
				}
			}
		}
		for (const auto& GeneratedSubObjectClass : SchemaDatabase->SubobjectClassPathToSchema)
		{
			const FString SubObjectClassName = UnrealNameToSchemaName(GeneratedSubObjectClass.Value.GeneratedSchemaName);
			for (const auto& SubObjectData : GeneratedSubObjectClass.Value.DynamicSubobjectComponents)
			{
				if (SubObjectData.SchemaComponents[SchemaType] != 0)
				{
					Writer.Printf("import \"{0}/Subobjects/{1}.proto\";", IncludePath, SubObjectClassName);
					break;
				}
			}
		}
	}

	Writer.PrintNewLine();
	Writer.Printf("message {0} {", GetComponentSetNameBySchemaType(SchemaType)).Indent();
	//Writer.Printf("id = {0};", GetComponentSetIdBySchemaType(SchemaType));
	Writer.Printf("optional uint32 id = 1[default = {0}];", GetComponentSetIdBySchemaType(SchemaType));
	Writer.Printf("message Components{").Indent();

	FString SchemaTypeString = GetReplicatedPropertyGroupName(SchemaComponentTypeToPropertyGroup(SchemaType));

	// Write all components.
	{
		int nIndex = 1;
		for (const auto& GeneratedActorClass : SchemaDatabase->ActorClassPathToSchema)
		{
			// Actor components.
			const FString& ActorClassName = UnrealNameToSchemaComponentName(GeneratedActorClass.Value.GeneratedSchemaName);
			if (GeneratedActorClass.Value.SchemaComponents[SchemaType] != 0)
			{
				Writer.Printf("optional unreal.generated.{0}.{1}{2} cpts_x{3} = {4};", ActorClassName.ToLower(), ActorClassName, SchemaTypeString,nIndex,nIndex);
				nIndex++;
			}
			// Actor static subobjects.
			for (const auto& ActorSubObjectData : GeneratedActorClass.Value.SubobjectData)
			{
				const FString ActorSubObjectName = UnrealNameToSchemaComponentName(ActorSubObjectData.Value.Name.ToString());
				if (ActorSubObjectData.Value.SchemaComponents[SchemaType] != 0)
				{
					Writer.Printf("optional unreal.generated.{0}.subobjects.{1}{2} cpts_x{3} = {4};", ActorClassName.ToLower(), ActorSubObjectName,
								  SchemaTypeString,nIndex,nIndex);
					nIndex++;
				}
			}
		}
		// Dynamic subobjects.
		for (const auto& GeneratedSubObjectClass : SchemaDatabase->SubobjectClassPathToSchema)
		{
			const FString& SubObjectClassName = UnrealNameToSchemaComponentName(GeneratedSubObjectClass.Value.GeneratedSchemaName);
			for (auto SubObjectNumber = 0; SubObjectNumber < GeneratedSubObjectClass.Value.DynamicSubobjectComponents.Num();
				 ++SubObjectNumber)
			{
				const FDynamicSubobjectSchemaData& SubObjectSchemaData =
					GeneratedSubObjectClass.Value.DynamicSubobjectComponents[SubObjectNumber];
				if (SubObjectSchemaData.SchemaComponents[SchemaType] != 0)
				{
					Writer.Printf("optional unreal.generated.{0}{1}Dynamic{2} cpts_x{3} = {4};", SubObjectClassName, SchemaTypeString, SubObjectNumber + 1,nIndex,nIndex);
					nIndex++;
				}
			}
		}
	}

	Writer.RemoveTrailingComma();

	Writer.Outdent().Print("}");
	Writer.Outdent().Print("}");

	const FString OutputPath = GetComponentSetOutputPathBySchemaType(SchemaOutputPath, SchemaType);
	Writer.WriteToFile(OutputPath);
}

void WriteComponentSetFiles(const USchemaDatabase* SchemaDatabase, FString SchemaOutputPath)
{
	if (SchemaOutputPath == "")
	{
		SchemaOutputPath = GetDefault<USpatialGDKEditorSettings>()->GetGeneratedSchemaOutputFolder();
	}

	WriteServerAuthorityComponentSet(SchemaDatabase, SchemaOutputPath);
	WriteClientAuthorityComponentSet(SchemaOutputPath);
	WriteRoutingWorkerAuthorityComponentSet(SchemaOutputPath);
	WriteComponentSetBySchemaType(SchemaDatabase, SCHEMA_Data, SchemaOutputPath);
	WriteComponentSetBySchemaType(SchemaDatabase, SCHEMA_OwnerOnly, SchemaOutputPath);
	WriteComponentSetBySchemaType(SchemaDatabase, SCHEMA_ServerOnly, SchemaOutputPath);
	WriteComponentSetBySchemaType(SchemaDatabase, SCHEMA_InitialOnly, SchemaOutputPath);
	static_assert(SCHEMA_Count == 4, "Unexpected number of Schema type components, please check the enclosing function is still correct.");
}

USchemaDatabase* InitialiseSchemaDatabase(const FString& PackagePath)
{
	UPackage* Package = GDK_CREATE_PACKAGE(*PackagePath);

	ActorClassPathToSchema.KeySort([](const FString& LHS, const FString& RHS) {
		return LHS < RHS;
	});
	SubobjectClassPathToSchema.KeySort([](const FString& LHS, const FString& RHS) {
		return LHS < RHS;
	});
	LevelPathToComponentId.KeySort([](const FString& LHS, const FString& RHS) {
		return LHS < RHS;
	});

	USchemaDatabase* SchemaDatabase = NewObject<USchemaDatabase>(Package, USchemaDatabase::StaticClass(), FName("SchemaDatabase"),
																 EObjectFlags::RF_Public | EObjectFlags::RF_Standalone);
	SchemaDatabase->NextAvailableComponentId = NextAvailableComponentId;
	SchemaDatabase->ActorClassPathToSchema = ActorClassPathToSchema;
	SchemaDatabase->SubobjectClassPathToSchema = SubobjectClassPathToSchema;
	SchemaDatabase->LevelPathToComponentId = LevelPathToComponentId;
	SchemaDatabase->NetCullDistanceToComponentId = NetCullDistanceToComponentId;
	SchemaDatabase->ComponentIdToClassPath = CreateComponentIdToClassPathMap();

	SchemaDatabase->NetCullDistanceComponentIds.Reset();
	TArray<Worker_ComponentId> NetCullDistanceComponentIds;
	NetCullDistanceComponentIds.Reserve(NetCullDistanceToComponentId.Num());
	NetCullDistanceToComponentId.GenerateValueArray(NetCullDistanceComponentIds);
	SchemaDatabase->NetCullDistanceComponentIds.Append(NetCullDistanceComponentIds);

	SchemaDatabase->LevelComponentIds.Reset(LevelPathToComponentId.Num());
	LevelPathToComponentId.GenerateValueArray(SchemaDatabase->LevelComponentIds);

	SchemaDatabase->ComponentSetIdToComponentIds.Reset();

	// Save ring buffer sizes
	for (uint8 RPCType = static_cast<uint8>(ERPCType::RingBufferTypeBegin); RPCType <= static_cast<uint8>(ERPCType::RingBufferTypeEnd);
		 RPCType++)
	{
		SchemaDatabase->RPCRingBufferSizeMap.Add(static_cast<ERPCType>(RPCType),
												 GetDefault<USpatialGDKSettings>()->GetRPCRingBufferSize(static_cast<ERPCType>(RPCType)));
	}

	SchemaDatabase->SchemaDatabaseVersion = ESchemaDatabaseVersion::LatestVersion;

	return SchemaDatabase;
}

bool SaveSchemaDatabase(USchemaDatabase* SchemaDatabase)
{
	// Generate hash
	{
		SchemaDatabase->SchemaBundleHash = 0;
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

		TUniquePtr<IFileHandle> FileHandle(PlatformFile.OpenRead(*SpatialGDKServicesConstants::SchemaBundlePath));
		if (FileHandle)
		{
			// Create our byte buffer
			int64 FileSize = FileHandle->Size();
			TUniquePtr<uint8[]> ByteArray(new uint8[FileSize]);
			bool Result = FileHandle->Read(ByteArray.Get(), FileSize);
			if (Result)
			{
				SchemaDatabase->SchemaBundleHash = CityHash32(reinterpret_cast<const char*>(ByteArray.Get()), FileSize);
				// clang-format off
				UE_LOG(LogSpatialGDKSchemaGenerator, Display, TEXT("Generated schema bundle hash for database %u"), SchemaDatabase->SchemaBundleHash);
				// clang-format on
			}
			else
			{
				// clang-format off
				UE_LOG(LogSpatialGDKSchemaGenerator, Warning, TEXT("Failed to fully read schema.sb. Schema not saved. Location: %s"), *SpatialGDKServicesConstants::SchemaBundlePath);
				// clang-format on
			}
		}
		else
		{
			// clang-format off
			UE_LOG(LogSpatialGDKSchemaGenerator, Warning, TEXT("Failed to open schema.sb generated by the schema compiler! Location: %s"), *SpatialGDKServicesConstants::SchemaBundlePath);
			// clang-format on
		}
	}

	FAssetRegistryModule::AssetCreated(SchemaDatabase);
	SchemaDatabase->MarkPackageDirty();

	// NOTE: UPackage::GetMetaData() has some code where it will auto-create the metadata if it's missing
	// UPackage::SavePackage() calls UPackage::GetMetaData() at some point, and will cause an exception to get thrown
	// if the metadata auto-creation branch needs to be taken. This is the case when generating the schema from the
	// command line, so we just preempt it here.
	UPackage* Package = SchemaDatabase->GetOutermost();
	const FString& PackagePath = Package->GetPathName();
	Package->GetMetaData();

	FString FilePath = FString::Printf(TEXT("%s%s"), *PackagePath, *FPackageName::GetAssetPackageExtension());
	bool bSuccess = UPackage::SavePackage(Package, SchemaDatabase, EObjectFlags::RF_Public | EObjectFlags::RF_Standalone,
										  *FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension()),
										  GError, nullptr, false, true, SAVE_NoError);

	if (!bSuccess)
	{
		FString FullPath = FPaths::ConvertRelativePathToFull(FilePath);
		FPaths::MakePlatformFilename(FullPath);
		FMessageDialog::Debugf(FText::Format(
			LOCTEXT("SchemaDatabaseLocked_Error", "Unable to save schema database to '{0}'! The file may be locked by another process."),
			FText::FromString(FullPath)));
		return false;
	}

	return true;
}

bool IsSupportedClass(const UClass* SupportedClass)
{
	if (!IsValid(SupportedClass))
	{
		// clang-format off
		UE_LOG(LogSpatialGDKSchemaGenerator, Verbose, TEXT("[%s] Invalid Class not supported for schema gen."), *GetPathNameSafe(SupportedClass));
		// clang-format on
		return false;
	}

	if (SupportedClass->IsEditorOnly())
	{
		// clang-format off
		UE_LOG(LogSpatialGDKSchemaGenerator, Verbose, TEXT("[%s] Editor-only Class not supported for schema gen."), *GetPathNameSafe(SupportedClass));
		// clang-format on
		return false;
	}

	if (!SupportedClass->HasAnySpatialClassFlags(SPATIALCLASS_SpatialType))
	{
		if (SupportedClass->HasAnySpatialClassFlags(SPATIALCLASS_NotSpatialType))
		{
			// clang-format off
			UE_LOG(LogSpatialGDKSchemaGenerator, Verbose, TEXT("[%s] Has NotSpatialType flag, not supported for schema gen."), *GetPathNameSafe(SupportedClass));
			// clang-format on
		}
		else
		{
			// clang-format off
			UE_LOG(LogSpatialGDKSchemaGenerator, Verbose, TEXT("[%s] Has neither a SpatialType or NotSpatialType flag."), *GetPathNameSafe(SupportedClass));
			// clang-format on
		}

		return false;
	}

	if (SupportedClass->HasAnyClassFlags(CLASS_NeedsDeferredDependencyLoading))
	{
		// clang-format off
		UE_LOG(LogSpatialGDKSchemaGenerator, Verbose, TEXT("[%s] Layout CLASS_NeedsDeferredDependencyLoading, not supported"), *GetPathNameSafe(SupportedClass));
		// clang-format on
		return false;
	}

	// Ensure we don't process transient generated classes for BP
	if (SupportedClass->GetName().StartsWith(TEXT("SKEL_"), ESearchCase::CaseSensitive)
		|| SupportedClass->GetName().StartsWith(TEXT("REINST_"), ESearchCase::CaseSensitive)
		|| SupportedClass->GetName().StartsWith(TEXT("TRASHCLASS_"), ESearchCase::CaseSensitive)
		|| SupportedClass->GetName().StartsWith(TEXT("HOTRELOADED_"), ESearchCase::CaseSensitive)
		|| SupportedClass->GetName().StartsWith(TEXT("PROTO_BP_"), ESearchCase::CaseSensitive)
		|| SupportedClass->GetName().StartsWith(TEXT("PLACEHOLDER-CLASS_"), ESearchCase::CaseSensitive)
		|| SupportedClass->GetName().StartsWith(TEXT("ORPHANED_DATA_ONLY_"), ESearchCase::CaseSensitive))
	{
		// clang-format off
		UE_LOG(LogSpatialGDKSchemaGenerator, Verbose, TEXT("[%s] Transient Class not supported for schema gen"), *GetPathNameSafe(SupportedClass));
		// clang-format on
		return false;
	}

	const TArray<FDirectoryPath>& DirectoriesToNeverCook = GetDefault<UProjectPackagingSettings>()->DirectoriesToNeverCook;

	// Avoid processing classes contained in Directories to Never Cook
	const FString& ClassPath = SupportedClass->GetPathName();
	if (DirectoriesToNeverCook.ContainsByPredicate([&ClassPath](const FDirectoryPath& Directory) {
			return ClassPath.StartsWith(Directory.Path);
		}))
	{
		// clang-format off
		UE_LOG(LogSpatialGDKSchemaGenerator, Verbose, TEXT("[%s] Inside Directory to never cook for schema gen"), *GetPathNameSafe(SupportedClass));
		// clang-format on
		return false;
	}
	// clang-format off
	UE_LOG(LogSpatialGDKSchemaGenerator, Verbose, TEXT("[%s] Supported Class"), *GetPathNameSafe(SupportedClass));
	// clang-format on
	return true;
}

TSet<UClass*> GetAllSupportedClasses(const TArray<UObject*>& AllClasses)
{
	TSet<UClass*> Classes;

	for (const auto& ClassIt : AllClasses)
	{
		UClass* SupportedClass = Cast<UClass>(ClassIt);

		if (IsSupportedClass(SupportedClass))
		{
			Classes.Add(SupportedClass);
		}
	}

	return Classes;
}

void CopyWellKnownSchemaFiles(const FString& GDKSchemaCopyDir, const FString& CoreSDKSchemaCopyDir)
{
	FString PluginDir = FSpatialGDKServicesModule::GetSpatialGDKPluginDirectory();

	FString GDKSchemaDir = FPaths::Combine(PluginDir, TEXT("Extras/schema"));
	FString CoreSDKSchemaDir = FPaths::Combine(PluginDir, TEXT("Binaries/ThirdParty/Improbable/Programs/schema"));

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	RefreshSchemaFiles(*GDKSchemaCopyDir);
	if (!PlatformFile.CopyDirectoryTree(*GDKSchemaCopyDir, *GDKSchemaDir, true /*bOverwriteExisting*/))
	{
		// clang-format off
		UE_LOG(LogSpatialGDKSchemaGenerator, Error, TEXT("Could not copy gdk schema to '%s'! Please make sure the directory is writeable."), *GDKSchemaCopyDir);
		// clang-format on
	}

	RefreshSchemaFiles(*CoreSDKSchemaCopyDir);
	if (!PlatformFile.CopyDirectoryTree(*CoreSDKSchemaCopyDir, *CoreSDKSchemaDir, true /*bOverwriteExisting*/))
	{
		// clang-format off
		UE_LOG(LogSpatialGDKSchemaGenerator, Error, TEXT("Could not copy standard library schema to '%s'! Please make sure the directory is writeable."), *CoreSDKSchemaCopyDir);
		// clang-format on
	}
}

bool RefreshSchemaFiles(const FString& SchemaOutputPath, const bool bDeleteExistingSchema /*= true*/,
						const bool bCreateDirectoryTree /*= true*/)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (bDeleteExistingSchema && PlatformFile.DirectoryExists(*SchemaOutputPath))
	{
		if (!PlatformFile.DeleteDirectoryRecursively(*SchemaOutputPath))
		{
			// clang-format off
			UE_LOG(LogSpatialGDKSchemaGenerator, Error, TEXT("Could not clean the schema directory '%s'! Please make sure the directory and the files inside are writeable."), *SchemaOutputPath);
			// clang-format on
			return false;
		}
	}

	if (bCreateDirectoryTree && !PlatformFile.CreateDirectoryTree(*SchemaOutputPath))
	{
		// clang-format off
		UE_LOG(LogSpatialGDKSchemaGenerator, Error, TEXT("Could not create schema directory '%s'! Please make sure the parent directory is writeable."), *SchemaOutputPath);
		// clang-format on
		return false;
	}
	return true;
}

void ResetSchemaGeneratorState()
{
	ActorClassPathToSchema.Empty();
	SubobjectClassPathToSchema.Empty();
	LevelPathToComponentId.Empty();
	NextAvailableComponentId = SpatialConstants::STARTING_GENERATED_COMPONENT_ID;
	SchemaGeneratedClasses.Empty();
	NetCullDistanceToComponentId.Empty();
}

void ResetSchemaGeneratorStateAndCleanupFolders()
{
	ResetSchemaGeneratorState();
	RefreshSchemaFiles(GetDefault<USpatialGDKEditorSettings>()->GetGeneratedSchemaOutputFolder());
}

bool LoadGeneratorStateFromSchemaDatabase(const FString& FileName)
{
	FString RelativeFileName = FPaths::Combine(FPaths::ProjectContentDir(), FileName);
	RelativeFileName = FPaths::SetExtension(RelativeFileName, FPackageName::GetAssetPackageExtension());

	if (IsAssetReadOnly(FileName))
	{
		FString AbsoluteFilePath = FPaths::ConvertRelativePathToFull(RelativeFileName);
		// clang-format off
		UE_LOG(LogSpatialGDKSchemaGenerator, Error, TEXT("Schema generation failed: Schema Database at %s is read only. Make it writable before generating schema"), *AbsoluteFilePath);
		// clang-format on
		return false;
	}

	FFileStatData StatData = FPlatformFileManager::Get().GetPlatformFile().GetStatData(*RelativeFileName);
	if (StatData.bIsValid)
	{
		const FString DatabaseAssetPath = FPaths::SetExtension(FPaths::Combine(TEXT("/Game/"), FileName), TEXT(".SchemaDatabase"));
		const USchemaDatabase* const SchemaDatabase = Cast<USchemaDatabase>(FSoftObjectPath(DatabaseAssetPath).TryLoad());

		if (SchemaDatabase == nullptr)
		{
			// clang-format off
			UE_LOG(LogSpatialGDKSchemaGenerator, Error, TEXT("Schema generation failed: Failed to load existing schema database. If this continues, delete the schema database and try again."));
			// clang-format on
			return false;
		}

		ActorClassPathToSchema = SchemaDatabase->ActorClassPathToSchema;
		SubobjectClassPathToSchema = SchemaDatabase->SubobjectClassPathToSchema;
		LevelPathToComponentId = SchemaDatabase->LevelPathToComponentId;
		NextAvailableComponentId = SchemaDatabase->NextAvailableComponentId;
		NetCullDistanceToComponentId = SchemaDatabase->NetCullDistanceToComponentId;

		// Component Id generation was updated to be non-destructive, if we detect an old schema database, delete it.
		if (ActorClassPathToSchema.Num() > 0 && NextAvailableComponentId == SpatialConstants::STARTING_GENERATED_COMPONENT_ID)
		{
			return false;
		}
	}
	else
	{
		return false;
	}

	return true;
}

bool IsAssetReadOnly(const FString& FileName)
{
	FString RelativeFileName = FPaths::Combine(FPaths::ProjectContentDir(), FileName);
	RelativeFileName = FPaths::SetExtension(RelativeFileName, FPackageName::GetAssetPackageExtension());

	FFileStatData StatData = FPlatformFileManager::Get().GetPlatformFile().GetStatData(*RelativeFileName);

	if (StatData.bIsValid && StatData.bIsReadOnly)
	{
		return true;
	}

	return false;
}

bool GeneratedSchemaFolderExists()
{
	const FString SchemaOutputPath = GetDefault<USpatialGDKEditorSettings>()->GetGeneratedSchemaOutputFolder();
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	return PlatformFile.DirectoryExists(*SchemaOutputPath);
}

bool DeleteSchemaDatabase(const FString& PackagePath)
{
	FString DatabaseAssetPath = "";

	DatabaseAssetPath =
		FPaths::SetExtension(FPaths::Combine(FPaths::ProjectContentDir(), PackagePath), FPackageName::GetAssetPackageExtension());
	FFileStatData StatData = FPlatformFileManager::Get().GetPlatformFile().GetStatData(*DatabaseAssetPath);

	if (StatData.bIsValid)
	{
		if (IsAssetReadOnly(PackagePath))
		{
			// clang-format off
			UE_LOG(LogSpatialGDKSchemaGenerator, Error, TEXT("Unable to delete schema database at %s because it is read-only."), *DatabaseAssetPath);
			// clang-format on
			return false;
		}

		if (!FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*DatabaseAssetPath))
		{
			// This should never run, since DeleteFile should only return false if the file does not exist which we have already checked
			// for.
			// clang-format off
			UE_LOG(LogSpatialGDKSchemaGenerator, Error, TEXT("Unable to delete schema database at %s"), *DatabaseAssetPath);
			// clang-format on
			return false;
		}
	}

	return true;
}

bool GeneratedSchemaDatabaseExists()
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	return PlatformFile.FileExists(*GetRelativeSchemaDatabaseFilePath());
}

FSpatialGDKEditor::ESchemaDatabaseValidationResult ValidateSchemaDatabase()
{
	FFileStatData StatData = FPlatformFileManager::Get().GetPlatformFile().GetStatData(*GetRelativeSchemaDatabaseFilePath());
	if (!StatData.bIsValid)
	{
		return FSpatialGDKEditor::NotFound;
	}

	const FString DatabaseAssetPath = FPaths::SetExtension(SpatialConstants::SCHEMA_DATABASE_ASSET_PATH, TEXT(".SchemaDatabase"));
	const USchemaDatabase* const SchemaDatabase = Cast<USchemaDatabase>(FSoftObjectPath(DatabaseAssetPath).TryLoad());

	if (SchemaDatabase == nullptr)
	{
		return FSpatialGDKEditor::NotFound;
	}

	if (SchemaDatabase->SchemaDatabaseVersion < ESchemaDatabaseVersion::LatestVersion)
	{
		return FSpatialGDKEditor::OldVersion;
	}

	// Check ring buffer sizes
	for (uint8 RPCType = static_cast<uint8>(ERPCType::RingBufferTypeBegin); RPCType <= static_cast<uint8>(ERPCType::RingBufferTypeEnd);
		 RPCType++)
	{
		if (SchemaDatabase->RPCRingBufferSizeMap.FindRef(static_cast<ERPCType>(RPCType))
			!= GetDefault<USpatialGDKSettings>()->GetRPCRingBufferSize(static_cast<ERPCType>(RPCType)))
		{
			return FSpatialGDKEditor::RingBufferSizeChanged;
		}
	}

	return FSpatialGDKEditor::Ok;
}

void ResolveClassPathToSchemaName(const FString& ClassPath, const FString& SchemaName)
{
	if (SchemaName.IsEmpty())
	{
		return;
	}

	ClassPathToSchemaName.Add(ClassPath, SchemaName);
	SchemaNameToClassPath.Add(SchemaName, ClassPath);
	FSoftObjectPath ObjPath = FSoftObjectPath(ClassPath);
	FString DesiredSchemaName = UnrealNameToSchemaName(ObjPath.GetAssetName());

	if (DesiredSchemaName != SchemaName)
	{
		AddPotentialNameCollision(DesiredSchemaName, ClassPath, SchemaName);
	}
	AddPotentialNameCollision(SchemaName, ClassPath, SchemaName);
}

void ResetUsedNames()
{
	ClassPathToSchemaName.Empty();
	SchemaNameToClassPath.Empty();
	PotentialSchemaNameCollisions.Empty();

	for (const TPair<FString, FActorSchemaData>& Entry : ActorClassPathToSchema)
	{
		ResolveClassPathToSchemaName(Entry.Key, Entry.Value.GeneratedSchemaName);
	}

	for (const TPair<FString, FSubobjectSchemaData>& Entry : SubobjectClassPathToSchema)
	{
		ResolveClassPathToSchemaName(Entry.Key, Entry.Value.GeneratedSchemaName);
	}
}

bool RunSchemaCompiler(FString& SchemaBundleJsonOutput, FString SchemaInputDir, FString BuildDir)
{
	FString PluginDir = FSpatialGDKServicesModule::GetSpatialGDKPluginDirectory();

	// Get the schema_compiler path and arguments
	FString SchemaCompilerExe = FPaths::Combine(PluginDir, TEXT("Binaries/ThirdParty/Improbable/Programs/schema_compiler.exe"));

	if (SchemaInputDir == "")
	{
		SchemaInputDir = FPaths::Combine(SpatialGDKServicesConstants::SpatialOSDirectory, TEXT("schema/"));
	}

	if (BuildDir == "")
	{
		BuildDir = FPaths::Combine(SpatialGDKServicesConstants::SpatialOSDirectory, TEXT("build"));
	}
	FString CompiledSchemaDir = FPaths::Combine(BuildDir, TEXT("assembly/schema"));
	FString CoreSDKSchemaDir = FPaths::Combine(BuildDir, TEXT("dependencies/schema/standard_library"));

	FString CompiledSchemaASTDir = FPaths::Combine(CompiledSchemaDir, TEXT("ast"));
	FString SchemaBundleOutput = FPaths::Combine(CompiledSchemaDir, TEXT("schema.sb"));
	SchemaBundleJsonOutput = FPaths::Combine(CompiledSchemaDir, TEXT("schema.json"));

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	// clang-format off
	const FString& SchemaCompilerBaseArgs = FString::Printf(TEXT("--schema_path=\"%s\" --core_path=\"%s\" --bundle_out=\"%s\" --bundle_json_out=\"%s\" --load_all_schema_on_schema_path "), *SchemaInputDir, *CoreSDKSchemaDir, *SchemaBundleOutput, *SchemaBundleJsonOutput);
	// clang-format on

	// If there's already a compiled schema dir, blow it away so we don't have lingering artifacts from previous generation runs.
	if (FPaths::DirectoryExists(CompiledSchemaDir))
	{
		if (!PlatformFile.DeleteDirectoryRecursively(*CompiledSchemaDir))
		{
			// clang-format off
			UE_LOG(LogSpatialGDKSchemaGenerator, Error, TEXT("Could not delete pre-existing compiled schema directory '%s'! Please make sure the directory is writeable."), *CompiledSchemaDir);
			// clang-format on
			return false;
		}
	}

	// schema_compiler cannot create folders, so we need to set them up beforehand.
	if (!PlatformFile.CreateDirectoryTree(*CompiledSchemaDir))
	{
		// clang-format off
		UE_LOG(LogSpatialGDKSchemaGenerator, Error, TEXT("Could not create compiled schema directory '%s'! Please make sure the parent directory is writeable."), *CompiledSchemaDir);
		// clang-format on
		return false;
	}

	FString AdditionalSchemaCompilerArgs;

	TArray<FString> Tokens;
	TArray<FString> Switches;
	FCommandLine::Parse(FCommandLine::Get(), Tokens, Switches);

	if (const FString* SchemaCompileArgsCLSwitchPtr = Switches.FindByPredicate([](const FString& ClSwitch) {
			return ClSwitch.StartsWith(FString{ TEXT("AdditionalSchemaCompilerArgs") });
		}))
	{
		FString SwitchName;
		SchemaCompileArgsCLSwitchPtr->Split(FString{ TEXT("=") }, &SwitchName, &AdditionalSchemaCompilerArgs);
		if (AdditionalSchemaCompilerArgs.Contains(FString{ TEXT("ast_proto_out") })
			|| AdditionalSchemaCompilerArgs.Contains(FString{ TEXT("ast_json_out") }))
		{
			if (!PlatformFile.CreateDirectoryTree(*CompiledSchemaASTDir))
			{
				// clang-format off
				UE_LOG(LogSpatialGDKSchemaGenerator, Error, TEXT("Could not create compiled schema AST directory '%s'! Please make sure the parent directory is writeable."), *CompiledSchemaASTDir);
				// clang-format on
				return false;
			}
		}
	}

	FString SchemaCompilerArgs = FString::Printf(TEXT("%s %s"), *SchemaCompilerBaseArgs, *AdditionalSchemaCompilerArgs.TrimQuotes());

	// clang-format off
	UE_LOG(LogSpatialGDKSchemaGenerator, Log, TEXT("Starting '%s' with `%s` arguments."), *SpatialGDKServicesConstants::SchemaCompilerExe, *SchemaCompilerArgs);
	// clang-format on

	int32 ExitCode = 1;
	FString SchemaCompilerOut;
	FString SchemaCompilerErr;
	FPlatformProcess::ExecProcess(*SpatialGDKServicesConstants::SchemaCompilerExe, *SchemaCompilerArgs, &ExitCode, &SchemaCompilerOut,
								  &SchemaCompilerErr);

	if (ExitCode == 0)
	{
		// clang-format off
		UE_LOG(LogSpatialGDKSchemaGenerator, Log, TEXT("schema_compiler successfully generated compiled schema with arguments `%s`: %s"), *SchemaCompilerArgs, *SchemaCompilerOut);
		// clang-format on
		return true;
	}
	else
	{
		// clang-format off
		UE_LOG(LogSpatialGDKSchemaGenerator, Error, TEXT("schema_compiler failed to generate compiled schema for arguments `%s`: %s"), *SchemaCompilerArgs, *SchemaCompilerErr);
		// clang-format on
		return false;
	}
}

bool ExtractInformationFromSchemaJson(const FString& SchemaJsonPath, TMap<uint32, FComponentIDs>& OutComponentSetMap,
									  TMap<uint32, uint32>& OutComponentIdToFieldIdsIndex, TArray<FFieldIDs>& OutFieldIdsArray)
{
	TUniquePtr<FArchive> SchemaFile(IFileManager::Get().CreateFileReader(*SchemaJsonPath));
	if (!SchemaFile)
	{
		UE_LOG(LogSpatialGDKSchemaGenerator, Error, TEXT("Could not open schema bundle file %s"), *SchemaJsonPath);
		return false;
	}

	TSharedPtr<FJsonValue> SchemaBundleJson;
	{
		TSharedRef<TJsonReader<char>> JsonReader = TJsonReader<char>::Create(SchemaFile.Get());
		FJsonSerializer::Deserialize(*JsonReader, SchemaBundleJson);
	}

	const TSharedPtr<FJsonObject>* RootObject;
	if (!SchemaBundleJson || !SchemaBundleJson->TryGetObject(RootObject))
	{
		UE_LOG(LogSpatialGDKSchemaGenerator, Error, TEXT("%s is not a valid Json file"), *SchemaJsonPath);
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* SchemaFiles;
	SAFE_TRYGETFIELD((*RootObject), Array, "schemaFiles", SchemaFiles);

	TMap<FString, uint32> ComponentMap;
	TMap<uint32, TSet<FString>> ComponentRefSetMap;

	TMap<FString, uint32> DataDefinitionNameToFieldIdsIndex;
	TMap<uint32, FString> ComponentIdToDataDefinitionName;

	for (const auto& FileValue : *SchemaFiles)
	{
		const TSharedPtr<FJsonObject>* FileObject;
		SAFE_TRYGET(FileValue, Object, FileObject);

		const TArray<TSharedPtr<FJsonValue>>* TypesDecl;
		SAFE_TRYGETFIELD((*FileObject), Array, "types", TypesDecl);

		for (const auto& TypeValue : *TypesDecl)
		{
			const TSharedPtr<FJsonObject>* TypeObject;
			SAFE_TRYGET(TypeValue, Object, TypeObject);

			FString ComponentName;
			SAFE_TRYGETFIELD((*TypeObject), String, "qualifiedName", ComponentName);

			COND_SCHEMA_GEN_ERROR_AND_RETURN(DataDefinitionNameToFieldIdsIndex.Contains(ComponentName),
											 TEXT("The schema bundle contains duplicate data definitions for %s."), *ComponentName);
			DataDefinitionNameToFieldIdsIndex.Add(ComponentName, OutFieldIdsArray.Num());
			TArray<uint32>& FieldIDs = OutFieldIdsArray.AddDefaulted_GetRef().FieldIds;

			const TArray<TSharedPtr<FJsonValue>>* FieldArray;
			SAFE_TRYGETFIELD((*TypeObject), Array, "fields", FieldArray);

			for (const auto& ArrayValue : *FieldArray)
			{
				const TSharedPtr<FJsonObject>* ArrayObject;
				SAFE_TRYGET(ArrayValue, Object, ArrayObject);

				int32 FieldId;
				SAFE_TRYGETFIELD((*ArrayObject), Number, "fieldId", FieldId);

				COND_SCHEMA_GEN_ERROR_AND_RETURN(FieldIDs.Contains(FieldId),
												 TEXT("The schema bundle contains duplicate fieldId: %d, component name: %s."), FieldId,
												 *ComponentName);
				FieldIDs.Add(FieldId);
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* ComponentsDecl;
		SAFE_TRYGETFIELD((*FileObject), Array, "components", ComponentsDecl);

		for (const auto& CompValue : *ComponentsDecl)
		{
			const TSharedPtr<FJsonObject>* CompObject;
			SAFE_TRYGET(CompValue, Object, CompObject);

			FString ComponentName;
			SAFE_TRYGETFIELD((*CompObject), String, "qualifiedName", ComponentName);

			int32 ComponentId;
			SAFE_TRYGETFIELD((*CompObject), Number, "componentId", ComponentId);

			ComponentMap.Add(ComponentName, ComponentId);

			const TArray<TSharedPtr<FJsonValue>>* FieldArray;
			SAFE_TRYGETFIELD((*CompObject), Array, "fields", FieldArray);

			if (FieldArray->Num() > 0)
			{
				COND_SCHEMA_GEN_ERROR_AND_RETURN(OutComponentIdToFieldIdsIndex.Contains(ComponentId),
												 TEXT("The schema bundle contains duplicate component IDs with component %s."),
												 *ComponentName);
				OutComponentIdToFieldIdsIndex.Add(ComponentId, OutFieldIdsArray.Num());
				TArray<uint32>& FieldIDs = OutFieldIdsArray.AddDefaulted_GetRef().FieldIds;

				for (const auto& ArrayValue : *FieldArray)
				{
					const TSharedPtr<FJsonObject>* ArrayObject;
					SAFE_TRYGET(ArrayValue, Object, ArrayObject);

					int32 FieldId;
					SAFE_TRYGETFIELD((*ArrayObject), Number, "fieldId", FieldId);

					COND_SCHEMA_GEN_ERROR_AND_RETURN(FieldIDs.Contains(FieldId),
													 TEXT("The schema bundle contains duplicate fieldId: %d, component name: %s."), FieldId,
													 *ComponentName);
					FieldIDs.Add(FieldId);
				}
			}

			FString DataDefinition;
			SAFE_TRYGETFIELD((*CompObject), String, "dataDefinition", DataDefinition);

			if (!DataDefinition.IsEmpty())
			{
				COND_SCHEMA_GEN_ERROR_AND_RETURN(
					FieldArray->Num() != 0,
					TEXT("The schema bundle supplied both a data definition and field IDs - this is unexpected, component name: %s."),
					*ComponentName);
				ComponentIdToDataDefinitionName.Add(ComponentId, DataDefinition);
			}
		}
		// TODO:IMPROBABLE-BEGIN ???±??±?compontsets
		/*
		const TArray<TSharedPtr<FJsonValue>>* ComponentSets;
		SAFE_TRYGETFIELD((*FileObject), Array, "componentSets", ComponentSets);

		for (const auto& CompSetValue : *ComponentSets)
		{
			const TSharedPtr<FJsonObject>* CompSetObject;
			SAFE_TRYGET(CompSetValue, Object, CompSetObject);

			int32 ComponentSetId;
			SAFE_TRYGETFIELD((*CompSetObject), Number, "componentSetId", ComponentSetId);

			const TSharedPtr<FJsonObject>* CompListObject;
			SAFE_TRYGETFIELD((*CompSetObject), Object, "componentList", CompListObject);

			const TArray<TSharedPtr<FJsonValue>>* RefComponents;
			SAFE_TRYGETFIELD((*CompListObject), Array, "components", RefComponents);

			TSet<FString> Components;

			for (const auto& CompRefValue : *RefComponents)
			{
				const TSharedPtr<FJsonObject>* CompRefObject;
				SAFE_TRYGET(CompRefValue, Object, CompRefObject);

				FString ComponentName;
				SAFE_TRYGETFIELD((*CompRefObject), String, "component", ComponentName);

				Components.Add(ComponentName);
			}

			ComponentRefSetMap.Add(ComponentSetId, MoveTemp(Components));
		}*/
		// IMPROBABLE-END
	}

	TMap<uint32, FComponentIDs> FinalMap;

	for (const auto& SetEntry : ComponentRefSetMap)
	{
		const TSet<FString>& ComponentRefs = SetEntry.Value;

		FComponentIDs SetIds;
		for (const auto& CompRef : ComponentRefs)
		{
			uint32* FoundId = ComponentMap.Find(CompRef);
			COND_SCHEMA_GEN_ERROR_AND_RETURN(FoundId == nullptr, TEXT("Schema file %s is missing a component entry for %s"),
											 *SchemaJsonPath, *CompRef);
			SetIds.ComponentIDs.Add(*FoundId);
		}

		FinalMap.Add(SetEntry.Key, MoveTemp(SetIds));
	}

	for (const auto& Pair : ComponentIdToDataDefinitionName)
	{
		COND_SCHEMA_GEN_ERROR_AND_RETURN(
			!DataDefinitionNameToFieldIdsIndex.Contains(Pair.Value),
			TEXT("The schema bundle did not contain a data definition for component ID %d, data definition name: %s."), Pair.Key,
			*Pair.Value);
		OutComponentIdToFieldIdsIndex.Add(Pair.Key, DataDefinitionNameToFieldIdsIndex[Pair.Value]);
	}

	OutComponentSetMap = MoveTemp(FinalMap);

	return true;
}

bool SpatialGDKGenerateSchema()
{
	SchemaGeneratedClasses.Empty();

	// Generate Schema for classes loaded in memory.

	TArray<UObject*> AllClasses;
	GetObjectsOfClass(UClass::StaticClass(), AllClasses);
	if (!SpatialGDKGenerateSchemaForClasses(GetAllSupportedClasses(AllClasses)))
	{
		return false;
	}
	SpatialGDKSanitizeGeneratedSchema();

	GenerateSchemaForSublevels();
	GenerateSchemaForRPCEndpoints();
	GenerateSchemaForNCDs();

	USchemaDatabase* SchemaDatabase = InitialiseSchemaDatabase(SpatialConstants::SCHEMA_DATABASE_ASSET_PATH);

	// Needs to happen before RunSchemaCompiler
	WriteComponentSetFiles(SchemaDatabase);

	//编译生成的proto


	FString SchemaJsonOutput;
	if (!RunSchemaCompiler(SchemaJsonOutput))
	{
		return false;
	}

	/*if (!ExtractInformationFromSchemaJson(SchemaJsonOutput, SchemaDatabase->ComponentSetIdToComponentIds,
										  SchemaDatabase->ComponentIdToFieldIdsIndex, SchemaDatabase->FieldIdsArray))
	{
		return false;
	}

	if (!build_schema("",""))
	{
		return false;
	}*/

	if (!SaveSchemaDatabase(SchemaDatabase)) // This requires RunSchemaCompiler to run first
	{
		return false;
	}

	return true;
}

bool SpatialGDKGenerateSchemaForClasses(TSet<UClass*> Classes, FString SchemaOutputPath /*= ""*/)
{
	ResetUsedNames();
	Classes.Sort([](const UClass& A, const UClass& B) {
		return A.GetPathName() < B.GetPathName();
	});

	// Generate Type Info structs for all classes
	TArray<TSharedPtr<FUnrealType>> TypeInfos;

	for (const auto& Class : Classes)
	{
		if (SchemaGeneratedClasses.Contains(Class))
		{
			continue;
		}

		SchemaGeneratedClasses.Add(Class);
		// Parent and static array index start at 0 for checksum calculations.
		TSharedPtr<FUnrealType> TypeInfo = CreateUnrealTypeInfo(Class, 0, 0);
		TypeInfos.Add(TypeInfo);
		VisitAllObjects(TypeInfo, [&](TSharedPtr<FUnrealType> TypeNode) {
			if (UClass* NestedClass = Cast<UClass>(TypeNode->Type))
			{
				if (!SchemaGeneratedClasses.Contains(NestedClass) && IsSupportedClass(NestedClass))
				{
					TypeInfos.Add(CreateUnrealTypeInfo(NestedClass, 0, 0));
					SchemaGeneratedClasses.Add(NestedClass);
				}
			}
			return true;
		});
	}

	if (!ValidateIdentifierNames(TypeInfos))
	{
		return false;
	}

	if (!ValidateAlwaysWriteRPCs(TypeInfos))
	{
		return false;
	}

	if (SchemaOutputPath.IsEmpty())
	{
		SchemaOutputPath = GetDefault<USpatialGDKEditorSettings>()->GetGeneratedSchemaOutputFolder();
	}

	UE_LOG(LogSpatialGDKSchemaGenerator, Display, TEXT("Schema path %s"), *SchemaOutputPath);

	// Check schema path is valid.
	if (!FPaths::CollapseRelativeDirectories(SchemaOutputPath))
	{
		UE_LOG(LogSpatialGDKSchemaGenerator, Error, TEXT("Invalid path: '%s'. Schema not generated."), *SchemaOutputPath);
		return false;
	}

	FComponentIdGenerator IdGenerator = FComponentIdGenerator(NextAvailableComponentId);

	GenerateSchemaFromClasses(TypeInfos, SchemaOutputPath, IdGenerator);

	NextAvailableComponentId = IdGenerator.Peek();

	return true;
}

template <class T>
void SanitizeClassMap(TMap<FString, T>& Map, const TSet<FName>& ValidClassNames)
{
	for (auto Item = Map.CreateIterator(); Item; ++Item)
	{
		FString SanitizeName = Item->Key;
		SanitizeName.RemoveFromEnd(TEXT("_C"));
		if (!ValidClassNames.Contains(FName(*SanitizeName)))
		{
			UE_LOG(LogSpatialGDKSchemaGenerator, Log, TEXT("Found stale class (%s), removing from schema database."), *Item->Key);
			Item.RemoveCurrent();
		}
	}
}

void SpatialGDKSanitizeGeneratedSchema()
{
	// Sanitize schema database, removing assets that no longer exist
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAllAssets(Assets, false);
	TSet<FName> ValidClassNames;
	for (const auto& Asset : Assets)
	{
		FAssetDataTagMapSharedView::FFindTagResult GeneratedClassPathResult = Asset.TagsAndValues.FindTag(TEXT("GeneratedClass"));
		if (GeneratedClassPathResult.IsSet())
		{
			FString SanitizedClassPath = FPackageName::ExportTextPathToObjectPath(GeneratedClassPathResult.GetValue());
			SanitizedClassPath.RemoveFromEnd(TEXT("_C"));
			ValidClassNames.Add(FName(*SanitizedClassPath));
		}
	}

	TArray<UObject*> AllClasses;
	GetObjectsOfClass(UClass::StaticClass(), AllClasses);
	for (const auto& SupportedClass : GetAllSupportedClasses(AllClasses))
	{
		ValidClassNames.Add(FName(*SupportedClass->GetPathName()));
	}

	SanitizeClassMap(ActorClassPathToSchema, ValidClassNames);
	SanitizeClassMap(SubobjectClassPathToSchema, ValidClassNames);
}

} // namespace Schema
} // namespace SpatialGDKEditor

#undef LOCTEXT_NAMESPACE
