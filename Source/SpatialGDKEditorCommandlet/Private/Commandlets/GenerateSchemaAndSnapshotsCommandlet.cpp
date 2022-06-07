// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "GenerateSchemaAndSnapshotsCommandlet.h"
#include "SpatialGDKEditor.h"
#include "SpatialGDKEditorCommandletPrivate.h"
#include "SpatialGDKEditorSchemaGenerator.h"

#include "Engine/LevelStreaming.h"
#include "Engine/ObjectLibrary.h"
#include "Engine/World.h"
#include "Engine/WorldComposition.h"
#include "FileHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"

UGenerateSchemaAndSnapshotsCommandlet::UGenerateSchemaAndSnapshotsCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UGenerateSchemaAndSnapshotsCommandlet::Main(const FString& Args)
{
	UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Schema & Snapshot Generation Commandlet Started"));

	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, GIsRunningUnattendedScript || IsRunningCommandlet());

	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> Params;
	ParseCommandLine(*Args, Tokens, Switches, Params);

	// TODO: Optionally clean up previous schema and snapshot files once UNR-954 is done
	GeneratedMapPaths.Empty();

	FSpatialGDKEditor SpatialGDKEditor;

	if (!HandleOptions(Switches))
	{
		UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Schema generation aborted"));
		return 1;
	}

	if (Switches.Contains("SkipSchema"))
	{
		UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Skipping schema generation"));
	}
	else
	{
		// Do full schema generation
		if (!GenerateSchema(SpatialGDKEditor))
		{
			return 1;
		}
	}

	if (Params.Contains(MapPathsParamName))
	{
		FString MapNameParam = *Params.Find(MapPathsParamName);

		// Spaces are disallowed in all paths, so we just check for them now and exit early if an invalid path was provided
		if (MapNameParam.Contains(TEXT(" ")))
		{
			UE_LOG(LogSpatialGDKEditorCommandlet, Error, TEXT("%s argument may not contain spaces."), *MapPathsParamName);
			return 1;
		}

		FString ThisMapName;
		FString RemainingMapPaths = MapNameParam;
		while (RemainingMapPaths.Split(TEXT(";"), &ThisMapName, &RemainingMapPaths))
		{
			if (!GenerateSnapshotForPath(SpatialGDKEditor, ThisMapName))
			{
				return 1; // Error
			}
		}
		// When we get to this point, one of two things is true:
		// 1) RemainingMapPaths was NEVER split, and should be interpreted as a single map name
		// 2) RemainingMapPaths was split n times, and the last map that needs to be run after the loop is still in it
		if (!GenerateSnapshotForPath(SpatialGDKEditor, RemainingMapPaths))
		{
			return 1; // Error
		}
	}
	else
	{
		// Default to everything in the project
		if (!GenerateSnapshotForPath(SpatialGDKEditor, TEXT("")))
		{
			return 1; // Error
		}
	}

	UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Schema & Snapshot Generation Commandlet Complete"));

	return 0;
}

bool UGenerateSchemaAndSnapshotsCommandlet::GenerateSnapshotForPath(FSpatialGDKEditor& InSpatialGDKEditor, const FString& InPath)
{
	// Massage input to allow some flexibility in command line path argument:
	// 	/Game/Path/MapName = Single map
	// 	/Game/Path/DirName/ = All maps in a dir, recursively
	// NOTE: "/Game" is optional. If "/Game" is not included, the starting "/" is optional.
	// Spaces in paths are DISALLOWED (it will currently break recursion, but there's no guarantee that
	// will be consistent behaviour going forward).
	// A single map is differentiated from a directory by the inclusion of "/" at the end of the path.
	FString CorrectedPath;
	if (!InPath.StartsWith(AssetPathGameDirName))
	{
		CorrectedPath = AssetPathGameDirName;
	}
	CorrectedPath.PathAppend(*InPath, InPath.Len());

	// We rely on IsValidLongPackageName to differentiate between a map file and "anything else" -- the only accepted
	// format of which is a directory path.
	if (FPackageName::IsValidLongPackageName(CorrectedPath))
	{
		// Single Map
		FString MapPathToLoad = CorrectedPath;
		if (!InPath.Contains(TEXT("/")))
		{
			// We assume that a lack of '/' in InPath means the user is specifying only a map's name, which will need to be searched for
			FString LongPackageName;
			FString Filename;
			if (!FPackageName::SearchForPackageOnDisk(*InPath, &LongPackageName, &Filename))
			{
				UE_LOG(LogSpatialGDKEditorCommandlet, Error, TEXT("Could not find map matching pattern %s"), *InPath);
				return false;
			}
			MapPathToLoad = LongPackageName;
		}
		UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Selecting direct map %s"), *MapPathToLoad);
		return GenerateSnapshotForMap(InSpatialGDKEditor, MapPathToLoad);
	}
	else if (CorrectedPath.EndsWith(TEXT("/")))
	{
		// Whole Directory
		UObjectLibrary* ObjectLibrary = UObjectLibrary::CreateLibrary(UWorld::StaticClass(), false, true);

		// Convert InPath into a format acceptable by LoadAssetDataFromPath().
		FString DirPath = CorrectedPath.LeftChop(1); // Remove the final '/' character

		ObjectLibrary->LoadAssetDataFromPath(DirPath);

		TArray<FAssetData> AssetDatas;
		ObjectLibrary->GetAssetDataList(AssetDatas);
		UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Found %d maps in %s"), AssetDatas.Num(), *InPath);

		for (FAssetData& AssetData : AssetDatas)
		{
			FString MapPath = AssetData.PackageName.ToString();
			UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Selecting map %s"), *MapPath);
			if (!GenerateSnapshotForMap(InSpatialGDKEditor, MapPath))
			{
				return false;
			}
		}
	}
	else
	{
		FString CorrectedLongPackageNameError;
		FString Dummy;
		FPackageName::TryConvertFilenameToLongPackageName(CorrectedPath, Dummy, &CorrectedLongPackageNameError);
		UE_LOG(LogSpatialGDKEditorCommandlet, Error, TEXT("Requested path \"%s\" is not in the expected format. %s"), *InPath,
			   *CorrectedLongPackageNameError);
		return false; // Future-proofing
	}

	return true;
}

bool UGenerateSchemaAndSnapshotsCommandlet::GenerateSnapshotForMap(FSpatialGDKEditor& InSpatialGDKEditor, const FString& InMapName)
{
	// Check if this map path has already been generated and early exit if so
	if (GeneratedMapPaths.Contains(InMapName))
	{
		UE_LOG(LogSpatialGDKEditorCommandlet, Warning, TEXT("Map %s has already been generated against. Skipping duplicate generation."),
			   *InMapName);
		return true;
	}
	GeneratedMapPaths.Add(InMapName);

	// Load persistent Level (this will load over any previously loaded levels)
	if (!FEditorFileUtils::LoadMap(InMapName)) // This loads the world into GWorld
	{
		UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Failed to load map %s"), *InMapName);
		return false;
	}

	// Ensure all sub-levels are also loaded
	const TArray<ULevelStreaming*> StreamingLevels = GWorld->GetStreamingLevels();
	UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Loading %d Streaming SubLevels"), StreamingLevels.Num());
	for (ULevelStreaming* StreamingLevel : StreamingLevels)
	{
		FLatentActionInfo LatentInfo;
		UGameplayStatics::LoadStreamLevel(GWorld, StreamingLevel->GetWorldAssetPackageFName(), false, true, LatentInfo);
	}

	// Ensure all world composition tiles are also loaded
	if (GWorld->WorldComposition != nullptr)
	{
		TArray<ULevelStreaming*> StreamingTiles = GWorld->WorldComposition->TilesStreaming;
		UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Loading %d World Composition Tiles"), StreamingTiles.Num());
		for (ULevelStreaming* StreamingTile : StreamingTiles)
		{
			FLatentActionInfo LatentInfo;
			UGameplayStatics::LoadStreamLevel(GWorld, StreamingTile->GetWorldAssetPackageFName(), false, true, LatentInfo);
		}
	}

	// Generate Snapshot
	if (!GenerateSnapshotForLoadedMap(InSpatialGDKEditor, FPaths::GetCleanFilename(InMapName)))
	{
		return false;
	}

	return true;
}

bool UGenerateSchemaAndSnapshotsCommandlet::GenerateSchema(FSpatialGDKEditor& InSpatialGDKEditor)
{
	UE_LOG(
		LogSpatialGDKEditorCommandlet, Error,
		TEXT("Commandlet GenerateSchemaAndSnapshots without -SkipSchema has been deprecated in favor of CookAndGenerateSchemaCommandlet."));

	return false;
}

bool UGenerateSchemaAndSnapshotsCommandlet::GenerateSnapshotForLoadedMap(FSpatialGDKEditor& InSpatialGDKEditor, const FString& MapName)
{
	// Generate the Snapshot!
	bool bSnapshotGenSuccess = false;
	InSpatialGDKEditor.GenerateSnapshot(GWorld, FPaths::SetExtension(FPaths::GetCleanFilename(MapName), TEXT(".snapshot")),
										FSimpleDelegate::CreateLambda([&bSnapshotGenSuccess]() {
											UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Snapshot Generation Completed!"));
											bSnapshotGenSuccess = true;
										}),
										FSimpleDelegate::CreateLambda([]() {
											UE_LOG(LogSpatialGDKEditorCommandlet, Display, TEXT("Snapshot Generation Failed"));
										}),
										FSpatialGDKEditorErrorHandler::CreateLambda([](FString ErrorText) {
											UE_LOG(LogSpatialGDKEditorCommandlet, Error, TEXT("%s"), *ErrorText);
										}));
	return bSnapshotGenSuccess;
}
