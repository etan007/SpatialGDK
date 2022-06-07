// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "TestMaps/SpatialComponentMap.h"
#include "EngineClasses/SpatialWorldSettings.h"
#include "GameFramework/PlayerStart.h"
#include "SpatialGDKFunctionalTests/SpatialGDK/SpatialAuthorityTest/SpatialAuthorityTestGameMode.h"
#include "SpatialGDKFunctionalTests/SpatialGDK/SpatialComponentTest/SpatialComponentSettingsOverride.h"
#include "SpatialGDKFunctionalTests/SpatialGDK/SpatialComponentTest/SpatialComponentTest.h"
#include "SpatialGDKFunctionalTests/SpatialGDK/SpatialComponentTest/SpatialComponentTestActor.h"
#include "SpatialGDKFunctionalTests/SpatialGDK/SpatialComponentTest/SpatialComponentTestReplicatedActor.h"
#include "TestWorkerSettings.h"

USpatialComponentMap::USpatialComponentMap()
	: UGeneratedTestMap(EMapCategory::CI_PREMERGE, TEXT("SpatialComponentMap"))
{
	SetNumberOfClients(2);
}

void USpatialComponentMap::CreateCustomContentForMap()
{
	ULevel* CurrentLevel = World->GetCurrentLevel();

	// The actors are placed in one quadrant of the map to make sure they are LBed together
	FVector SpatialComponentTestActorPosition = FVector(-250, -250, 0);

	// Add the tests
	ASpatialComponentTest& CompTest = AddActorToLevel<ASpatialComponentTest>(CurrentLevel, FTransform(SpatialComponentTestActorPosition));
	ASpatialComponentSettingsOverride& SettingsOverrideTest =
		AddActorToLevel<ASpatialComponentSettingsOverride>(CurrentLevel, FTransform(SpatialComponentTestActorPosition));

	// Add the helpers, as we need things placed in the level
	CompTest.LevelActor = &AddActorToLevel<ASpatialComponentTestActor>(CurrentLevel, FTransform(SpatialComponentTestActorPosition));
	CompTest.LevelReplicatedActor =
		&AddActorToLevel<ASpatialComponentTestReplicatedActor>(CurrentLevel, FTransform(SpatialComponentTestActorPosition));

	// Quirk of the test. We need the player spawns on the same portion of the map as the test, so they are LBed together
	AActor** PlayerStart = CurrentLevel->Actors.FindByPredicate([](AActor* Actor) {
		return Actor->GetClass() == APlayerStart::StaticClass();
	});
	(*PlayerStart)->SetActorLocation(FVector(-500, -250, 100));

	ASpatialWorldSettings* WorldSettings = CastChecked<ASpatialWorldSettings>(World->GetWorldSettings());
	WorldSettings->SetMultiWorkerSettingsClass(UTest1x2FullInterestWorkerSettings::StaticClass());
	WorldSettings->DefaultGameMode = ASpatialAuthorityTestGameMode::StaticClass();
}
