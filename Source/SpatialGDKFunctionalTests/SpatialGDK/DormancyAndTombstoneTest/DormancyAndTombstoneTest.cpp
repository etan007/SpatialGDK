// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "DormancyAndTombstoneTest.h"
#include "DormancyTestActor.h"
#include "EngineUtils.h"

/**
 * This test tests dormancy and tombstoning of bNetLoadOnClient actors placed in the level.
 *
 * The test includes a single server and two client workers. The client workers begin with a player controller and their default pawns,
 * which they initially possess. The test also REQUIRES the presence of a ADormancyTestActor (this actor is initially dormant) in the level
 * where it is placed. The flow is as follows:
 *  - Setup:
 *    - (Refer to above about placing instructions).
 *  - Test:
 *    - The server sets the dormant actor's TestIntProp to 1 (in C++, so dormancy isn't changed, as it would be with blueprints).
 *    - The client verifies that locally it is still set to 0.
 *    - The server deletes the dormant actor.
 *    - The clients check that the actor has been deleted in their local world.
 *  - Cleanup:
 *    - No cleanup required, as the actor is deleted as part of the test. Note that the actor exists in the world if other tests are run
 * before this one.
 *    - Note that this test cannot be rerun, as it relies on an actor placed in the level being deleted as part of the test.
 */
ADormancyAndTombstoneTest::ADormancyAndTombstoneTest()
{
	Author = "Miron";
	Description = TEXT("Test Actor Dormancy and Tombstones");
}

void ADormancyAndTombstoneTest::PrepareTest()
{
	Super::PrepareTest();

	{ // Step 1 - Set TestIntProp to 1.
		AddStep(TEXT("ServerSetTestIntPropTo1"), FWorkerDefinition::Server(1), nullptr, [this]() {
			int Counter = 0;
			int ExpectedDormancyActors = 1;
			for (TActorIterator<ADormancyTestActor> Iter(GetWorld()); Iter; ++Iter)
			{
				Counter++;
				RequireEqual_Int(Iter->NetDormancy, DORM_Initial, TEXT("Dormancy on ADormancyTestActor (should be DORM_Initial)"));
				Iter->TestIntProp = 1;
			}
			RequireEqual_Int(Counter, ExpectedDormancyActors, TEXT("Number of TestDormancyActors in the server world"));

			FinishStep();
		});
	}
	{ // Step 2 - Observe TestIntProp on client should still be 0.
		AddStep(
			TEXT("ClientCheckValue"), FWorkerDefinition::AllClients, nullptr, nullptr,
			[this](float DeltaTime) {
				int Counter = 0;
				int ExpectedDormancyActors = 1;
				for (TActorIterator<ADormancyTestActor> Iter(GetWorld()); Iter; ++Iter)
				{
					if (Iter->TestIntProp == 0 && Iter->NetDormancy == DORM_Initial)
					{
						Counter++;
					}
				}

				RequireEqual_Int(Counter, ExpectedDormancyActors, TEXT("Number of TestDormancyActors in client world"));

				FinishStep();
			},
			5.0f);
	}

	{ // Step 3 - Delete the test actor on the server.
		AddStep(TEXT("ServerDeleteActor"), FWorkerDefinition::Server(1), nullptr, [this]() {
			int Counter = 0;
			int ExpectedDormancyActors = 1;
			for (TActorIterator<ADormancyTestActor> Iter(GetWorld()); Iter; ++Iter)
			{
				Counter++;
				Iter->Destroy();
			}
			RequireEqual_Int(Counter, ExpectedDormancyActors, TEXT("Number of TestDormancyActors in the server world"));

			FinishStep();
		});
	}

	{ // Step 4 - Observe the test actor has been deleted on the client.
		AddStep(
			TEXT("ClientCheckActorDestroyed"), FWorkerDefinition::AllClients, nullptr, nullptr,
			[this](float DeltaTime) {
				int Counter = 0;
				int ExpectedDormancyActors = 0;
				for (TActorIterator<ADormancyTestActor> Iter(GetWorld()); Iter; ++Iter)
				{
					Counter++;
				}

				RequireEqual_Int(Counter, ExpectedDormancyActors, TEXT("Number of TestDormancyActors in client world"));

				FinishStep();
			},
			5.0f);
	}
}
