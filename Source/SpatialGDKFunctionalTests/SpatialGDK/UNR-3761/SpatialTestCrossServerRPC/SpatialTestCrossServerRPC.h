// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "SpatialFunctionalTest.h"
#include "SpatialTestCrossServerRPC.generated.h"

class ACrossServerRPCCube;
class ANonReplicatedCrossServerRPCCube;

UCLASS()
class SPATIALGDKFUNCTIONALTESTS_API ASpatialTestCrossServerRPC : public ASpatialFunctionalTest
{
	GENERATED_BODY()

public:
	ASpatialTestCrossServerRPC();

	UPROPERTY(EditAnywhere, Category = "Default")
	ANonReplicatedCrossServerRPCCube* LevelCube;

	virtual void PrepareTest() override;

	void CheckInvalidEntityID(ACrossServerRPCCube* TestCube);
	void CheckValidEntityID(ACrossServerRPCCube* TestCube);
};
