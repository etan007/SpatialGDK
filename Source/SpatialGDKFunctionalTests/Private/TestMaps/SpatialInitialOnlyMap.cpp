// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "TestMaps/SpatialInitialOnlyMap.h"
#include "EngineClasses/SpatialWorldSettings.h"
#include "SpatialGDKFunctionalTests/SpatialGDK/SpatialTestInitialOnly/SpatialTestInitialOnlyForInterestActor.h"
#include "SpatialGDKFunctionalTests/SpatialGDK/SpatialTestInitialOnly/SpatialTestInitialOnlyForInterestActorWithUpdatedValue.h"
#include "SpatialGDKFunctionalTests/SpatialGDK/SpatialTestInitialOnly/SpatialTestInitialOnlyForSpawnActor.h"
#include "SpatialGDKFunctionalTests/SpatialGDK/SpatialTestInitialOnly/SpatialTestInitialOnlyForSpawnComponents.h"

USpatialInitialOnlyMap::USpatialInitialOnlyMap()
	: UGeneratedTestMap(EMapCategory::CI_PREMERGE, TEXT("SpatialInitialOnlyMap"))
{
	// clang-format off
	SetCustomConfig(TEXT("[/Script/SpatialGDK.SpatialGDKSettings]") LINE_TERMINATOR
					TEXT("bEnableInitialOnlyReplicationCondition=True") LINE_TERMINATOR
					TEXT("PositionUpdateThresholdMaxCentimeters=0"));
	// clang-format on
}

void USpatialInitialOnlyMap::CreateCustomContentForMap()
{
	ULevel* CurrentLevel = World->GetCurrentLevel();

	FVector TestActorPosition(0.f, 0.f, 0.f);

	// Add tests
	AddActorToLevel<ASpatialTestInitialOnlyForInterestActor>(CurrentLevel, FTransform(TestActorPosition));
	AddActorToLevel<ASpatialTestInitialOnlyForInterestActorWithUpdatedValue>(CurrentLevel, FTransform(TestActorPosition));
	AddActorToLevel<ASpatialTestInitialOnlyForSpawnActor>(CurrentLevel, FTransform(TestActorPosition));
	AddActorToLevel<ASpatialTestInitialOnlyForSpawnComponents>(CurrentLevel, FTransform(TestActorPosition));
}
