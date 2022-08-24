// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "Schema/Component.h"
#include "SpatialConstants.h"

#include <WorkerSDK/improbable/c_schema.h>
#include <WorkerSDK/improbable/c_worker.h>

namespace SpatialGDK
{
struct GameplayDebuggerComponent
{
	static const Worker_ComponentId ComponentId = SpatialConstants::GDK_GAMEPLAY_DEBUGGER_COMPONENT_ID;

	GameplayDebuggerComponent() {}

	explicit GameplayDebuggerComponent(Schema_ComponentData& Data)
	{
		Schema_Object* ComponentObject = Schema_GetComponentDataFields(&Data);
		ReadFromSchema(ComponentObject);
	}

	Worker_ComponentData CreateComponent() const
	{
		Worker_ComponentData Data = {};
		Data.component_id = ComponentId;
		Data.schema_type = Schema_CreateComponentData(ComponentId);
		Schema_Object* ComponentObject = Schema_GetComponentDataFields(Data.schema_type);
		WriteToSchema(ComponentObject);
		return Data;
	}

	Worker_ComponentUpdate CreateComponentUpdate() const
	{
		Worker_ComponentUpdate Data = {};
		Data.component_id = ComponentId;
		Data.schema_type = Schema_CreateComponentUpdate(ComponentId);
		Schema_Object* ComponentObject = Schema_GetComponentUpdateFields(Data.schema_type);
		WriteToSchema(ComponentObject);

		return Data;
	}

	void ApplyComponentUpdate(Schema_ComponentUpdate* Update)
	{
		Schema_Object* ComponentObject = Schema_GetComponentUpdateFields(Update);
		ReadFromSchema(ComponentObject);
	}

	VirtualWorkerId DelegatedVirtualWorkerId = 0;
	bool bTrackPlayer = false;

private:
	void ReadFromSchema(Schema_Object* ComponentObject)
	{
		DelegatedVirtualWorkerId = Schema_GetInt32(ComponentObject, 1);
		bTrackPlayer = !!Schema_GetBool(ComponentObject, 2);
	}

	void WriteToSchema(Schema_Object* ComponentObject) const
	{
		Schema_AddInt32(ComponentObject, 2, DelegatedVirtualWorkerId);
		Schema_AddBool(ComponentObject, 3, bTrackPlayer);
	}
};

} // namespace SpatialGDK
