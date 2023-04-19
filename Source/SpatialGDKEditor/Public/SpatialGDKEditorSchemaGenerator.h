// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "Logging/LogMacros.h"

#include "SpatialGDKEditor.h"
#include "Utils/CodeWriter.h"
#include "Utils/SchemaDatabase.h"

#include "WorkerSDK/improbable/c_worker.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSpatialGDKSchemaGenerator, Log, All);

namespace SpatialGDKEditor
{
namespace Schema
{
SPATIALGDKEDITOR_API bool IsSupportedClass(const UClass* SupportedClass);

SPATIALGDKEDITOR_API TSet<UClass*> GetAllSupportedClasses(const TArray<UObject*>& AllClasses);

SPATIALGDKEDITOR_API bool SpatialGDKGenerateSchema();

SPATIALGDKEDITOR_API bool SpatialGDKGenerateSchemaForClasses(TSet<UClass*> Classes, FString SchemaOutputPath = "");

SPATIALGDKEDITOR_API void SpatialGDKSanitizeGeneratedSchema();

SPATIALGDKEDITOR_API void GenerateSchemaForSublevels();

SPATIALGDKEDITOR_API void GenerateSchemaForSublevels(const FString& SchemaOutputPath, const TMultiMap<FName, FName>& LevelNamesToPaths);

SPATIALGDKEDITOR_API void GenerateSchemaForRPCEndpoints();

SPATIALGDKEDITOR_API void GenerateSchemaForRPCEndpoints(const FString& SchemaOutputPath);

SPATIALGDKEDITOR_API void GenerateSchemaForNCDs();

SPATIALGDKEDITOR_API void GenerateSchemaForNCDs(const FString& SchemaOutputPath);

SPATIALGDKEDITOR_API bool LoadGeneratorStateFromSchemaDatabase(const FString& FileName);

SPATIALGDKEDITOR_API bool IsAssetReadOnly(const FString& FileName);

SPATIALGDKEDITOR_API bool GeneratedSchemaDatabaseExists();

SPATIALGDKEDITOR_API FSpatialGDKEditor::ESchemaDatabaseValidationResult ValidateSchemaDatabase();

SPATIALGDKEDITOR_API USchemaDatabase* InitialiseSchemaDatabase(const FString& PackagePath);

SPATIALGDKEDITOR_API bool SaveSchemaDatabase(USchemaDatabase* SchemaDatabase);

SPATIALGDKEDITOR_API bool DeleteSchemaDatabase(const FString& PackagePath);

SPATIALGDKEDITOR_API void ResetSchemaGeneratorState();

SPATIALGDKEDITOR_API void ResetSchemaGeneratorStateAndCleanupFolders();

SPATIALGDKEDITOR_API bool GeneratedSchemaFolderExists();

SPATIALGDKEDITOR_API bool RefreshSchemaFiles(const FString& SchemaOutputPath, const bool bDeleteExistingSchema = true,
											 const bool bCreateDirectoryTree = true);

SPATIALGDKEDITOR_API void CopyWellKnownSchemaFiles(const FString& GDKSchemaCopyDir, const FString& CoreSDKSchemaCopyDir);

SPATIALGDKEDITOR_API bool RunSchemaCompiler(FString& SchemaJsonPath, FString SchemaInputDir = "", FString BuildDir = "");

SPATIALGDKEDITOR_API bool ExtractInformationFromSchemaJson(const FString& SchemaJsonPath, TMap<uint32, FComponentIDs>& OutComponentSetMap,
														   TMap<uint32, uint32>& OutComponentIdToFieldIdsIndex,
														   TArray<FFieldIDs>& OutFieldIdsArray);

SPATIALGDKEDITOR_API void WriteComponentSetFiles(const USchemaDatabase* SchemaDatabase, FString SchemaOutputPath = "");

SPATIALGDKEDITOR_API void WriteServerAuthorityComponentSet(const USchemaDatabase* SchemaDatabase,
														   TArray<Worker_ComponentId>& ServerAuthoritativeComponentIds,
														   const FString& SchemaOutputPath);

SPATIALGDKEDITOR_API void WriteClientAuthorityComponentSet(const FString& SchemaOutputPath);

SPATIALGDKEDITOR_API void WriteComponentSetBySchemaType(const USchemaDatabase* SchemaDatabase, ESchemaComponentType SchemaType,
														const FString& SchemaOutputPath);

SPATIALGDKEDITOR_API bool build_schema();

} // namespace Schema
} // namespace SpatialGDKEditor
