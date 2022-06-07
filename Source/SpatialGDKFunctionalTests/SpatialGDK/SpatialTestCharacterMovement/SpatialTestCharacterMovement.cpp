// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "SpatialTestCharacterMovement.h"
#include "Components/BoxComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Math/Plane.h"
#include "SpatialFunctionalTestFlowController.h"
#include "SpatialGDKFunctionalTests/SpatialGDK/TestActors/TestMovementCharacter.h"

/**
 * This test tests if the movement of a character from a starting point to a Destination, performed on a client, is correctly replicated on
 *the server and on all other clients. This test requires the CharacterMovementTestGameMode, trying to run this test on a different game
 *mode will fail.
 *
 * The test includes a single server and two client workers. The client workers begin with a PlayerController and a TestCharacterMovement
 *
 * The flow is as follows:
 * - Setup:
 *    - The server checks if the clients received a TestCharacterMovement and sets their position to (0.0f, 0.0f, 50.0f) for the first
 *client and (100.0f, 300.0f, 50.0f) for the second.
 *    - The client with ID 1 moves its character as an autonomous proxy towards the Destination.
 *  - Test:
 *     - The owning client asserts that his character has reached the Destination.
 *     - The server asserts that client's 1 character has reached the Destination on the server.
 *     - The second client checks that client's 1 character has reached the Destination.
 */

ASpatialTestCharacterMovement::ASpatialTestCharacterMovement()
	: Super()
{
	Author = "Andrei";
	Description = TEXT("Test Character Movement");
}

bool ASpatialTestCharacterMovement::HasCharacterReachedDestination(ATestMovementCharacter* PlayerCharacter,
																   const FPlane& DestinationPlane) const
{
	// Checks if the character has passed the plane
	return DestinationPlane.PlaneDot(PlayerCharacter->GetActorLocation()) > 0;
}

void ASpatialTestCharacterMovement::PrepareTest()
{
	Super::PrepareTest();

	FVector Origin = FVector(0.0f, 0.0f, 50.0f);
	FVector Destination = FVector(232.0f, 0.0f, 50.0f);

	// Create a plane at the destination for testing against
	FVector Direction = Destination - Origin;
	Direction.Normalize();
	FPlane DestinationPlane(Destination, Direction);

	// The server checks if the clients received a TestCharacterMovement and moves them to the mentioned locations
	AddStep(TEXT("SpatialTestCharacterMovementServerSetupStep"), FWorkerDefinition::Server(1), nullptr, [this]() {
		for (ASpatialFunctionalTestFlowController* FlowController : GetFlowControllers())
		{
			if (FlowController->WorkerDefinition.Type == ESpatialFunctionalTestWorkerType::Server)
			{
				continue;
			}

			AController* PlayerController = Cast<AController>(FlowController->GetOwner());
			ATestMovementCharacter* PlayerCharacter = Cast<ATestMovementCharacter>(PlayerController->GetPawn());

			checkf(PlayerCharacter, TEXT("Client did not receive a TestMovementCharacter"));

			int FlowControllerId = FlowController->WorkerDefinition.Id;

			if (FlowControllerId == 1)
			{
				PlayerCharacter->SetActorLocation(FVector(0.0f, 0.0f, 50.0f));
			}
			else
			{
				PlayerCharacter->SetActorLocation(FVector(100.0f + 100 * FlowControllerId, 300.0f, 50.0f));
			}
		}

		FinishStep();
	});

	// Client 1 moves his character and asserts that it reached the Destination locally.
	AddStep(
		TEXT("SpatialTestCharacterMovementClient1Move"), FWorkerDefinition::Client(1),
		[this]() -> bool {
			AController* PlayerController = Cast<AController>(GetLocalFlowController()->GetOwner());
			ATestMovementCharacter* PlayerCharacter = Cast<ATestMovementCharacter>(PlayerController->GetPawn());

			// Since the character is simulating gravity, it will drop from the original position close to (0, 0, 40), depending on the size
			// of the CapsuleComponent in the TestMovementCharacter. However, depending on physics is not good for tests, so I'm
			// changing this test to only compare Z (height) coordinate.
			return IsValid(PlayerCharacter) && FMath::IsNearlyEqual(PlayerCharacter->GetActorLocation().Z, 40.0f, 2.0f);
		},
		nullptr,
		[this, DestinationPlane](float DeltaTime) {
			AController* PlayerController = Cast<AController>(GetLocalFlowController()->GetOwner());
			ATestMovementCharacter* PlayerCharacter = Cast<ATestMovementCharacter>(PlayerController->GetPawn());

			PlayerCharacter->AddMovementInput(FVector(1, 0, 0), 1.0f);

			RequireTrue(HasCharacterReachedDestination(PlayerCharacter, DestinationPlane),
						TEXT("Player character has reached the destination on the autonomous proxy."));
			FinishStep();
		},
		10.0f);

	// Server asserts that the character of client 1 has reached the Destination.
	AddStep(
		TEXT("SpatialTestChracterMovementServerCheckMovementVisibility"), FWorkerDefinition::Server(1), nullptr, nullptr,
		[this, DestinationPlane](float DeltaTime) {
			for (ASpatialFunctionalTestFlowController* FlowController : GetFlowControllers())
			{
				if (FlowController->WorkerDefinition.Type == ESpatialFunctionalTestWorkerType::Server)
				{
					continue;
				}

				AController* PlayerController = Cast<AController>(FlowController->GetOwner());
				ATestMovementCharacter* PlayerCharacter = Cast<ATestMovementCharacter>(PlayerController->GetPawn());

				if (FlowController->WorkerDefinition.Id == 1)
				{
					RequireTrue(HasCharacterReachedDestination(PlayerCharacter, DestinationPlane),
								TEXT("Player character has reached the destination on the server."));
					FinishStep();
				}
			}
		},

		5.0f);

	// Client 2 asserts that the character of client 1 has reached the Destination.
	AddStep(
		TEXT("SpatialTestCharacterMovementClient2CheckMovementVisibility"), FWorkerDefinition::Client(2), nullptr, nullptr,
		[this, DestinationPlane](float DeltaTime) {
			AController* Client2PlayerController = Cast<AController>(GetLocalFlowController()->GetOwner());
			ATestMovementCharacter* Client2PlayerCharacter = Cast<ATestMovementCharacter>(Client2PlayerController->GetPawn());

			TArray<AActor*> FoundActors;
			UGameplayStatics::GetAllActorsOfClass(GetWorld(), ATestMovementCharacter::StaticClass(), FoundActors);

			for (AActor* PlayerCharacter : FoundActors)
			{
				if (PlayerCharacter == Client2PlayerCharacter)
				{
					// Ignore the player character that client 2 controls
					continue;
				}

				RequireTrue(HasCharacterReachedDestination(Cast<ATestMovementCharacter>(PlayerCharacter), DestinationPlane),
							TEXT("Player character has reached the destination on the simulated proxy"));
				FinishStep();
			}
		},
		5.0f);
}
