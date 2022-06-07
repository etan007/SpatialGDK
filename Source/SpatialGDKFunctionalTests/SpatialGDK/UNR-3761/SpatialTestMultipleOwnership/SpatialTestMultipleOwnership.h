// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "SpatialFunctionalTest.h"
#include "SpatialTestMultipleOwnership.generated.h"

class AMultipleOwnershipPawn;
UCLASS()
class SPATIALGDKFUNCTIONALTESTS_API ASpatialTestMultipleOwnership : public ASpatialFunctionalTest
{
	GENERATED_BODY()

public:
	ASpatialTestMultipleOwnership();

	virtual void PrepareTest() override;

	// Helper array used to avoid code duplication by storing the references to the MultipleOwnershipPawns on the test itself, instead of
	// calling GetAllActorsOfClass multiple times.
	TArray<AMultipleOwnershipPawn*> MultipleOwnershipPawns;

	// Helper map to store what the original pawns were before we started possessing different ones, so we can restore them at the end of
	// the test.
	UPROPERTY()
	TMap<AController*, APawn*> OriginalPossessedPawns;
};
