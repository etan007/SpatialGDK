// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "MultipleOwnershipPawn.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "Net/UnrealNetwork.h"

AMultipleOwnershipPawn::AMultipleOwnershipPawn()
{
	bReplicates = true;
 
	SetReplicatingMovement(true);
 
	SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneComponent"));
	RootComponent = SceneComponent;

	CubeComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CubeComponent"));
	CubeComponent->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("StaticMesh'/Engine/BasicShapes/Cube.Cube'")));
	CubeComponent->SetMaterial(0,
							   LoadObject<UMaterial>(nullptr, TEXT("Material'/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial'")));
	CubeComponent->SetVisibility(true);
	CubeComponent->SetupAttachment(RootComponent);

	ReceivedRPCs = 0;
}

void AMultipleOwnershipPawn::ServerSendRPC_Implementation()
{
	++ReceivedRPCs;
}

void AMultipleOwnershipPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMultipleOwnershipPawn, ReceivedRPCs);
}
