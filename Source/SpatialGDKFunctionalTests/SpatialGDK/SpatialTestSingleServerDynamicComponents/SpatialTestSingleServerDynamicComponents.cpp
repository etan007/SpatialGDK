// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "SpatialTestSingleServerDynamicComponents.h"

#include "SpatialGDKSettings.h"
#include "TestDynamicComponent.h"
#include "TestDynamicComponentActor.h"

#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"

/**
 * This test tests dynamic component creation, attachment, removal and replication of properties in a single-server context.
 *
 * The test includes 1 Server and 2 Clients.
 * The flow is as follows:
 * - Setup:
 *  - The TestActor is spawned and a dynamic component is immediately created and attached to it.
 *  - The TestActor by itself attaches another dynamic component as part of ATestDynamicComponentActor::PostInitializeComponents.
 *  - After one second, the Server creates and attaches one more dynamic component to the TestActor.
 *  - All the components have a replicated array that contains references to the TestActor and to the test itself.
 * - Test:
 *	- The Clients check that the dynamic components exist and that the replicated references are correct.
 *  - The Server removes the dynamic components from the TestActor.
 *  - The Clients check that the components were properly removed.
 *  - The Server creates and attaches 2 more dynamic components to the TestActor.
 *  - The Clients check that the newly attached components exist and they correctly replicate the references.
 * - Clean-up:
 *	- The TestActor is destroyed.
 */
ASpatialTestSingleServerDynamicComponents::ASpatialTestSingleServerDynamicComponents()
	: Super()
{
	Author = "Miron + Andrei";
	Description = TEXT("Test Dynamic Component Replication in a Single Server Context");
}

void ASpatialTestSingleServerDynamicComponents::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASpatialTestSingleServerDynamicComponents, TestActor);
}

void ASpatialTestSingleServerDynamicComponents::PrepareTest()
{
	Super::PrepareTest();

	bInitialOnlyEnabled = GetDefault<USpatialGDKSettings>()->bEnableInitialOnlyReplicationCondition;
	bSpatialEnabled = GetDefault<UGeneralProjectSettings>()->UsesSpatialNetworking();

	// The Server spawns the TestActor and immediately after it creates and attaches the OnSpawnComponent.
	AddStep(TEXT("SpatialTestSingleServerDynamicComponentsServerSpawnTestActor"), FWorkerDefinition::Server(1), nullptr, [this]() {
		if (bInitialOnlyEnabled && bSpatialEnabled)
		{
			AddExpectedLogError(TEXT("Dynamic component using InitialOnly data. This data will not be sent."), 5, false);
		}

		TestActor = GetWorld()->SpawnActor<ATestDynamicComponentActor>(ActorSpawnPosition, FRotator::ZeroRotator, FActorSpawnParameters());
		TestActor->OnSpawnComponent = CreateAndAttachTestDynamicComponentToActor(TestActor, TEXT("OnSpawnDynamicComponent1"));
		TestActor->OnSpawnComponent->OwnerOnlyReplicatedVar = 101;
		TestActor->OnSpawnComponent->InitialOnlyReplicatedVar = 102;
		TestActor->OnSpawnComponent->HandoverReplicatedVar = 103;

		FinishStep();
	});

	// After a second, the Server sets the references of the PostInitializeComponent and creates and attaches the LateAddedComponent.
	AddStep(
		TEXT("SpatialTestSingleServerDynamicComponentsServerAddDynamicComponentsAndReferences"), FWorkerDefinition::Server(1),
		[this]() -> bool {
			return GetWorld()->GetTimeSeconds() - TestActor->CreationTime >= 1.0f;
		},
		[this]() {
			// Make sure the PostInitializeComponent was created and it does not have any reference at this stage.
			if (TestActor->PostInitializeComponent == nullptr || TestActor->PostInitializeComponent->ReferencesArray.Num() != 0)
			{
				FinishTest(EFunctionalTestResult::Failed, TEXT("The PostInitializedComponent was not created correctly!"));
				return;
			}

			// Set the references for the PostInitializeComponent which is created from
			// ATestDynamicComponentActor::PostInitializeComponents.
			TestActor->PostInitializeComponent->ReferencesArray.Add(TestActor);
			TestActor->PostInitializeComponent->ReferencesArray.Add(this);

			// Create and attach the LateAddedComponent.
			TestActor->LateAddedComponent = CreateAndAttachTestDynamicComponentToActor(TestActor, TEXT("LateAddedDynamicComponent1"));
			TestActor->LateAddedComponent->OwnerOnlyReplicatedVar = 201;
			TestActor->LateAddedComponent->InitialOnlyReplicatedVar = 202;
			TestActor->LateAddedComponent->HandoverReplicatedVar = 203;

			FinishStep();
		});

	// The Clients check if they have correctly received the TestActor, its components and the references array of the components.
	AddStep(
		TEXT("SpatialTestSingleServerDynamicComponentsClientCheck"), FWorkerDefinition::AllClients,
		[this]() -> bool {
			// Make sure we have the received the TestActor and its replicated components before checking their references.
			return TestActor != nullptr && TestActor->OnSpawnComponent != nullptr && TestActor->PostInitializeComponent != nullptr
				   && TestActor->LateAddedComponent != nullptr;
		},
		[this]() {
			// At this point the Actor and its replicated components were received, therefore the references can be checked.

			// Check the references for the OnSpawnComponent
			AssertTrue(TestActor->OnSpawnComponent->ReferencesArray[0] == TestActor,
					   TEXT("Reference from the on-spawn dynamic component to its parent works."));
			AssertTrue(TestActor->OnSpawnComponent->ReferencesArray[1] == this,
					   TEXT("Reference from the on-spawn dynamic component to the test works."));
			AssertTrue(TestActor->OnSpawnComponent->OwnerOnlyReplicatedVar == 0,
					   TEXT("Owner only property should not have been replicated yet, as the owner hasn't been set."));
			AssertTrue(TestActor->OnSpawnComponent->InitialOnlyReplicatedVar == ((bInitialOnlyEnabled && bSpatialEnabled) ? 0 : 102),
					   TEXT("Initial only property should have been replicated by now, unless spatial InitialOnly is enabled."));
			AssertTrue(TestActor->OnSpawnComponent->HandoverReplicatedVar == 0,
					   TEXT("Handover property should not have been replicated to clients."));

			// Check the references for the PostInitializeComponent
			AssertTrue(TestActor->PostInitializeComponent->ReferencesArray[0] == TestActor,
					   TEXT("Reference from the post-init dynamic component to its parent works."));
			AssertTrue(TestActor->PostInitializeComponent->ReferencesArray[1] == this,
					   TEXT("Reference from the post-init dynamic component to the test works."));

			// Check the references for the LateAddedComponent
			AssertTrue(TestActor->LateAddedComponent->ReferencesArray[0] == TestActor,
					   TEXT("Reference from the late-created dynamic component to its parent works."));
			AssertTrue(TestActor->LateAddedComponent->ReferencesArray[1] == this,
					   TEXT("Reference from the late-created dynamic component to the test works."));
			AssertTrue(TestActor->LateAddedComponent->OwnerOnlyReplicatedVar == 0,
					   TEXT("Owner only property should not have been replicated yet, as the owner hasn't been set."));
			// Seems like native Unreal will NOT send the initial only property on the late added component, presumably because it does not
			// come in an initial bunch
			AssertTrue(TestActor->LateAddedComponent->InitialOnlyReplicatedVar == ((bInitialOnlyEnabled || !bSpatialEnabled) ? 0 : 202),
					   TEXT("Initial only property should not have been replicated, unless running with Spatial without proper InitialOnly "
							"support."));
			AssertTrue(TestActor->LateAddedComponent->HandoverReplicatedVar == 0,
					   TEXT("Handover property should not have been replicated to clients."));

			FinishStep();
		},
		nullptr, 5.0f);

	// The Server destroys all the components of the TestActor.
	AddStep(TEXT("SpatialTestSingleServerDynamicComponentsServerRemoveDynamiComponents"), FWorkerDefinition::Server(1), nullptr, [this]() {
		TestActor->OnSpawnComponent->DestroyComponent();
		TestActor->PostInitializeComponent->DestroyComponent();
		TestActor->LateAddedComponent->DestroyComponent();
		TestActor->OnSpawnComponent = nullptr;
		TestActor->PostInitializeComponent = nullptr;
		TestActor->LateAddedComponent = nullptr;

		FinishStep();
	});

	// The Clients check if the components were correctly destroyed.
	AddStep(
		TEXT("SpatialTestSingleServerDynamicComponentsClientCheckDynamicComponentsRemoved"), FWorkerDefinition::AllClients, nullptr,
		nullptr,
		[this](float DeltaTime) {
			if (TestActor->GetComponents().Num() == 0)
			{
				if (TestActor->OnSpawnComponent == nullptr && TestActor->PostInitializeComponent == nullptr
					&& TestActor->LateAddedComponent == nullptr)
				{
					FinishStep();
				}
			}
		},
		5.0f);

	// The Server creates two components and adds them to the TestActor, using the existing replicated properties.
	AddStep(TEXT("SpatialTestSingleServerDynamicComponentsServerReCreateComponents"), FWorkerDefinition::Server(1), nullptr, [this]() {
		TestActor->OnSpawnComponent = CreateAndAttachTestDynamicComponentToActor(TestActor, TEXT("OnSpawnDynamicComponent2"));
		TestActor->OnSpawnComponent->ReferencesArray.SetNum(4);
		TestActor->OnSpawnComponent->ReferencesArray[2] = this;
		TestActor->OnSpawnComponent->ReferencesArray[3] = TestActor;
		TestActor->OnSpawnComponent->OwnerOnlyReplicatedVar = 301;
		TestActor->OnSpawnComponent->InitialOnlyReplicatedVar = 302;
		TestActor->OnSpawnComponent->HandoverReplicatedVar = 303;

		TestActor->LateAddedComponent = CreateAndAttachTestDynamicComponentToActor(TestActor, TEXT("LateAddedDynamicComponent2"));
		TestActor->LateAddedComponent->ReferencesArray.SetNum(4);
		TestActor->LateAddedComponent->ReferencesArray[2] = TestActor;
		TestActor->LateAddedComponent->ReferencesArray[3] = this;
		TestActor->LateAddedComponent->OwnerOnlyReplicatedVar = 401;
		TestActor->LateAddedComponent->InitialOnlyReplicatedVar = 402;
		TestActor->LateAddedComponent->HandoverReplicatedVar = 403;

		FinishStep();
	});

	// The Clients check that the components were correctly replicated.
	AddStep(
		TEXT("SpatialTestSingleServerDynamicComponentsClientCheckDynamicComponentsReCreated"), FWorkerDefinition::AllClients,
		[this]() -> bool {
			return TestActor != nullptr && TestActor->OnSpawnComponent != nullptr && TestActor->PostInitializeComponent == nullptr
				   && TestActor->LateAddedComponent != nullptr;
		},
		[this]() {
			AssertTrue(TestActor->OnSpawnComponent->ReferencesArray[2] == this,
					   TEXT("Reference from the on-spawn dynamic component to the test works after swapping."));
			AssertTrue(TestActor->OnSpawnComponent->ReferencesArray[3] == TestActor,
					   TEXT("Reference from the on-spawn dynamic component to its parent works after swapping."));
			AssertTrue(TestActor->OnSpawnComponent->OwnerOnlyReplicatedVar == 0,
					   TEXT("Owner only property should not have been replicated yet, as the owner hasn't been set."));
			// Native Unreal will NOT send the initial only property, since this was a component added dynamically (and late) to an actor
			AssertTrue(TestActor->OnSpawnComponent->InitialOnlyReplicatedVar == ((bInitialOnlyEnabled || !bSpatialEnabled) ? 0 : 302),
					   TEXT("Initial only property should not have been replicated, unless running with Spatial without proper InitialOnly "
							"support."));
			AssertTrue(TestActor->OnSpawnComponent->HandoverReplicatedVar == 0,
					   TEXT("Handover property should not have been replicated to clients."));

			AssertTrue(TestActor->LateAddedComponent->ReferencesArray[2] == TestActor,
					   TEXT("Reference from the late-created dynamic component to its parent works."));
			AssertTrue(TestActor->LateAddedComponent->ReferencesArray[3] == this,
					   TEXT("Reference from the late-created dynamic component to the test works."));
			AssertTrue(TestActor->LateAddedComponent->OwnerOnlyReplicatedVar == 0,
					   TEXT("Owner only property should not have been replicated yet, as the owner hasn't been set."));
			// Native Unreal will NOT send the initial only property, since this was a component added dynamically (and late) to an actor
			AssertTrue(TestActor->LateAddedComponent->InitialOnlyReplicatedVar == ((bInitialOnlyEnabled || !bSpatialEnabled) ? 0 : 402),
					   TEXT("Initial only property should not have been replicated, unless running with Spatial without proper InitialOnly "
							"support."));
			AssertTrue(TestActor->LateAddedComponent->HandoverReplicatedVar == 0,
					   TEXT("Handover property should not have been replicated to clients."));

			FinishStep();
		},
		nullptr, 5.0f);

	// Since calling RegisterAutoDestroy adds a component to the Actor, the clean-up is done manually.
	AddStep(TEXT("SpatialTestSingleServerDynamicComponentsServerCleanup"), FWorkerDefinition::Server(1), nullptr, [this]() {
		RegisterAutoDestroyActor(TestActor);

		FinishStep();
	});
}

// Helper function that creates and attaches a UTestDynamicComponent to the TestActor and also sets the component's references accordingly.
UTestDynamicComponent* ASpatialTestSingleServerDynamicComponents::CreateAndAttachTestDynamicComponentToActor(AActor* Actor, FName Name)
{
	UTestDynamicComponent* NewDynamicComponent =
		NewObject<UTestDynamicComponent>(Actor, UTestDynamicComponent::StaticClass(), Name, RF_Transient);

	NewDynamicComponent->SetupAttachment(Actor->GetRootComponent());
	NewDynamicComponent->RegisterComponent();
	NewDynamicComponent->ReferencesArray.Add(Actor);
	NewDynamicComponent->ReferencesArray.Add(this);

	return NewDynamicComponent;
}
