// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "SpatialTestNetReference.h"
#include "CubeWithReferences.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "SpatialFunctionalTestFlowController.h"
#include "SpatialGDKFunctionalTests/SpatialGDK/TestActors/TestMovementCharacter.h"
#include "SpatialGDKSettings.h"

/**
 * This test automates the Net Reference Test gym, which tested that references to replicated actors are stable when actors go in and out of
 *relevance. This test also adds an interest check on top of the previously mentioned Gym. NOTE: The test also includes support for visual
 *debugging. If desired, it is suggested to comment the line that is updating the PositionUpdateFrequency before trying to visually debug
 *the test.
 *
 * The test includes a single server and two client workers. For performance considerations, the only client that is executing the test is
 *Client 1. The flow is as follows:
 * - Setup:
 *   - The Server spawns 4 CubeWithReferences objects and sets up their references.
 * - Test:
 *	 - The test contains 2 runs of the same flow:
 *			1) The Server moves the character of Client 1 to 4 specific locations
 *			2) After arriving at each location on the Client, the test checks that:
 *				2.1) The correct amount of cubes are present in the world, based on the default NetCullDistanceSquared of the
 *PlayerController. 2.2) The references to the replicated actors are correct.
 * - Clean-up:
 *	- The previously spawned CubeWithReferences and TestMovementCharacter are destroyed
 */

ASpatialTestNetReference::ASpatialTestNetReference()
	: Super()
{
	Author = "Andrei";
	Description = TEXT("Test Net Reference");

	// The test locations are specifically set so that the specified number of cubes are visible, based on the default
	// NetCullDistanceSquared. To be more specific, in this setup, a cube will be visible if the distance from it to the PlayerCharacter is
	// less than 15000 units.
	TestLocations.Add(TPair<FVector, int>(FVector(0.0f, -15000.0f, 40.0f), 1));
	TestLocations.Add(TPair<FVector, int>(FVector(5000.0f, -5000.0f, 40.0f), 2));
	TestLocations.Add(TPair<FVector, int>(FVector(5000.0f, 1000.0f, 40.0f), 3));
	TestLocations.Add(TPair<FVector, int>(FVector(100.0f, 100.0f, 40.0f), 4));

	/* Uncomment these lines, together with the line in the SpatialTestNetReferenceServerMove step related to the camera movement to enable
	visual debugging. However, note that uncommenting these lines will make the test fail if running with Native Unreal networking
	// The camera relative locations are set so that the camera is always at the location (8500.0f, 13000.0f, 40.f), in order to have all 4
	possible cubes in its view for ease of visual debugging

	CameraRelativeLocations.Add(FVector(8500.0f, 28000.0f, 0.0f));
	CameraRelativeLocations.Add(FVector(3500.0f, 18000.0f, 0.0f));
	CameraRelativeLocations.Add(FVector(3500.0f, 12000.0f, 0.0f));
	CameraRelativeLocations.Add(FVector(8400.0f, 12900.0f, 0.0f));

	CameraRelativeRotation = FRotator::MakeFromEuler(FVector(0.0f, 0.0f, 240.0f));
	*/
}

void ASpatialTestNetReference::PrepareTest()
{
	Super::PrepareTest();

	AddStep(TEXT("SpatialTestNetReferenceServerSetup"), FWorkerDefinition::Server(1), nullptr, [this]() {
		// Set up the cubes' spawn locations
		TArray<FVector> CubeLocations;
		CubeLocations.Add(FVector(0.0f, -11000.0f, 40.0f));
		CubeLocations.Add(FVector(11000.0f, 0.0f, 40.0f));
		CubeLocations.Add(FVector(0.0f, 11000.0f, 40.0f));
		CubeLocations.Add(FVector(-11000.0f, 0.0f, 40.0f));

		// Spawn the cubes
		TArray<ACubeWithReferences*> TestCubes;
		int NumberOfCubes = CubeLocations.Num();

		for (int i = 0; i < NumberOfCubes; ++i)
		{
			ACubeWithReferences* CubeWithReferences =
				GetWorld()->SpawnActor<ACubeWithReferences>(CubeLocations[i], FRotator::ZeroRotator, FActorSpawnParameters());

			// Cubes are scaled so that they can be seen by the camera, used for easing visual debugging
			CubeWithReferences->SetActorScale3D(FVector(10.0f, 30.0f, 30.0f));

			TestCubes.Add(CubeWithReferences);

			RegisterAutoDestroyActor(CubeWithReferences);
		}

		// Set the cubes' references
		for (int i = 0; i < NumberOfCubes; ++i)
		{
			TestCubes[i]->Neighbour1 = TestCubes[(i + 1) % NumberOfCubes];
			TestCubes[i]->Neighbour2 = TestCubes[(i + NumberOfCubes - 1) % NumberOfCubes];
		}

		// Spawn the TestMovementCharacter actor for Client 1 to possess.
		ASpatialFunctionalTestFlowController* FlowController = GetFlowController(ESpatialFunctionalTestWorkerType::Client, 1);
		ATestMovementCharacter* TestCharacter =
			GetWorld()->SpawnActor<ATestMovementCharacter>(FVector::ZeroVector, FRotator::ZeroRotator, FActorSpawnParameters());
		APlayerController* PlayerController = Cast<APlayerController>(FlowController->GetOwner());

		// Set a reference to the previous Pawn so that it can be possessed back in the last step of the test
		OriginalPawn = TPair<AController*, APawn*>(PlayerController, PlayerController->GetPawn());

		RegisterAutoDestroyActor(TestCharacter);
		PlayerController->Possess(TestCharacter);

		FinishStep();
	});

	for (int i = 0; i < 2 * TestLocations.Num(); ++i)
	{
		// The mod is required since the test goes over each test location twice
		int CurrentMoveIndex = i % TestLocations.Num();

		AddStep(TEXT("SpatialTestNetReferenceServerMove"), FWorkerDefinition::Server(1), nullptr, [this, CurrentMoveIndex]() {
			ASpatialFunctionalTestFlowController* FlowController = GetFlowController(ESpatialFunctionalTestWorkerType::Client, 1);
			APlayerController* PlayerController = Cast<APlayerController>(FlowController->GetOwner());
			ATestMovementCharacter* PlayerCharacter = Cast<ATestMovementCharacter>(PlayerController->GetPawn());

			// Move the character to the correct location
			PlayerCharacter->SetActorLocation(TestLocations[CurrentMoveIndex].Key);

			/* Uncomment this line to allow for visual debugging, together with the lines in the constructor.
			   However, note that uncommenting these lines will make the test fail if running with Native Unreal networking

			// Update the camera location for visual debugging
			PlayerCharacter->UpdateCameraLocationAndRotation(CameraRelativeLocations[CurrentMoveIndex], CameraRelativeRotation);
			*/

			FinishStep();
		});

		AddStep(
			TEXT("SpatialTestNetReferenceClientCheckMovement"), FWorkerDefinition::Client(1), nullptr, nullptr,
			[this, CurrentMoveIndex](float DeltaTime) {
				AController* PlayerController = Cast<AController>(GetLocalFlowController()->GetOwner());
				ATestMovementCharacter* PlayerCharacter = Cast<ATestMovementCharacter>(PlayerController->GetPawn());

				if (PlayerCharacter != nullptr && PlayerCharacter->GetActorLocation().Equals(TestLocations[CurrentMoveIndex].Key, 1.0f))
				{
					FinishStep();
				}
			},
			10.0f);

		AddStep(
			TEXT("SpatialTestNetReferenceClientCheckNumberOfReferences"), FWorkerDefinition::Client(1), nullptr, nullptr,
			[this, CurrentMoveIndex](float DeltaTime) {
				TArray<AActor*> CubesWithReferences;
				UGameplayStatics::GetAllActorsOfClass(GetWorld(), ACubeWithReferences::StaticClass(), CubesWithReferences);

				bool bHasCorrectNumberOfCubes = CubesWithReferences.Num() == TestLocations[CurrentMoveIndex].Value;

				if (bHasCorrectNumberOfCubes)
				{
					FinishStep();
				}
			},
			10.0f);

		AddStep(
			TEXT("SpatialTestNetReferenceClientCheckReferences"), FWorkerDefinition::Client(1), nullptr, nullptr,
			[this, CurrentMoveIndex](float DeltaTime) {
				TArray<AActor*> CubesWithReferences;
				UGameplayStatics::GetAllActorsOfClass(GetWorld(), ACubeWithReferences::StaticClass(), CubesWithReferences);

				bool bHasCorrectReferences = true;

				for (AActor* ArrayObject : CubesWithReferences)
				{
					ACubeWithReferences* CurrentCube = Cast<ACubeWithReferences>(ArrayObject);
					FVector CurrentCubeLocation = CurrentCube->GetActorLocation();

					int ExpectedValidReferences = 0;

					for (AActor* OtherObject : CubesWithReferences)
					{
						ACubeWithReferences* OtherCube = Cast<ACubeWithReferences>(OtherObject);
						FVector OtherCubeLocation = OtherObject->GetActorLocation();

						// If the cube is the current one or the diagonally opposed one, then ignore it as it should never be a neigbhour of
						// the current cube
						if (OtherCubeLocation.Equals(CurrentCubeLocation)
							|| (FMath::IsNearlyEqual(OtherCubeLocation.X, -CurrentCubeLocation.X)
								&& FMath::IsNearlyEqual(OtherCubeLocation.Y, -CurrentCubeLocation.Y)))
						{
							continue;
						}

						// Check that the current cube has a neighbour reference to this OtherCube
						bHasCorrectReferences &= (CurrentCube->Neighbour1 == OtherCube) || (CurrentCube->Neighbour2 == OtherCube);

						if (bHasCorrectReferences)
						{
							ExpectedValidReferences++;
						}
					}

					if (ExpectedValidReferences == 0)
					{
						// Check that the current cube has 0 valid references
						bHasCorrectReferences &= !IsValid(CurrentCube->Neighbour1) && !IsValid(CurrentCube->Neighbour2);
					}
					else if (ExpectedValidReferences == 1)
					{
						// We have previously checked that one neighbour reference is correctly pointing to the neighbour cube, also check
						// that the other reference is null
						bHasCorrectReferences &= !IsValid(CurrentCube->Neighbour1) || !IsValid(CurrentCube->Neighbour2);
					}
				}

				if (bHasCorrectReferences)
				{
					FinishStep();
				}
			},
			15.0f);
	}

	AddStep(TEXT("SpatialTestNetReferenceServerCleanup"), FWorkerDefinition::Server(1), nullptr, [this]() {
		// Possess the original pawn, so that other tests start from the expected, default set-up
		OriginalPawn.Key->Possess(OriginalPawn.Value);

		FinishStep();
	});
}

USpatialTestNetReferenceMap::USpatialTestNetReferenceMap()
	: UGeneratedTestMap(EMapCategory::CI_PREMERGE, TEXT("SpatialTestNetReferenceMap"))
{
}

void USpatialTestNetReferenceMap::CreateCustomContentForMap()
{
	ULevel* CurrentLevel = World->GetCurrentLevel();

	// Add the test
	AddActorToLevel<ASpatialTestNetReference>(CurrentLevel, FTransform::Identity);
}
