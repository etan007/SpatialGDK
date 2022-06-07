// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "SpatialTestReplicatedStartupActor.h"

#include "ReplicatedStartupActor.h"
#include "ReplicatedStartupActorGameMode.h"
#include "ReplicatedStartupActorPlayerController.h"
#include "SpatialFunctionalTestFlowController.h"

#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"

/**
 * This test automates the ReplicatedStartupActor gym. The gym was used to support QA test case "C1944 Replicated startup actors are
 * correctly spawned on all clients". The test also covers the QA work-flow "Startup actors correctly replicate arbitrary properties".
 * NOTE: 1. This test requires a specific Map with a ReplicatedStartupActor placed on the map and in the interest of the players and a
 * custom GameMode and PlayerController, trying to run this test on a different Map will make it fail.
 *
 * The test contains two main phases:
 * - Common Setup:
 *  - Each worker sets a reference to the ReplicatedStartupActor.
 * - Phase 1:
 *  - Test:
 *   - Each client sends a server RPC from the ReplicatedStartupActor.
 *   - Each client tests that the server has a valid reference to its ReplicatedStartupActor.
 *
 * - Phase 2:
 *  - Test:
 *   - The server sets some default values for the replicated properties whilst the ReplicatedStartupActor is in view of the clients.
 *   - All workers check that the properties were replicated correctly.
 *   - The server moves the ReplicatedStartupActor out of view.
 *   - All workers check the movement is visible.
 *   - The server updates the replicated properties and moves the ReplicatedStartupActor back into the view of the clients.
 *   - All workers check that the ReplicatedStartupActor is in view and all its replicated properties were replicated correctly.
 * - Common Clean-up:
 *  - None.
 */

ASpatialTestReplicatedStartupActor::ASpatialTestReplicatedStartupActor()
	: Super()
{
	Author = "Andrei";
	Description = TEXT("Test Replicated Startup Actor Reference And Property Replication");
}

void ASpatialTestReplicatedStartupActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASpatialTestReplicatedStartupActor, bIsValidReference);
}

void ASpatialTestReplicatedStartupActor::PrepareTest()
{
	Super::PrepareTest();

	// Common Setup

	// All workers set a reference to the ReplicatedStartupActor.
	AddStep(
		TEXT("SpatialTestReplicatedStartupActorUniversalReferenceSetup"), FWorkerDefinition::AllWorkers, nullptr, nullptr,
		[this](float DeltaTime) {
			TArray<AActor*> ReplicatedStartupActors;
			UGameplayStatics::GetAllActorsOfClass(GetWorld(), AReplicatedStartupActor::StaticClass(), ReplicatedStartupActors);

			if (ReplicatedStartupActors.Num() == 1)
			{
				ReplicatedStartupActor = Cast<AReplicatedStartupActor>(ReplicatedStartupActors[0]);

				// Reset the variables to allow for relevant consecutive runs of the same test.
				ASpatialFunctionalTestFlowController* FlowController = GetLocalFlowController();
				if (FlowController->WorkerDefinition.Type == ESpatialFunctionalTestWorkerType::Client)
				{
					AReplicatedStartupActorPlayerController* PlayerController =
						Cast<AReplicatedStartupActorPlayerController>(FlowController->GetOwner());
					PlayerController->ResetBoolean(this);
				}
				else
				{
					bIsValidReference = false;
				}

				FinishStep();
			}
		},
		5.0f);

	// Phase 1

	// All clients send a server RPC from the ReplicatedStartupActor.
	AddStep(
		TEXT("SpatialTestReplicatedStartupActorClientsSendRPC"), FWorkerDefinition::AllClients, nullptr, nullptr,
		[this](float DeltaTime) {
			AReplicatedStartupActorPlayerController* PlayerController =
				Cast<AReplicatedStartupActorPlayerController>(GetLocalFlowController()->GetOwner());

			// Make sure that the PlayerController has been set before trying to do anything with it, this might prevent Null Pointer
			// exceptions being thrown when UE ticks at a relatively slow rate
			if (IsValid(PlayerController))
			{
				PlayerController->ClientToServerRPC(this, ReplicatedStartupActor);
				FinishStep();
			}
		},
		5.0f);

	// All clients check that the RPC was received and correctly applied.
	AddStep(
		TEXT("SpatialTestReplicatedStarupActorClientsCheckRPC"), FWorkerDefinition::AllClients, nullptr, nullptr,
		[this](float DeltaTime) {
			RequireTrue(bIsValidReference, TEXT("Reference should be valid."));
			FinishStep();
		},
		5.0f);

	// Phase 2

	// The server sets default values for the replicated properties.
	AddStep(TEXT("SpatialTestReplicatedStartupActorServerSetDefaultProperties"), FWorkerDefinition::Server(1), nullptr, [this]() {
		ReplicatedStartupActor->TestIntProperty = 1;

		ReplicatedStartupActor->TestArrayProperty.Empty();
		ReplicatedStartupActor->TestArrayProperty.Add(1);

		ReplicatedStartupActor->TestArrayStructProperty.Empty();
		ReplicatedStartupActor->TestArrayStructProperty.Add(FTestStruct{ 1 });

		FinishStep();
	});

	// All workers check that the properties were replicated correctly.
	AddStep(
		TEXT("SpatialTestReplicatedStartupActorAllWorkersCheckDefaultProperties"), FWorkerDefinition::AllWorkers, nullptr, nullptr,
		[this](float DeltaTime) {
			RequireEqual_Int(ReplicatedStartupActor->TestIntProperty, 1, TEXT("TestInt should be correct after server update."));
			if (RequireEqual_Int(ReplicatedStartupActor->TestArrayProperty.Num(), 1,
								 TEXT("TestArrayProperty size should be correct after server update.")))
			{
				RequireEqual_Int(ReplicatedStartupActor->TestArrayProperty[0], 1,
								 TEXT("TestArrayProperty[0] should be correct after server update."));
			}
			if (RequireEqual_Int(ReplicatedStartupActor->TestArrayStructProperty.Num(), 1,
								 TEXT("TestArrayProperty size should be correct after server update.")))
			{
				RequireEqual_Int(ReplicatedStartupActor->TestArrayStructProperty[0].Int, 1,
								 TEXT("TestArrayStructProperty[0] should be correct after server update."));
			}
			FinishStep();
		},
		5.0f);

	// The server moves the ReplicatedStartupActor out of the clients' view.
	AddStep(TEXT("SpatialTestReplicatedStartupActorServerMoveActorOutOfView"), FWorkerDefinition::Server(1), nullptr, [this]() {
		ReplicatedStartupActor->SetActorLocation(FVector(15000.0f, 15000.0f, 50.0f));

		FinishStep();
	});

	// All workers check that the movement is visible.
	AddStep(
		TEXT("SpatialTestReplicatedStartupActorAllWorkersCheckMovement"), FWorkerDefinition::AllWorkers, nullptr, nullptr,
		[this](float DeltaTime) {
			// Make sure the Actor was moved out of view of the clients before updating its properties
			// TODO: UNR-4305, we should have the if condition after this ticket is completed
			// if (ReplicatedStartupActor->GetActorLocation().Equals(FVector(15000.0f, 15000.0f, 50.0f), 1))
			{
				FinishStep();
			}
		},
		5.0f);

	// The server updates the replicated properties whilst the ReplicatedStartupActor is out of the clients' view.
	AddStep(TEXT("SpatialTestReplicatedStartupActorServerUpdateProperties"), FWorkerDefinition::Server(1), nullptr, [this]() {
		ReplicatedStartupActor->TestIntProperty = 0;

		ReplicatedStartupActor->TestArrayProperty.Empty();

		ReplicatedStartupActor->TestArrayStructProperty.Empty();

		ReplicatedStartupActor->SetActorLocation(FVector(250.0f, -250.0f, 50.0f));

		FinishStep();
	});

	// All workers check that the ReplicatedStartupActor is back in view and that properties were replicated correctly.
	AddStep(
		TEXT("SpatialTestReplicatedStartupActorAllWorkersCheckModifiedProperties"), FWorkerDefinition::AllWorkers, nullptr, nullptr,
		[this](float DeltaTime) {
			RequireTrue(ReplicatedStartupActor->GetActorLocation().Equals(FVector(250.0f, -250.0f, 50.0f), 1),
						TEXT("ReplicatedStartupActor should have moved after server update."));
			RequireEqual_Int(ReplicatedStartupActor->TestIntProperty, 0, TEXT("TestInt should be correct after server update."));
			RequireEqual_Int(ReplicatedStartupActor->TestArrayProperty.Num(), 0,
							 TEXT("TestArrayProperty size should be correct after server update."));
			RequireEqual_Int(ReplicatedStartupActor->TestArrayStructProperty.Num(), 0,
							 TEXT("TestArrayProperty size should be correct after server update."));

			FinishStep();
		},
		5.0f);
}

USpatialTestReplicatedStartupActorMap::USpatialTestReplicatedStartupActorMap()
	: UGeneratedTestMap(EMapCategory::CI_PREMERGE, TEXT("ReplicatedStartupActorMap"))
{
}

void USpatialTestReplicatedStartupActorMap::CreateCustomContentForMap()
{
	ULevel* CurrentLevel = World->GetCurrentLevel();

	// Add the test
	AddActorToLevel<ASpatialTestReplicatedStartupActor>(CurrentLevel, FTransform::Identity);

	// Add the test helper - startup actor placed in the level
	AddActorToLevel<AReplicatedStartupActor>(CurrentLevel, FTransform::Identity);

	AWorldSettings* WorldSettings = World->GetWorldSettings();
	WorldSettings->DefaultGameMode = AReplicatedStartupActorGameMode::StaticClass();
}
