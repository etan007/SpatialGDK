// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "Schema/Component.h"
#include "SpatialCommonTypes.h"
#include "Utils/SchemaUtils.h"

#include <WorkerSDK/improbable/c_schema.h>

namespace SpatialGDK
{
// The AuthorityIntent component is a piece of the Zoning solution for the UnrealGDK. For each
// entity in SpatialOS, Unreal will use the AuthorityIntent to indicate which Unreal server worker
// should be authoritative for the entity. No Unreal worker should write to an entity if the
// VirtualWorkerId set here doesn't match the worker's Id.
struct AuthorityIntent : AbstractMutableComponent
{
	static const Worker_ComponentId ComponentId = SpatialConstants::AUTHORITY_INTENT_COMPONENT_ID;

	AuthorityIntent()
		: VirtualWorkerId(SpatialConstants::AUTHORITY_INTENT_VIRTUAL_WORKER_ID)
	{
	}

	AuthorityIntent(VirtualWorkerId InVirtualWorkerId)
		: VirtualWorkerId(InVirtualWorkerId)
	{
	}

	AuthorityIntent(const Worker_ComponentData& Data)
		: AuthorityIntent(Data.schema_type)
	{
	}

	AuthorityIntent(Schema_ComponentData* Data)
	{
		Schema_Object* ComponentObject = Schema_GetComponentDataFields(Data);

		VirtualWorkerId = Schema_GetUint32(ComponentObject, SpatialConstants::AUTHORITY_INTENT_VIRTUAL_WORKER_ID);
	}

	Worker_ComponentData CreateComponentData() const override
	{
		Worker_ComponentData Data = {};
		Data.component_id = ComponentId;
		Data.schema_type = Schema_CreateComponentData();
		Schema_Object* ComponentObject = Schema_GetComponentDataFields(Data.schema_type);

		Schema_AddUint32(ComponentObject, SpatialConstants::AUTHORITY_INTENT_VIRTUAL_WORKER_ID, VirtualWorkerId);

		return Data;
	}

	Worker_ComponentUpdate CreateAuthorityIntentUpdate()
	{
		Worker_ComponentUpdate Update = {};
		Update.component_id = ComponentId;
		Update.schema_type = Schema_CreateComponentUpdate();
		Schema_Object* ComponentObject = Schema_GetComponentUpdateFields(Update.schema_type);

		Schema_AddUint32(ComponentObject, SpatialConstants::AUTHORITY_INTENT_VIRTUAL_WORKER_ID, VirtualWorkerId);

		return Update;
	}

	void ApplyComponentUpdate(const Worker_ComponentUpdate& Update) { ApplyComponentUpdate(Update.schema_type); }

	void ApplyComponentUpdate(Schema_ComponentUpdate* Update)
	{
		Schema_Object* ComponentObject = Schema_GetComponentUpdateFields(Update);
		VirtualWorkerId = Schema_GetUint32(ComponentObject, SpatialConstants::AUTHORITY_INTENT_VIRTUAL_WORKER_ID);
	}

	// Id of the Unreal server worker which should be authoritative for the entity.
	// 0 is reserved as an invalid/unset value.
	VirtualWorkerId VirtualWorkerId;
};

} // namespace SpatialGDK
