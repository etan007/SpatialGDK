// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "Tests/TestDefinitions.h"

#include "SchemaGenObjectStub.h"
#include "SpatialConstants.h"
#include "SpatialGDKEditorSchemaGenerator.h"
#include "SpatialGDKServicesConstants.h"
#include "SpatialGDKServicesModule.h"
#include "SpatialGDKSettings.h"
#include "Utils/SchemaDatabase.h"

#include "CoreMinimal.h"
#include "GeneralProjectSettings.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"

#define LOCTEXT_NAMESPACE "SpatialGDKEDitorSchemaGeneratorTest"

#define SCHEMA_GENERATOR_TEST(TestName) GDK_TEST(SpatialGDKEditor, SchemaGenerator, TestName)

namespace
{
const FString SchemaOutputFolder = FPaths::Combine(SpatialGDKServicesConstants::SpatialOSDirectory, TEXT("Tests/"));
const FString SchemaDatabaseFileName = TEXT("Spatial/Tests/SchemaDatabase");
const FString DatabaseOutputFile = TEXT("/Game/Spatial/Tests/SchemaDatabase");

DECLARE_LOG_CATEGORY_EXTERN(LogSpatialGDKSchemaGeneratorTest, Log, All);
DEFINE_LOG_CATEGORY(LogSpatialGDKSchemaGeneratorTest);

TArray<FString> LoadSchemaFileForClassToStringArray(const FString& InSchemaOutputFolder, const UClass* CurrentClass)
{
	FString SchemaFileFolder = TEXT("");

	if (!CurrentClass->IsChildOf<AActor>())
	{
		SchemaFileFolder = TEXT("Subobjects");
	}

	TArray<FString> FileContent;
	FFileHelper::LoadFileToStringArray(
		FileContent,
		*FPaths::SetExtension(FPaths::Combine(InSchemaOutputFolder, SchemaFileFolder, CurrentClass->GetName()), TEXT(".proto")));

	return FileContent;
}

struct ComponentNamesAndIds
{
	TArray<FString> Names;
	TArray<FString> SubobjectNames;
	TArray<int32> Ids;
};

ComponentNamesAndIds ParseAvailableNamesAndIdsFromSchemaFile(const TArray<FString>& LoadedSchema)
{
	ComponentNamesAndIds ParsedNamesAndIds;

	for (const auto& SchemaLine : LoadedSchema)
	{
		FRegexPattern IdPattern(TEXT("\\s+(id\\s*=\\s*)([0-9]+)(\\s*;)"));
		FRegexMatcher IdRegMatcher(IdPattern, SchemaLine);

		FRegexPattern NamePattern(TEXT("(^component )(.+)( \\{)"));
		FRegexMatcher NameRegMatcher(NamePattern, SchemaLine);

		FRegexPattern SubobjectNamePattern(TEXT("(\tUnrealObjectRef )(.+)( = )([0-9]+)(;)"));
		FRegexMatcher SubobjectNameRegMatcher(SubobjectNamePattern, SchemaLine);

		if (IdRegMatcher.FindNext())
		{
			FString ParsedId = IdRegMatcher.GetCaptureGroup(2);

			if (ParsedId.IsNumeric())
			{
				const int32 ComponentId = FCString::Atoi(*ParsedId);
				// Component sets are now picked up by this regex (as they have the same id). Ignore any ids we've already seen.
				if (ParsedNamesAndIds.Ids.Find(ComponentId) == INDEX_NONE)
				{
					ParsedNamesAndIds.Ids.Push(ComponentId);
				}
			}
		}
		else if (NameRegMatcher.FindNext())
		{
			FString ComponentName = NameRegMatcher.GetCaptureGroup(2);
			ParsedNamesAndIds.Names.Push(ComponentName);
		}
		else if (SubobjectNameRegMatcher.FindNext())
		{
			FString ParsedSubobjectName = SubobjectNameRegMatcher.GetCaptureGroup(2);

			if (!ParsedSubobjectName.IsEmpty() && ParsedSubobjectName.Compare(TEXT("attachmentreplication_attachparent")) != 0
				&& ParsedSubobjectName.Compare(TEXT("attachmentreplication_attachcomponent")) != 0
				&& ParsedSubobjectName.Compare(TEXT("owner")) != 0 && ParsedSubobjectName.Compare(TEXT("instigator")) != 0)
			{
				ParsedNamesAndIds.SubobjectNames.Push(ParsedSubobjectName);
			}
		}
	}

	return ParsedNamesAndIds;
}

FString ComponentTypeToString(ESchemaComponentType Type)
{
	static_assert(SCHEMA_Count == 4, "Unexpected number of Schema type components, please check ComponentTypeToString is still correct.");

	switch (Type)
	{
	case SCHEMA_Data:
		return TEXT("");
	case SCHEMA_OwnerOnly:
		return TEXT("OwnerOnly");
	case SCHEMA_ServerOnly:
		return TEXT("ServerOnly");
	case SCHEMA_InitialOnly:
		return TEXT("InitialOnly");
	default:
		return TEXT("");
	}
}

bool TestEqualDatabaseEntryAndSchemaFile(const UClass* CurrentClass, const FString& InSchemaOutputFolder,
										 const USchemaDatabase* SchemaDatabase)
{
	const TArray<FString> LoadedSchema = LoadSchemaFileForClassToStringArray(InSchemaOutputFolder, CurrentClass);
	ComponentNamesAndIds ParsedNamesAndIds = ParseAvailableNamesAndIdsFromSchemaFile(LoadedSchema);

	if (CurrentClass->IsChildOf<AActor>())
	{
		const FActorSchemaData* ActorData = SchemaDatabase->ActorClassPathToSchema.Find(CurrentClass->GetPathName());
		if (ActorData == nullptr)
		{
			return false;
		}
		else
		{
			if (ActorData->GeneratedSchemaName.Compare(ParsedNamesAndIds.Names[0]) != 0)
			{
				return false;
			}

			TArray<FActorSpecificSubobjectSchemaData> SubobjectNames;
			ActorData->SubobjectData.GenerateValueArray(SubobjectNames);
			if (SubobjectNames.Num() != ParsedNamesAndIds.SubobjectNames.Num())
			{
				return false;
			}

			// TODO: UNR-2298 - Uncomment and fix it
			// for (int i = 0; i < ParsedNamesAndIds.SubobjectNames.Num(); ++i)
			//{
			//	const auto& Predicate = [&SchemaName = ParsedNamesAndIds.SubobjectNames[i]](const FActorSpecificSubobjectSchemaData& Data)
			//	{
			//		return (Data.Name.ToString().Compare(SchemaName) == 0);
			//	};

			//	if (!SubobjectNames.ContainsByPredicate(Predicate))
			//	{
			//		return false;
			//	}
			//}

			uint32 IdIndex = 0;
			for (int i = 0; i < SCHEMA_Count; ++i)
			{
				if (ActorData->SchemaComponents[i] == SpatialConstants::INVALID_COMPONENT_ID)
				{
					continue;
				}
				if (ActorData->SchemaComponents[i] != ParsedNamesAndIds.Ids[IdIndex++])
				{
					return false;
				}
			}
		}
	}
	else
	{
		const FSubobjectSchemaData* SubobjectSchemaData = SchemaDatabase->SubobjectClassPathToSchema.Find(CurrentClass->GetPathName());
		if (SubobjectSchemaData == nullptr)
		{
			UE_LOG(LogSpatialGDKSchemaGeneratorTest, Error, TEXT("SubobjectSchemaData is null"));
			return false;
		}
		else
		{
			if (ParsedNamesAndIds.Names.Num() != ParsedNamesAndIds.Ids.Num())
			{
				UE_LOG(LogSpatialGDKSchemaGeneratorTest, Error,
					   TEXT("ParsedNamesAndIds.Names.Num() is not equal with ParsedNamesAndIds.Ids.Num()"));
				return false;
			}

			TArray<int32> SavedIds;
			TMap<int32, TPair<int, ESchemaComponentType> > SavedIdType;
			const uint32 DynamicComponentsPerClass = GetDefault<USpatialGDKSettings>()->MaxDynamicallyAttachedSubobjectsPerClass;
			for (uint32 i = 0; i < DynamicComponentsPerClass; ++i)
			{
				for (int j = SCHEMA_Begin; j < SCHEMA_Count; ++j)
				{
					int32 Id = SubobjectSchemaData->DynamicSubobjectComponents[i].SchemaComponents[j];
					if (Id != 0)
					{
						SavedIds.Push(Id);
						SavedIdType.Emplace(Id, TPair<int, ESchemaComponentType>(i, (ESchemaComponentType)j));
					}
				}
			}
			if (SavedIds.Num() != ParsedNamesAndIds.Ids.Num())
			{
				UE_LOG(LogSpatialGDKSchemaGeneratorTest, Error, TEXT("SavedIds.Num() is not equal with ParsedNamesAndIds.Ids.Num()"));
				return false;
			}

			for (int i = 0; i < ParsedNamesAndIds.Ids.Num(); ++i)
			{
				if (SavedIds[i] != ParsedNamesAndIds.Ids[i])
				{
					UE_LOG(LogSpatialGDKSchemaGeneratorTest, Error, TEXT("%d Saved Id %d != Loaded Id %d"), i, SavedIds[i],
						   ParsedNamesAndIds.Ids[i]);
					return false;
				}

				const TPair<int, ESchemaComponentType>& IdType = SavedIdType[SavedIds[i]];
				FString ExpectedComponentName = SubobjectSchemaData->GeneratedSchemaName;
				ExpectedComponentName += ComponentTypeToString(IdType.Value);
				ExpectedComponentName += TEXT("Dynamic");
				ExpectedComponentName.AppendInt(IdType.Key + 1);
				if (ParsedNamesAndIds.Names[i].Compare(ExpectedComponentName) != 0)
				{
					UE_LOG(LogSpatialGDKSchemaGeneratorTest, Error, TEXT("Expected component name %s not matched %s"),
						   *ExpectedComponentName, *ParsedNamesAndIds.Names[i]);
					return false;
				}
			}
		}
	}

	return true;
}

FString LoadSchemaFileForClass(const FString& InSchemaOutputFolder, const UClass* CurrentClass)
{
	FString SchemaFileFolder = TEXT("");

	if (!CurrentClass->IsChildOf<AActor>())
	{
		SchemaFileFolder = TEXT("Subobjects");
	}

	FString FileContent;
	FFileHelper::LoadFileToString(
		FileContent,
		*FPaths::SetExtension(FPaths::Combine(InSchemaOutputFolder, SchemaFileFolder, CurrentClass->GetName()), TEXT(".proto")));

	return FileContent;
}

const TArray<UObject*>& AllTestClassesArray()
{
	static TArray<UObject*> TestClassesArray = { USchemaGenObjectStub::StaticClass(),
												 USchemaGenObjectStubCondOwnerOnly::StaticClass(),
												 USchemaGenObjectStubHandOver::StaticClass(),
												 USchemaGenObjectStubInitialOnly::StaticClass(),
												 USpatialTypeObjectStub::StaticClass(),
												 UChildOfSpatialTypeObjectStub::StaticClass(),
												 UNotSpatialTypeObjectStub::StaticClass(),
												 UChildOfNotSpatialTypeObjectStub::StaticClass(),
												 UNoSpatialFlagsObjectStub::StaticClass(),
												 UChildOfNoSpatialFlagsObjectStub::StaticClass(),
												 ASpatialTypeActor::StaticClass(),
												 ANonSpatialTypeActor::StaticClass(),
												 USpatialTypeActorComponent::StaticClass(),
												 ASpatialTypeActorWithActorComponent::StaticClass(),
												 ASpatialTypeActorWithMultipleActorComponents::StaticClass(),
												 ASpatialTypeActorWithMultipleObjectComponents::StaticClass(),
												 ASpatialTypeActorWithSubobject::StaticClass() };
	return TestClassesArray;
};

const TSet<UClass*>& AllTestClassesSet()
{
	static TSet<UClass*> TestClassesSet = { USchemaGenObjectStub::StaticClass(),
											USchemaGenObjectStubCondOwnerOnly::StaticClass(),
											USchemaGenObjectStubHandOver::StaticClass(),
											USchemaGenObjectStubInitialOnly::StaticClass(),
											USpatialTypeObjectStub::StaticClass(),
											UChildOfSpatialTypeObjectStub::StaticClass(),
											UNotSpatialTypeObjectStub::StaticClass(),
											UChildOfNotSpatialTypeObjectStub::StaticClass(),
											UNoSpatialFlagsObjectStub::StaticClass(),
											UChildOfNoSpatialFlagsObjectStub::StaticClass(),
											ASpatialTypeActor::StaticClass(),
											ANonSpatialTypeActor::StaticClass(),
											USpatialTypeActorComponent::StaticClass(),
											ASpatialTypeActorWithActorComponent::StaticClass(),
											ASpatialTypeActorWithMultipleActorComponents::StaticClass(),
											ASpatialTypeActorWithMultipleObjectComponents::StaticClass(),
											ASpatialTypeActorWithSubobject::StaticClass() };
	return TestClassesSet;
};

 
FString ExpectedContentsDirectory =
	TEXT("SpatialGDK/Source/SpatialGDKTests/SpatialGDKEditor/SpatialGDKEditorSchemaGenerator/ExpectedSchema_425");
 
TMap<FString, FString> ExpectedContentsFilenames = {
	{ "SpatialTypeActor", "SpatialTypeActor.proto" },
	{ "NonSpatialTypeActor", "NonSpatialTypeActor.proto" },
	{ "SpatialTypeActorComponent", "SpatialTypeActorComponent.proto" },
	{ "SpatialTypeActorWithActorComponent", "SpatialTypeActorWithActorComponent.proto" },
	{ "SpatialTypeActorWithMultipleActorComponents", "SpatialTypeActorWithMultipleActorComponents.proto" },
	{ "SpatialTypeActorWithMultipleObjectComponents", "SpatialTypeActorWithMultipleObjectComponents.proto" }
};
uint32 ExpectedRPCEndpointsRingBufferSize = 32;
TMap<ERPCType, uint32> ExpectedRPCRingBufferSizeOverrides = { { ERPCType::ServerAlwaysWrite, 1 } };
FString ExpectedRPCEndpointsSchemaFilename = TEXT("rpc_endpoints.proto");

class SchemaValidator
{
public:
	bool ValidateGeneratedSchemaAgainstExpectedSchema(const FString& GeneratedSchemaContent, const FString& ExpectedSchemaFilename)
	{
		FString ExpectedContentFullPath =
			FPaths::Combine(FSpatialGDKServicesModule::GetSpatialGDKPluginDirectory(ExpectedContentsDirectory), ExpectedSchemaFilename);

		FString ExpectedContent;
		FFileHelper::LoadFileToString(ExpectedContent, *ExpectedContentFullPath);
		while (true)
		{
			FString SearchString = TEXT("{{id}}");
			int32 Index = ExpectedContent.Find(SearchString, ESearchCase::IgnoreCase, ESearchDir::FromStart, -1);
			if (Index == -1)
				break;
			ExpectedContent.RemoveAt(Index, SearchString.Len());
			ExpectedContent.InsertAt(Index, *FString::FromInt(GetNextFreeId()));
		}

		return (CleanSchema(GeneratedSchemaContent).Compare(CleanSchema(ExpectedContent)) == 0);
	}

	bool ValidateGeneratedSchemaForClass(const FString& FileContent, const UClass* CurrentClass)
	{
		if (FString* ExpectedContentFilenamePtr = ExpectedContentsFilenames.Find(CurrentClass->GetName()))
		{
			return ValidateGeneratedSchemaAgainstExpectedSchema(FileContent, *ExpectedContentFilenamePtr);
		}
		else
		{
			return false;
		}
	}

private:
	int GetNextFreeId() { return FreeId++; }

	int FreeId = 10000;

	// This is needed to ensure the schema generated is the same for both Windows and macOS.
	// The new-line characters differ which will fail the tests when running it on macOS.
	FString CleanSchema(const FString& SchemaContent)
	{
		FString Content = SchemaContent;
		Content.ReplaceInline(TEXT("\r"), TEXT(""));
		Content.ReplaceInline(TEXT("\n"), TEXT(""));
		return Content;
	}
};

class SchemaTestFixture
{
public:
	SchemaTestFixture()
	{
		SpatialGDKEditor::Schema::ResetSchemaGeneratorState();
		EnableSpatialNetworking();
	}
	virtual ~SchemaTestFixture()
	{
		DeleteTestFolders();
		ResetSpatialNetworking();
	}

private:
	void DeleteTestFolders()
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.DeleteDirectoryRecursively(*FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Spatial/Tests/")));
		PlatformFile.DeleteDirectoryRecursively(*SchemaOutputFolder);
	}

	void EnableSpatialNetworking()
	{
		UGeneralProjectSettings* GeneralProjectSettings = GetMutableDefault<UGeneralProjectSettings>();
		bCachedSpatialNetworking = GeneralProjectSettings->UsesSpatialNetworking();
		GeneralProjectSettings->SetUsesSpatialNetworking(true);
	}

	void ResetSpatialNetworking()
	{
		UGeneralProjectSettings* GeneralProjectSettings = GetMutableDefault<UGeneralProjectSettings>();
		GetMutableDefault<UGeneralProjectSettings>()->SetUsesSpatialNetworking(bCachedSpatialNetworking);
		bCachedSpatialNetworking = true;
	}

	bool bCachedSpatialNetworking = true;
};

class SchemaRPCEndpointTestFixture : public SchemaTestFixture
{
public:
	SchemaRPCEndpointTestFixture() { SetRPCRingBufferSize(); }
	~SchemaRPCEndpointTestFixture() { ResetRPCRingBufferSize(); }

private:
	void SetRPCRingBufferSize()
	{
		USpatialGDKSettings* SpatialGDKSettings = GetMutableDefault<USpatialGDKSettings>();
		CachedDefaultRPCRingBufferSize = SpatialGDKSettings->DefaultRPCRingBufferSize;
		CachedRPCRingBufferSizeOverrides = SpatialGDKSettings->RPCRingBufferSizeOverrides;
		SpatialGDKSettings->DefaultRPCRingBufferSize = ExpectedRPCEndpointsRingBufferSize;
		SpatialGDKSettings->RPCRingBufferSizeOverrides = ExpectedRPCRingBufferSizeOverrides;
	}

	void ResetRPCRingBufferSize()
	{
		USpatialGDKSettings* SpatialGDKSettings = GetMutableDefault<USpatialGDKSettings>();
		SpatialGDKSettings->DefaultRPCRingBufferSize = CachedDefaultRPCRingBufferSize;
		SpatialGDKSettings->RPCRingBufferSizeOverrides = CachedRPCRingBufferSizeOverrides;
	}

	uint32 CachedDefaultRPCRingBufferSize;
	TMap<ERPCType, uint32> CachedRPCRingBufferSizeOverrides;
};

} // anonymous namespace

SCHEMA_GENERATOR_TEST(GIVEN_spatial_type_class_WHEN_checked_if_supported_THEN_is_supported)
{
	// GIVEN
	const UClass* SupportedClass = USpatialTypeObjectStub::StaticClass();

	// WHEN
	bool bIsSupported = SpatialGDKEditor::Schema::IsSupportedClass(SupportedClass);

	// THEN
	TestTrue("Spatial type class is supported", bIsSupported);
	return true;
}

SCHEMA_GENERATOR_TEST(GIVEN_class_derived_from_spatial_type_class_WHEN_checked_if_supported_THEN_is_supported)
{
	// GIVEN
	const UClass* SupportedClass = UChildOfSpatialTypeObjectStub::StaticClass();

	// WHEN
	bool bIsSupported = SpatialGDKEditor::Schema::IsSupportedClass(SupportedClass);

	// THEN
	TestTrue("Child of a Spatial type class is supported", bIsSupported);
	return true;
}

SCHEMA_GENERATOR_TEST(GIVEN_null_pointer_WHEN_checked_if_supported_THEN_is_not_supported)
{
	// GIVEN
	const UClass* SupportedClass = nullptr;

	// WHEN
	bool bIsSupported = SpatialGDKEditor::Schema::IsSupportedClass(SupportedClass);

	// THEN
	TestFalse("Null pointer is not a valid argument", bIsSupported);
	return true;
}

SCHEMA_GENERATOR_TEST(GIVEN_non_spatial_type_class_WHEN_checked_if_supported_THEN_is_not_supported)
{
	// GIVEN
	const UClass* SupportedClass = UNotSpatialTypeObjectStub::StaticClass();

	// WHEN
	bool bIsSupported = SpatialGDKEditor::Schema::IsSupportedClass(SupportedClass);

	// THEN
	TestFalse("Non spatial type class is not supported", bIsSupported);
	return true;
}

SCHEMA_GENERATOR_TEST(GIVEN_class_derived_from_non_spatial_type_class_WHEN_checked_if_supported_THEN_is_not_supported)
{
	// GIVEN
	const UClass* SupportedClass = UChildOfNotSpatialTypeObjectStub::StaticClass();

	// WHEN
	bool bIsSupported = SpatialGDKEditor::Schema::IsSupportedClass(SupportedClass);

	// THEN
	TestFalse("Child of Non-Spatial type class is not supported", bIsSupported);
	return true;
}

SCHEMA_GENERATOR_TEST(GIVEN_a_class_with_not_spatial_tag_WHEN_checked_if_supported_THEN_is_not_supported)
{
	// GIVEN
	const UClass* SupportedClass = UNotSpatialTypeObjectStub::StaticClass();

	// WHEN
	bool bIsSupported = SpatialGDKEditor::Schema::IsSupportedClass(SupportedClass);

	// THEN
	TestFalse("Class with Not Spatial Type flag is not supported", bIsSupported);
	return true;
}

SCHEMA_GENERATOR_TEST(GIVEN_a_class_without_any_spatial_tags_WHEN_checked_if_supported_THEN_is_not_supported)
{
	// GIVEN
	const UClass* SupportedClass = UNoSpatialFlagsObjectStub ::StaticClass();

	// WHEN
	bool bIsSupported = SpatialGDKEditor::Schema::IsSupportedClass(SupportedClass);

	// THEN
	TestFalse("Class without Spatial flags is not supported", bIsSupported);
	return true;
}

SCHEMA_GENERATOR_TEST(GIVEN_child_of_a_class_without_any_spatial_tags_WHEN_checked_if_supported_THEN_is_not_supported)
{
	// GIVEN
	const UClass* SupportedClass = UChildOfNoSpatialFlagsObjectStub::StaticClass();

	// WHEN
	bool bIsSupported = SpatialGDKEditor::Schema::IsSupportedClass(SupportedClass);

	// THEN
	TestFalse("Child class of class without Spatial flags is not supported", bIsSupported);
	return true;
}

SCHEMA_GENERATOR_TEST(GIVEN_multiple_classes_WHEN_generated_schema_for_these_classes_THEN_corresponding_schema_files_exist)
{
	SchemaTestFixture Fixture;

	// GIVEN
	TSet<UClass*> Classes = { USpatialTypeObjectStub::StaticClass(), ASpatialTypeActor::StaticClass() };

	// WHEN
	SpatialGDKEditor::Schema::SpatialGDKGenerateSchemaForClasses(Classes, SchemaOutputFolder);

	// THEN
	bool bExpectedFilesExist = true;
	for (const auto& CurrentClass : Classes)
	{
		if (LoadSchemaFileForClass(SchemaOutputFolder, CurrentClass).IsEmpty())
		{
			bExpectedFilesExist = false;
			break;
		}
	}

	TestTrue("All expected schema files have been generated", bExpectedFilesExist);

	return true;
}

SCHEMA_GENERATOR_TEST(GIVEN_an_Actor_class_WHEN_generated_schema_for_this_class_THEN_a_file_with_expected_schema_exists)
{
	SchemaTestFixture Fixture;

	// GIVEN
	SchemaValidator Validator;
	UClass* CurrentClass = ASpatialTypeActor::StaticClass();
	TSet<UClass*> Classes = { CurrentClass };

	// WHEN
	SpatialGDKEditor::Schema::SpatialGDKGenerateSchemaForClasses(Classes, SchemaOutputFolder);

	// THEN
	FString FileContent = LoadSchemaFileForClass(SchemaOutputFolder, CurrentClass);
	TestTrue("Generated Actor schema matches the expected schema", Validator.ValidateGeneratedSchemaForClass(FileContent, CurrentClass));

	return true;
}

SCHEMA_GENERATOR_TEST(GIVEN_multiple_Actor_classes_WHEN_generated_schema_for_these_classes_THEN_files_with_expected_schema_exist)
{
	SchemaTestFixture Fixture;

	// GIVEN
	SchemaValidator Validator;
	TSet<UClass*> Classes = { ASpatialTypeActor::StaticClass(), ANonSpatialTypeActor::StaticClass() };

	// Classes need to be sorted to have proper ids
	Classes.Sort([](const UClass& A, const UClass& B) {
		return A.GetPathName() < B.GetPathName();
	});

	// WHEN
	SpatialGDKEditor::Schema::SpatialGDKGenerateSchemaForClasses(Classes, SchemaOutputFolder);

	// THEN
	bool bGeneratedSchemaMatchesExpected = true;
	for (const auto& CurrentClass : Classes)
	{
		FString FileContent = LoadSchemaFileForClass(SchemaOutputFolder, CurrentClass);
		if (!Validator.ValidateGeneratedSchemaForClass(FileContent, CurrentClass))
		{
			bGeneratedSchemaMatchesExpected = false;
			break;
		}
	}

	TestTrue("Generated Actor schema matches the expected schema", bGeneratedSchemaMatchesExpected);

	return true;
}

SCHEMA_GENERATOR_TEST(GIVEN_an_Actor_component_class_WHEN_generated_schema_for_this_class_THEN_a_file_with_expected_schema_exists)
{
	SchemaTestFixture Fixture;

	// GIVEN
	SchemaValidator Validator;
	UClass* CurrentClass = USpatialTypeActorComponent::StaticClass();
	TSet<UClass*> Classes = { CurrentClass };

	// WHEN
	SpatialGDKEditor::Schema::SpatialGDKGenerateSchemaForClasses(Classes, SchemaOutputFolder);

	// THEN
	FString FileContent = LoadSchemaFileForClass(SchemaOutputFolder, CurrentClass);
	TestTrue("Generated Actor schema matches the expected schema", Validator.ValidateGeneratedSchemaForClass(FileContent, CurrentClass));

	return true;
}

SCHEMA_GENERATOR_TEST(
	GIVEN_an_Actor_class_with_an_actor_component_WHEN_generated_schema_for_this_class_THEN_a_file_with_expected_schema_exists)
{
	SchemaTestFixture Fixture;

	// GIVEN
	SchemaValidator Validator;
	UClass* CurrentClass = ASpatialTypeActorWithActorComponent::StaticClass();
	TSet<UClass*> Classes = { CurrentClass };

	// WHEN
	SpatialGDKEditor::Schema::SpatialGDKGenerateSchemaForClasses(Classes, SchemaOutputFolder);

	// THEN
	FString FileContent = LoadSchemaFileForClass(SchemaOutputFolder, CurrentClass);
	TestTrue("Generated Actor schema matches the expected schema", Validator.ValidateGeneratedSchemaForClass(FileContent, CurrentClass));

	return true;
}

SCHEMA_GENERATOR_TEST(
	GIVEN_an_Actor_class_with_multiple_actor_components_WHEN_generated_schema_for_this_class_THEN_files_with_expected_schema_exist)
{
	SchemaTestFixture Fixture;

	// GIVEN
	SchemaValidator Validator;
	UClass* CurrentClass = ASpatialTypeActorWithMultipleActorComponents::StaticClass();
	TSet<UClass*> Classes = { CurrentClass };

	// WHEN
	SpatialGDKEditor::Schema::SpatialGDKGenerateSchemaForClasses(Classes, SchemaOutputFolder);

	// THEN
	FString FileContent = LoadSchemaFileForClass(SchemaOutputFolder, CurrentClass);
	TestTrue("Generated Actor schema matches the expected schema", Validator.ValidateGeneratedSchemaForClass(FileContent, CurrentClass));

	return true;
}

SCHEMA_GENERATOR_TEST(
	GIVEN_an_Actor_class_with_multiple_object_components_WHEN_generated_schema_for_this_class_THEN_files_with_expected_schema_exist)
{
	SchemaTestFixture Fixture;

	// GIVEN
	SchemaValidator Validator;
	UClass* CurrentClass = ASpatialTypeActorWithMultipleObjectComponents::StaticClass();
	TSet<UClass*> Classes = { CurrentClass };

	// WHEN
	SpatialGDKEditor::Schema::SpatialGDKGenerateSchemaForClasses(Classes, SchemaOutputFolder);

	// THEN
	FString FileContent = LoadSchemaFileForClass(SchemaOutputFolder, CurrentClass);
	TestTrue("Generated Actor schema matches the expected schema", Validator.ValidateGeneratedSchemaForClass(FileContent, CurrentClass));

	return true;
}

SCHEMA_GENERATOR_TEST(GIVEN_multiple_schema_files_exist_WHEN_refresh_generated_files_THEN_schema_files_exist)
{
	SchemaTestFixture Fixture;

	// GIVEN
	TSet<UClass*> Classes = { USpatialTypeObjectStub::StaticClass(), ASpatialTypeActor::StaticClass() };

	SpatialGDKEditor::Schema::SpatialGDKGenerateSchemaForClasses(Classes, SchemaOutputFolder);

	// WHEN
	bool bRefreshSuccess = SpatialGDKEditor::Schema::RefreshSchemaFiles(SchemaOutputFolder);
	TestTrue("RefreshSchema was successful", bRefreshSuccess);

	// THEN
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	TestTrue("Schema directory exists", PlatformFile.DirectoryExists(*SchemaOutputFolder));

	return true;
}

SCHEMA_GENERATOR_TEST(GIVEN_no_schema_files_exist_WHEN_refresh_generated_files_THEN_schema_files_exist)
{
	// GIVEN

	// WHEN
	bool bRefreshSuccess = SpatialGDKEditor::Schema::RefreshSchemaFiles(SchemaOutputFolder);
	TestTrue("RefreshSchema was successful", bRefreshSuccess);

	// THEN
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	TestTrue("Schema directory now exists", PlatformFile.DirectoryExists(*SchemaOutputFolder));

	return true;
}

SCHEMA_GENERATOR_TEST(GIVEN_multiple_classes_with_schema_generated_WHEN_schema_database_saved_THEN_schema_database_exists)
{
	SchemaTestFixture Fixture;

	// GIVEN
	TSet<UClass*> Classes = { USpatialTypeObjectStub::StaticClass(), ASpatialTypeActor::StaticClass() };

	SpatialGDKEditor::Schema::SpatialGDKGenerateSchemaForClasses(Classes, SchemaOutputFolder);

	// WHEN
	USchemaDatabase* SchemaDatabase = SpatialGDKEditor::Schema::InitialiseSchemaDatabase(DatabaseOutputFile);
	SpatialGDKEditor::Schema::SaveSchemaDatabase(SchemaDatabase);

	// THEN
	const FString SchemaDatabasePackagePath = FPaths::Combine(FPaths::ProjectContentDir(), SchemaDatabaseFileName);
	const FString ExpectedSchemaDatabaseFileName =
		FPaths::SetExtension(SchemaDatabasePackagePath, FPackageName::GetAssetPackageExtension());
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	TestTrue("Generated schema database exists", PlatformFile.FileExists(*ExpectedSchemaDatabaseFileName));

	return true;
}

SCHEMA_GENERATOR_TEST(GIVEN_a_class_with_schema_generated_WHEN_schema_database_saved_THEN_expected_schema_database_exists)
{
	SchemaTestFixture Fixture;

	// GIVEN
	UClass* CurrentClass = ASpatialTypeActorWithSubobject::StaticClass();
	TSet<UClass*> Classes = { CurrentClass };

	SpatialGDKEditor::Schema::SpatialGDKGenerateSchemaForClasses(Classes, SchemaOutputFolder);

	// WHEN
	USchemaDatabase* SchemaDatabase = SpatialGDKEditor::Schema::InitialiseSchemaDatabase(DatabaseOutputFile);
	SpatialGDKEditor::Schema::SaveSchemaDatabase(SchemaDatabase);

	// THEN
	bool bDatabaseMatchesExpected = true;
	FSoftObjectPath SchemaDatabasePath = FSoftObjectPath(FPaths::SetExtension(DatabaseOutputFile, TEXT(".protoDatabase")));
	USchemaDatabase* LoadedSchemaDatabase = Cast<USchemaDatabase>(SchemaDatabasePath.TryLoad());
	if (SchemaDatabase == nullptr)
	{
		bDatabaseMatchesExpected = false;
	}
	else
	{
		if (!TestEqualDatabaseEntryAndSchemaFile(CurrentClass, SchemaOutputFolder, SchemaDatabase))
		{
			bDatabaseMatchesExpected = false;
		}
	}

	TestTrue("Generated schema database matches the expected database", bDatabaseMatchesExpected);

	return true;
}

// This test tests AllTestClassesSet classes schema generation.
//   Compare the loaded schema data with the saved schema to check if the given classes are fully supported.
SCHEMA_GENERATOR_TEST(GIVEN_multiple_classes_with_schema_generated_WHEN_schema_database_saved_THEN_expected_schema_database_exists)
{
	SchemaTestFixture Fixture;

	// GIVEN
	const TSet<UClass*>& Classes = AllTestClassesSet();

	SpatialGDKEditor::Schema::SpatialGDKGenerateSchemaForClasses(Classes, SchemaOutputFolder);

	// WHEN
	USchemaDatabase* SchemaDatabase = SpatialGDKEditor::Schema::InitialiseSchemaDatabase(DatabaseOutputFile);
	SpatialGDKEditor::Schema::SaveSchemaDatabase(SchemaDatabase);

	// THEN
	bool bDatabaseMatchesExpected = true;
	FSoftObjectPath SchemaDatabasePath = FSoftObjectPath(FPaths::SetExtension(DatabaseOutputFile, TEXT(".protoDatabase")));
	USchemaDatabase* LoadedSchemaDatabase = Cast<USchemaDatabase>(SchemaDatabasePath.TryLoad());
	if (SchemaDatabase == nullptr)
	{
		bDatabaseMatchesExpected = false;
	}
	else
	{
		for (const auto& CurrentClass : Classes)
		{
			if (!TestEqualDatabaseEntryAndSchemaFile(CurrentClass, SchemaOutputFolder, SchemaDatabase))
			{
				bDatabaseMatchesExpected = false;
				break;
			}
		}
	}

	TestTrue("Generated schema database matches the expected database", bDatabaseMatchesExpected);

	return true;
}

SCHEMA_GENERATOR_TEST(GIVEN_schema_database_exists_WHEN_schema_database_deleted_THEN_no_schema_database_exists)
{
	SchemaTestFixture Fixture;

	// GIVEN
	UClass* CurrentClass = ASpatialTypeActor::StaticClass();
	TSet<UClass*> Classes = { CurrentClass };

	SpatialGDKEditor::Schema::SpatialGDKGenerateSchemaForClasses(Classes, SchemaOutputFolder);
	USchemaDatabase* SchemaDatabase = SpatialGDKEditor::Schema::InitialiseSchemaDatabase(DatabaseOutputFile);
	SpatialGDKEditor::Schema::SaveSchemaDatabase(SchemaDatabase);

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	const FString SchemaDatabasePackagePath = FPaths::Combine(FPaths::ProjectContentDir(), SchemaDatabaseFileName);
	const FString ExpectedSchemaDatabaseFileName =
		FPaths::SetExtension(SchemaDatabasePackagePath, FPackageName::GetAssetPackageExtension());
	bool bFileCreated = PlatformFile.FileExists(*ExpectedSchemaDatabaseFileName);

	// WHEN
	SpatialGDKEditor::Schema::DeleteSchemaDatabase(SchemaDatabaseFileName);

	// THEN
	bool bResult = bFileCreated && !PlatformFile.FileExists(*ExpectedSchemaDatabaseFileName);
	TestTrue("Generated schema existed and is now deleted", bResult);

	return true;
}

SCHEMA_GENERATOR_TEST(GIVEN_schema_database_exists_WHEN_tried_to_load_THEN_loaded)
{
	SchemaTestFixture Fixture;

	// GIVEN
	UClass* CurrentClass = ASpatialTypeActor::StaticClass();
	TSet<UClass*> Classes = { CurrentClass };

	SpatialGDKEditor::Schema::SpatialGDKGenerateSchemaForClasses(Classes, SchemaOutputFolder);
	USchemaDatabase* SchemaDatabase = SpatialGDKEditor::Schema::InitialiseSchemaDatabase(DatabaseOutputFile);
	SpatialGDKEditor::Schema::SaveSchemaDatabase(SchemaDatabase);

	// WHEN
	bool bSuccess = SpatialGDKEditor::Schema::LoadGeneratorStateFromSchemaDatabase(SchemaDatabaseFileName);

	// THEN
	TestTrue("Schema database loaded", bSuccess);

	return true;
}

SCHEMA_GENERATOR_TEST(GIVEN_schema_database_does_not_exist_WHEN_tried_to_load_THEN_not_loaded)
{
	SchemaTestFixture Fixture;

	// GIVEN
	SpatialGDKEditor::Schema::DeleteSchemaDatabase(SchemaDatabaseFileName);

	// WHEN
	const FString SchemaDatabasePackagePath = FPaths::Combine(FPaths::ProjectContentDir(), SchemaDatabaseFileName);
	const FString ExpectedSchemaDatabaseFileName =
		FPaths::SetExtension(SchemaDatabasePackagePath, FPackageName::GetAssetPackageExtension());
	bool bSuccess = SpatialGDKEditor::Schema::LoadGeneratorStateFromSchemaDatabase(SchemaDatabaseFileName);

	// THEN
	TestFalse("Schema database not loaded", bSuccess);

	return true;
}

SCHEMA_GENERATOR_TEST(GIVEN_source_and_destination_of_well_known_schema_files_WHEN_copied_THEN_expected_files_exist)
{
	SchemaTestFixture Fixture;

	// GIVEN
	FString GDKSchemaCopyDir = FPaths::Combine(SpatialGDKServicesConstants::SpatialOSDirectory, TEXT("/Tests/schema/unreal/gdk"));
	FString CoreSDKSchemaCopyDir =
		FPaths::Combine(SpatialGDKServicesConstants::SpatialOSDirectory, TEXT("/Tests/build/dependencies/schema/standard_library"));
	TArray<FString> GDKSchemaFilePaths = { "authority_intent.proto",
										   "core_types.proto",
										   "debug_component.proto",
										   "gameplay_debugger_component.proto",
										   "debug_metrics.proto",
										   "global_state_manager.proto",
										   "initial_only_presence.proto",
										   "player_controller.proto",
										   "known_entity_auth_component_set.proto",
										   "migration_diagnostic.proto",
										   "net_owning_client_worker.proto",
										   "not_streamed.proto",
										   "partition_shadow.proto",
										   "query_tags.proto",
										   "relevant.proto",
										   "rpc_components.proto",
										   "rpc_payload.proto",
										   "server_worker.proto",
										   "spatial_debugging.proto",
										   "actor_group_member.proto",
										   "actor_set_member.proto",
										   "actor_ownership.proto",
										   "spawndata.proto",
										   "spawner.proto",
										   "tombstone.proto",
										   "unreal_metadata.proto",
										   "virtual_worker_translation.proto" };
	TArray<FString> CoreSDKFilePaths = { "improbable\\restricted\\system_components.proto", "improbable\\standard_library.proto" };

	// WHEN
	SpatialGDKEditor::Schema::CopyWellKnownSchemaFiles(GDKSchemaCopyDir, CoreSDKSchemaCopyDir);

	// THEN
	bool bExpectedFilesCopied = true;

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	TArray<FString> FoundSchemaFiles;
	PlatformFile.FindFilesRecursively(FoundSchemaFiles, *GDKSchemaCopyDir, TEXT(""));
	if (FoundSchemaFiles.Num() != GDKSchemaFilePaths.Num())
	{
		bExpectedFilesCopied = false;
	}
	for (const auto& FilePath : GDKSchemaFilePaths)
	{
		if (!PlatformFile.FileExists(*FPaths::Combine(GDKSchemaCopyDir, FilePath)))
		{
			bExpectedFilesCopied = false;
			break;
		}
	}

	TArray<FString> FoundCoreSDKFiles;
	PlatformFile.FindFilesRecursively(FoundCoreSDKFiles, *CoreSDKSchemaCopyDir, TEXT(""));
	if (FoundCoreSDKFiles.Num() != CoreSDKFilePaths.Num())
	{
		bExpectedFilesCopied = false;
	}
	for (const auto& FilePath : CoreSDKFilePaths)
	{
		if (!PlatformFile.FileExists(*FPaths::Combine(CoreSDKSchemaCopyDir, FilePath)))
		{
			bExpectedFilesCopied = false;
			break;
		}
	}

	TestTrue("Expected files have been copied", bExpectedFilesCopied);

	return true;
}

SCHEMA_GENERATOR_TEST(GIVEN_multiple_classes_WHEN_getting_all_supported_classes_THEN_all_unsupported_classes_are_filtered)
{
	SchemaTestFixture Fixture;

	// GIVEN
	const TArray<UObject*>& Classes = AllTestClassesArray();

	// WHEN
	TSet<UClass*> FilteredClasses = SpatialGDKEditor::Schema::GetAllSupportedClasses(Classes);

	// THEN
	TSet<UClass*> ExpectedClasses = { USpatialTypeObjectStub::StaticClass(),
									  UChildOfSpatialTypeObjectStub::StaticClass(),
									  ASpatialTypeActor::StaticClass(),
									  ANonSpatialTypeActor::StaticClass(),
									  USpatialTypeActorComponent::StaticClass(),
									  ASpatialTypeActorWithActorComponent::StaticClass(),
									  ASpatialTypeActorWithMultipleActorComponents::StaticClass(),
									  ASpatialTypeActorWithMultipleObjectComponents::StaticClass(),
									  ASpatialTypeActorWithSubobject::StaticClass() };

	bool bClassesFilteredCorrectly = true;
	if (FilteredClasses.Num() == ExpectedClasses.Num())
	{
		for (const auto& ExpectedClass : ExpectedClasses)
		{
			if (!FilteredClasses.Contains(ExpectedClass))
			{
				bClassesFilteredCorrectly = false;
				break;
			}
		}
	}
	else
	{
		bClassesFilteredCorrectly = false;
	}

	TestTrue("Supported classes have been filtered correctly", bClassesFilteredCorrectly);

	return true;
}

SCHEMA_GENERATOR_TEST(
	GIVEN_3_level_names_WHEN_generating_schema_for_sublevels_THEN_generated_schema_contains_3_components_with_unique_names)
{
	SchemaTestFixture Fixture;

	// GIVEN
	TMultiMap<FName, FName> LevelNamesToPaths;
	LevelNamesToPaths.Add(TEXT("TestLevel0"), TEXT("/Game/Maps/FirstTestLevel0"));
	LevelNamesToPaths.Add(TEXT("TestLevel0"), TEXT("/Game/Maps/SecondTestLevel0"));
	LevelNamesToPaths.Add(TEXT("TestLevel01"), TEXT("/Game/Maps/TestLevel01"));

	// WHEN
	SpatialGDKEditor::Schema::GenerateSchemaForSublevels(SchemaOutputFolder, LevelNamesToPaths);

	// THEN
	TArray<FString> LoadedSchema;
	FFileHelper::LoadFileToStringArray(LoadedSchema, *FPaths::Combine(SchemaOutputFolder, TEXT("Sublevels/sublevels.proto")));
	ComponentNamesAndIds ParsedNamesAndIds = ParseAvailableNamesAndIdsFromSchemaFile(LoadedSchema);

	bool bHasDuplicateNames = false;
	for (int i = 0; i < ParsedNamesAndIds.Names.Num() - 1; ++i)
	{
		for (int j = i + 1; j < ParsedNamesAndIds.Names.Num(); ++j)
		{
			if (ParsedNamesAndIds.Names[i].Compare(ParsedNamesAndIds.Names[j]) == 0)
			{
				bHasDuplicateNames = true;
				break;
			}
		}
	}

	TestFalse("No duplicate component names generated for equal sublevel map names", bHasDuplicateNames);

	return true;
}

SCHEMA_GENERATOR_TEST(GIVEN_no_schema_exists_WHEN_generating_schema_for_rpc_endpoints_THEN_generated_schema_matches_expected_contents)
{
	SchemaRPCEndpointTestFixture Fixture;
	SchemaValidator Validator;

	SpatialGDKEditor::Schema::GenerateSchemaForRPCEndpoints(SchemaOutputFolder);

	FString FileContent;
	FFileHelper::LoadFileToString(FileContent, *FPaths::Combine(SchemaOutputFolder, ExpectedRPCEndpointsSchemaFilename));

	TestTrue("Generated RPC endpoints schema matches the expected schema",
			 Validator.ValidateGeneratedSchemaAgainstExpectedSchema(FileContent, ExpectedRPCEndpointsSchemaFilename));

	return true;
}

SCHEMA_GENERATOR_TEST(GIVEN_actor_class_WHEN_generating_schema_THEN_expected_component_set_filled)
{
	SchemaTestFixture Fixture;

	TSet<UClass*> Classes = { ASpatialTypeActor::StaticClass(), USchemaGenObjectStubHandOver::StaticClass(),
							  ASpatialTypeActorWithOwnerOnly::StaticClass(), ASpatialTypeActorWithInitialOnly::StaticClass() };

	FString SchemaFolder = FPaths::Combine(SchemaOutputFolder, TEXT("schema"));
	FString UnrealSchemaFolder = FPaths::Combine(SchemaFolder, TEXT("unreal"));
	FString SchemaGenerationFolder = FPaths::Combine(UnrealSchemaFolder, TEXT("generated"));

	// Generate data for well known classes.
	SpatialGDKEditor::Schema::SpatialGDKGenerateSchemaForClasses(Classes, SchemaGenerationFolder);
	USchemaDatabase* SchemaDatabase = SpatialGDKEditor::Schema::InitialiseSchemaDatabase(DatabaseOutputFile);
	SpatialGDKEditor::Schema::WriteComponentSetFiles(SchemaDatabase, SchemaGenerationFolder);

	FString SchemaBuildFolder = FPaths::Combine(SchemaOutputFolder, TEXT("Build"));

	// Add the files necessary to run the schema compiler
	FString GDKSchemaCopyDir = FPaths::Combine(UnrealSchemaFolder, TEXT("gdk"));
	FString CoreSDKSchemaCopyDir = FPaths::Combine(SchemaBuildFolder, TEXT("dependencies/schema/standard_library"));
	SpatialGDKEditor::Schema::CopyWellKnownSchemaFiles(GDKSchemaCopyDir, CoreSDKSchemaCopyDir);
	SpatialGDKEditor::Schema::GenerateSchemaForRPCEndpoints(SchemaGenerationFolder);
	SpatialGDKEditor::Schema::GenerateSchemaForNCDs(SchemaGenerationFolder);

	// Run the schema compiler
	FString SchemaJsonPath;

	TestTrue("Schema compiler run successful",
			 SpatialGDKEditor::Schema::RunSchemaCompiler(SchemaJsonPath, SchemaFolder, SchemaBuildFolder));

	TestTrue("Schema bundle file successfully read", SpatialGDKEditor::Schema::ExtractInformationFromSchemaJson(
														 SchemaJsonPath, SchemaDatabase->ComponentSetIdToComponentIds,
														 SchemaDatabase->ComponentIdToFieldIdsIndex, SchemaDatabase->FieldIdsArray));

	TestTrue("Expected number of component set", SchemaDatabase->ComponentSetIdToComponentIds.Num() == 10);

	TestTrue("Found spatial well known components",
			 SchemaDatabase->ComponentSetIdToComponentIds.Contains(SpatialConstants::SPATIALOS_WELLKNOWN_COMPONENTSET_ID));
	if (SchemaDatabase->ComponentSetIdToComponentIds.Contains(SpatialConstants::SPATIALOS_WELLKNOWN_COMPONENTSET_ID))
	{
		TestTrue(
			"Spatial well know component is not empty",
			SchemaDatabase->ComponentSetIdToComponentIds[SpatialConstants::SPATIALOS_WELLKNOWN_COMPONENTSET_ID].ComponentIDs.Num() > 0);
	}

	TestTrue("Found Server worker components components",
			 SchemaDatabase->ComponentSetIdToComponentIds.Contains(SpatialConstants::SERVER_WORKER_ENTITY_AUTH_COMPONENT_SET_ID));
	if (SchemaDatabase->ComponentSetIdToComponentIds.Contains(SpatialConstants::SERVER_WORKER_ENTITY_AUTH_COMPONENT_SET_ID))
	{
		TestTrue(
			"Spatial well known component is not empty",
			SchemaDatabase->ComponentSetIdToComponentIds[SpatialConstants::SERVER_WORKER_ENTITY_AUTH_COMPONENT_SET_ID].ComponentIDs.Num()
				> 0);
	}

	{
		FComponentIDs* RoutingComponents =
			SchemaDatabase->ComponentSetIdToComponentIds.Find(SpatialConstants::ROUTING_WORKER_AUTH_COMPONENT_SET_ID);
		TestTrue("Found routing worker components", RoutingComponents != nullptr);
		if (RoutingComponents != nullptr)
		{
			TestTrue("Expected number of routing worker components",
					 RoutingComponents->ComponentIDs.Num() == SpatialConstants::RoutingWorkerComponents.Num());

			for (auto ComponentId : SpatialConstants::RoutingWorkerComponents)
			{
				FString DebugString = FString::Printf(TEXT("Found well known component %s"), *ComponentId.Value);
				TestTrue(*DebugString, RoutingComponents->ComponentIDs.Find(ComponentId.Key) != INDEX_NONE);
			}
		}
	}

	{
		// Check the resulting schema contains the expected Sets.

		FComponentIDs* ServerComponents = SchemaDatabase->ComponentSetIdToComponentIds.Find(SpatialConstants::SERVER_AUTH_COMPONENT_SET_ID);
		TestTrue("Found entry for server authority", ServerComponents != nullptr);
		if (ServerComponents == nullptr)
		{
			return false;
		}
		TestTrue("Set is not empty", ServerComponents->ComponentIDs.Num() > 0);
		for (auto ComponentId : SpatialConstants::ServerAuthorityWellKnownComponents)
		{
			FString DebugString = FString::Printf(TEXT("Found well known component %s"), *ComponentId.Value);
			TestTrue(*DebugString, ServerComponents->ComponentIDs.Find(ComponentId.Key) != INDEX_NONE);
		}

		uint32 ServerAuthSets[] = { SpatialConstants::DATA_COMPONENT_SET_ID, SpatialConstants::OWNER_ONLY_COMPONENT_SET_ID,
									SpatialConstants::HANDOVER_COMPONENT_SET_ID, SpatialConstants::INITIAL_ONLY_COMPONENT_SET_ID };

		for (uint32 ComponentType = SCHEMA_Begin; ComponentType < SCHEMA_Count; ++ComponentType)
		{
			FComponentIDs* DataComponents = SchemaDatabase->ComponentSetIdToComponentIds.Find(ServerAuthSets[ComponentType]);
			TestTrue("Found entry for class in data type component set", DataComponents != nullptr);
			if (DataComponents == nullptr)
			{
				return false;
			}
			// We should have a class for each type of set
			TestTrue("Set is not empty", DataComponents->ComponentIDs.Num() > 0);

			for (auto Class : Classes)
			{
				if (Class->IsChildOf<AActor>())
				{
					FActorSchemaData* SchemaData = SchemaDatabase->ActorClassPathToSchema.Find(Class->GetPathName());
					TestTrue("Found schema data", SchemaData != nullptr);
					if (SchemaData == nullptr)
					{
						continue;
					}
					uint32 ComponentId = SchemaData->SchemaComponents[ComponentType];
					if (ComponentId != 0)
					{
						FString DebugString = FString::Printf(TEXT("Schema data for component %i found in"), ComponentId);
						TestTrue(DebugString + TEXT(" server auth component set"),
								 ServerComponents->ComponentIDs.Find(ComponentId) != INDEX_NONE);
						TestTrue(DebugString + TEXT(" data type component set"),
								 DataComponents->ComponentIDs.Find(ComponentId) != INDEX_NONE);
					}
				}
				else
				{
					FSubobjectSchemaData* SchemaData = SchemaDatabase->SubobjectClassPathToSchema.Find(Class->GetPathName());
					TestTrue("Found schema data", SchemaData != nullptr);
					if (SchemaData == nullptr)
					{
						continue;
					}
					for (auto& DynamicComponent : SchemaData->DynamicSubobjectComponents)
					{
						uint32 ComponentId = DynamicComponent.SchemaComponents[ComponentType];
						if (ComponentId != 0)
						{
							FString DebugString = FString::Printf(TEXT("Schema data for component %i found in"), ComponentId);
							TestTrue(DebugString + TEXT(" server auth component set"),
									 ServerComponents->ComponentIDs.Find(ComponentId) != INDEX_NONE);
							TestTrue(DebugString + TEXT(" data type component set"),
									 DataComponents->ComponentIDs.Find(ComponentId) != INDEX_NONE);
						}
					}
				}
			}
		}
	}

	{
		FComponentIDs* ClientComponents = SchemaDatabase->ComponentSetIdToComponentIds.Find(SpatialConstants::CLIENT_AUTH_COMPONENT_SET_ID);
		TestTrue("Found entry for client authority", ClientComponents != nullptr);
		if (ClientComponents == nullptr)
		{
			return false;
		}
		TestTrue("Set is not empty", ClientComponents->ComponentIDs.Num() > 0);
		for (auto ComponentId : SpatialConstants::ClientAuthorityWellKnownComponents)
		{
			FString DebugString = FString::Printf(TEXT("Found well known component %s"), *ComponentId.Value);
			TestTrue(*DebugString, ClientComponents->ComponentIDs.Find(ComponentId.Key) != INDEX_NONE);
		}
	}

	{
		FComponentIDs* GDKWellKnownComponents =
			SchemaDatabase->ComponentSetIdToComponentIds.Find(SpatialConstants::GDK_KNOWN_ENTITY_AUTH_COMPONENT_SET_ID);
		TestTrue("Found entry for GDK well know entities authority", GDKWellKnownComponents != nullptr);
		if (GDKWellKnownComponents == nullptr)
		{
			return false;
		}
		TestTrue("Set is not empty", GDKWellKnownComponents->ComponentIDs.Num() > 0);
		for (auto ComponentId : SpatialConstants::KnownEntityAuthorityComponents)
		{
			FString DebugString = FString::Printf(TEXT("Found well known component %i"), ComponentId);
			TestTrue(*DebugString, GDKWellKnownComponents->ComponentIDs.Find(ComponentId) != INDEX_NONE);
		}
	}

	return true;
}

SCHEMA_GENERATOR_TEST(
	GIVEN_snapshot_affecting_schema_files_WHEN_hash_of_file_contents_is_generated_THEN_hash_matches_expected_snapshot_version_hash)
{
	SchemaTestFixture Fixture;

	// GIVEN
	FString GDKSchemaCopyDir = FPaths::Combine(SpatialGDKServicesConstants::SpatialOSDirectory, TEXT("schema/unreal/gdk"));
	TArray<FString> GDKSchemaFilePaths = { TEXT("global_state_manager.proto"), TEXT("spawner.proto"),
										   TEXT("virtual_worker_translation.proto") };

	// WHEN
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	uint32 HashCrc = 0;
	TArray<FString> SchemaStrings;

	for (const auto& FilePath : GDKSchemaFilePaths)
	{
		const FString FileNameAndPath = FPaths::Combine(GDKSchemaCopyDir, FilePath);
		if (!PlatformFile.FileExists(*FileNameAndPath))
		{
			const FString DebugString = FString::Printf(TEXT("Expected to find schema file %s"), *FilePath);
			TestFalse(DebugString, true);
			break;
		}
		else
		{
			TArray<FString> FileContents;
			FFileHelper::LoadFileToStringArray(FileContents, *FileNameAndPath);

			for (const FString& LineContents : FileContents)
			{
				HashCrc = FCrc::StrCrc32(*LineContents, HashCrc);
			}
		}
	}

	// THEN
	const FString ErrorMessage =
		FString::Printf(TEXT("Expected hash to be %u, but found it to be %u"), SpatialConstants::SPATIAL_SNAPSHOT_SCHEMA_HASH, HashCrc);
	TestEqual(ErrorMessage, SpatialConstants::SPATIAL_SNAPSHOT_SCHEMA_HASH, HashCrc);

	return true;
}

#undef LOCTEXT_NAMESPACE
