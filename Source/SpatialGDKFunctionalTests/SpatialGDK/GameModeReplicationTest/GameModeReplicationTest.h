// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "SpatialCommonTypes.h"
#include "SpatialFunctionalTest.h"

#include "GameFramework/GameModeBase.h"

#include "LoadBalancing/SpatialMultiWorkerSettings.h"

#include "GameModeReplicationTest.generated.h"

UCLASS(HideDropdown)
class UGameModeReplicationGridLBStrategy : public UGridBasedLBStrategy
{
public:
	GENERATED_BODY()

	UGameModeReplicationGridLBStrategy()
	{
		// 3 rows makes sure GameMode is in authority area for only one of the workers
		Rows = 3;
		Cols = 1;
		// 0 interest inflation means only one worker will have interest in the GameMode
		InterestBorder = 0.f;
	}
};

UCLASS(HideDropdown)
class SPATIALGDKFUNCTIONALTESTS_API UGameModeReplicationMultiWorkerSettings : public USpatialMultiWorkerSettings
{
public:
	GENERATED_BODY()

	static TArray<FLayerInfo> GetLayerSetup()
	{
		const FLayerInfo GridLayer(TEXT("Grid"), { AActor::StaticClass() }, UGameModeReplicationGridLBStrategy::StaticClass());

		return { GridLayer };
	}

	UGameModeReplicationMultiWorkerSettings() { WorkerLayers.Append(GetLayerSetup()); }
};

UCLASS(HideDropdown)
class SPATIALGDKFUNCTIONALTESTS_API AGameModeReplicationTestGameMode : public AGameModeBase
{
public:
	GENERATED_BODY()

	AGameModeReplicationTestGameMode();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	constexpr static int StartingValue = 0;

	constexpr static int UpdatedValue = 500;

	UPROPERTY(Replicated, Transient)
	int ReplicatedValue = StartingValue;
};

UCLASS()
class SPATIALGDKFUNCTIONALTESTS_API AGameModeReplicationTest : public ASpatialFunctionalTest
{
	GENERATED_BODY()

public:
	AGameModeReplicationTest();

	UFUNCTION(CrossServer, Reliable)
	void MarkWorkerGameModeAuthority(bool bHasGameModeAuthority);

	virtual void PrepareTest() override;

	int AuthorityServersCount = 0;

	int ServerResponsesCount = 0;

	float TimeWaited = 0.0f;
};
