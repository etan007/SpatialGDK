// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpatialFunctionalTestStep.h"
#include "SpatialFunctionalTestFlowController.generated.h"

namespace
{
constexpr int INVALID_FLOW_CONTROLLER_ID = 0;
}

class ASpatialFunctionalTest;

UCLASS(SpatialType = NotPersistent)
class SPATIALGDKFUNCTIONALTESTS_API ASpatialFunctionalTestFlowController : public AActor
{
	GENERATED_BODY()

public:
	ASpatialFunctionalTestFlowController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void BeginPlay() override;

	virtual void OnAuthorityGained() override;

	virtual void Tick(float DeltaSeconds) override;

	// Convenience function to know if this FlowController is locally owned
	bool IsLocalController() const;

	// # Testing APIs

	// Locally triggers StepIndex Test Step to start
	UFUNCTION(CrossServer, Reliable)
	void CrossServerStartStep(int StepIndex);

	// Tells Test owner that the current Step is finished locally
	void NotifyStepFinished(const int StepIndex);

	// Tell the Test owner that we want to end the Test
	void NotifyFinishTest(EFunctionalTestResult TestResult, const FString& Message);

	UPROPERTY(ReplicatedUsing = OnRep_OwningTest)
	ASpatialFunctionalTest* OwningTest;

	// Holds WorkerType and WorkerId. Type should be only Server or Client, and Id >= 1 (after registered)
	// The Client WorkerId will be given out in the order they connect; the Server one matches its VirtualWorkerId
	UPROPERTY(Replicated)
	FWorkerDefinition WorkerDefinition;

	// Prettier way to display type+id combo since it can be quite useful
	const FString GetDisplayName() const;

	// When Test is finished, this gets triggered. It's mostly important for when a Test was failed during runtime
	void OnTestFinished();

	// Marks the Flow Controller to be ready or not for the test to start, which means that PrepareTest()
	// has been called locally on the OwningTest.
	UFUNCTION()
	void SetReadyToRunTest(bool bIsReady);

	// Returns if the data regarding the FlowControllers has been replicated PrepareTest() has run on locally on the OwningTest.
	bool IsReadyToRunTest() { return WorkerDefinition.Id != INVALID_FLOW_CONTROLLER_ID && bIsReadyToRunTest; }

	// Each server worker will assign local client ids, this function will be used by
	// the Test owner server worker to guarantee they are all unique
	UFUNCTION(CrossServer, Reliable)
	void CrossServerSetWorkerId(int NewWorkerId);

	UFUNCTION(BlueprintPure, Category = "Spatial Functional Test")
	FWorkerDefinition GetWorkerDefinition() { return WorkerDefinition; }

	// Let's you know if the owning worker has acknowledged the FinishTest flow.
	bool HasAckFinishedTest() const { return bHasAckFinishedTest; }

	UFUNCTION()
	void DeregisterFlowController();

private:
	// Current Step being executed
	SpatialFunctionalTestStep CurrentStep;

	UPROPERTY(ReplicatedUsing = OnReadyToRegisterWithTest)
	uint8 bReadyToRegisterWithTest : 1;

	UPROPERTY(Replicated)
	bool bIsReadyToRunTest;

	UPROPERTY(Replicated)
	bool bHasAckFinishedTest;

	UFUNCTION()
	void OnReadyToRegisterWithTest();

	UFUNCTION()
	void OnRep_OwningTest();

	void TryRegisterFlowControllerWithOwningTest();

	UFUNCTION(Server, Reliable)
	void ServerSetReadyToRunTest(bool bIsReady);

	UFUNCTION(Client, Reliable)
	void ClientStartStep(int StepIndex);

	void StartStepInternal(const int StepIndex);

	void StopStepInternal();

	UFUNCTION(Server, Reliable)
	void ServerNotifyStepFinished(const int StepIndex);

	UFUNCTION(CrossServer, Reliable)
	void CrossServerNotifyStepFinished(const int StepIndex);

	UFUNCTION(Server, Reliable)
	void ServerNotifyFinishTest(EFunctionalTestResult TestResult, const FString& Message);

	void ServerNotifyFinishTestInternal(EFunctionalTestResult TestResult, const FString& Message);

	UFUNCTION(Server, Reliable)
	void ServerAckFinishedTest();
};
