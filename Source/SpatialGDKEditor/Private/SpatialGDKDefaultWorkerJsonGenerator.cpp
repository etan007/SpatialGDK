// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "SpatialGDKDefaultWorkerJsonGenerator.h"

#include "SpatialGDKServicesConstants.h"
#include "SpatialGDKSettings.h"

#include "Misc/FileHelper.h"

DEFINE_LOG_CATEGORY(LogSpatialGDKDefaultWorkerJsonGenerator);
#define LOCTEXT_NAMESPACE "SpatialGDKDefaultWorkerJsonGenerator"

bool GenerateDefaultWorkerJson(const FString& JsonPath, bool& bOutRedeployRequired)
{
	const FString TemplateWorkerJsonPath =
		FSpatialGDKServicesModule::GetSpatialGDKPluginDirectory(TEXT("Extras/templates/WorkerJsonTemplate.json"));

	FString Contents;
	if (FFileHelper::LoadFileToString(Contents, *TemplateWorkerJsonPath))
	{
		if (FFileHelper::SaveStringToFile(Contents, *JsonPath))
		{
			bOutRedeployRequired = true;
			UE_LOG(LogSpatialGDKDefaultWorkerJsonGenerator, Verbose, TEXT("Wrote default worker json to %s"), *JsonPath)

			return true;
		}
		else
		{
			UE_LOG(LogSpatialGDKDefaultWorkerJsonGenerator, Error, TEXT("Failed to write default worker json to %s"), *JsonPath)
		}
	}
	else
	{
		UE_LOG(LogSpatialGDKDefaultWorkerJsonGenerator, Error, TEXT("Failed to read default worker json template at %s"),
			   *TemplateWorkerJsonPath)
	}

	return false;
}

bool GenerateAllDefaultWorkerJsons(bool& bOutRedeployRequired)
{
	const FString WorkerJsonDir = FPaths::Combine(SpatialGDKServicesConstants::SpatialOSDirectory, TEXT("workers/unreal"));
	bool bAllJsonsGeneratedSuccessfully = true;

	if (const USpatialGDKSettings* SpatialGDKSettings = GetDefault<USpatialGDKSettings>())
	{
		const FName WorkerTypes[] = { SpatialConstants::DefaultServerWorkerType, SpatialConstants::RoutingWorkerType,
									  SpatialConstants::StrategyWorkerType };
		for (auto Worker : WorkerTypes)
		{
			FString JsonPath = FPaths::Combine(WorkerJsonDir, FString::Printf(TEXT("spatialos.%s.worker.json"), *Worker.ToString()));
			if (!FPaths::FileExists(JsonPath))
			{
				UE_LOG(LogSpatialGDKDefaultWorkerJsonGenerator, Verbose, TEXT("Could not find worker json at %s"), *JsonPath);

				if (!GenerateDefaultWorkerJson(JsonPath, bOutRedeployRequired))
				{
					bAllJsonsGeneratedSuccessfully = false;
				}
			}
		}

		return bAllJsonsGeneratedSuccessfully;
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
