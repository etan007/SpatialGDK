// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"

#include "Schema/Component.h"
#include "SpatialConstants.h"
#include "Utils/SchemaUtils.h"

#include <WorkerSDK/improbable/c_schema.h>
#include <WorkerSDK/improbable/c_worker.h>

namespace SpatialGDK
{
struct SpawnData : AbstractMutableComponent
{
	static const Worker_ComponentId ComponentId = SpatialConstants::SPAWN_DATA_COMPONENT_ID;

	SpawnData() = default;

	SpawnData(AActor* Actor)
	{
		const USceneComponent* RootComponent = Actor->GetRootComponent();

		Location = RootComponent ? FRepMovement::RebaseOntoZeroOrigin(Actor->GetActorLocation(), Actor) : FVector::ZeroVector;
		Rotation = RootComponent ? Actor->GetActorRotation() : FRotator::ZeroRotator;
		Scale = RootComponent ? Actor->GetActorScale() : FVector::OneVector;
		Velocity = RootComponent ? Actor->GetVelocity() : FVector::ZeroVector;
	}

	explicit SpawnData(const Worker_ComponentData& Data)
		: SpawnData(Data.schema_type)
	{
	}

	explicit SpawnData(Schema_ComponentData* Data)
	{
		Schema_Object* ComponentObject = Schema_GetComponentDataFields(Data);

		Location = GetVectorFromSchema(ComponentObject, 2);
		Rotation = GetRotatorFromSchema(ComponentObject, 3);
		Scale = GetVectorFromSchema(ComponentObject, 4);
		Velocity = GetVectorFromSchema(ComponentObject, 5);
	}

	Worker_ComponentData CreateComponentData() const override
	{
		Worker_ComponentData Data = {};
		Data.component_id = ComponentId;
		Data.schema_type = Schema_CreateComponentData(ComponentId);
		Schema_Object* ComponentObject = Schema_GetComponentDataFields(Data.schema_type);

		AddVectorToSchema(ComponentObject, 2, Location);
		AddRotatorToSchema(ComponentObject, 3, Rotation);
		AddVectorToSchema(ComponentObject, 4, Scale);
		AddVectorToSchema(ComponentObject, 5, Velocity);

		return Data;
	}

	FVector Location;
	FRotator Rotation;
	FVector Scale;
	FVector Velocity;
};

} // namespace SpatialGDK
