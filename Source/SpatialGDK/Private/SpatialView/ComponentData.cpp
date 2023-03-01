// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "SpatialView/ComponentData.h"
#include "SpatialView/ComponentUpdate.h"

namespace SpatialGDK
{
ComponentData::ComponentData(Worker_ComponentId Id)
	: ComponentId(Id)
	, Data(Schema_CreateComponentData(Id))
{
}

ComponentData::ComponentData(OwningComponentDataPtr Data, Worker_ComponentId Id)
	: ComponentId(Id)
	, Data(MoveTemp(Data))
{
}

ComponentData ComponentData::CreateCopy(const Schema_ComponentData* Data, Worker_ComponentId Id)
{
	return ComponentData(OwningComponentDataPtr(Schema_CopyComponentData(Data)), Id);
}

ComponentData ComponentData::DeepCopy() const
{
	check(Data.IsValid());
	return CreateCopy(Data.Get(), ComponentId);
}

Schema_ComponentData* ComponentData::Release() &&
{
	check(Data.IsValid());
	return Data.Release();
}

bool ComponentData::ApplyUpdate(const ComponentUpdate& Update)
{
	check(Update.GetComponentId() == GetComponentId());
	check(Update.GetUnderlying() != nullptr);
	std::string funstr = GWorld->GetWorld()->IsServer()?"Server:ApplyUpdate":"Client:ApplyUpdate";
	const bool bUpdateResult = Schema_ApplyComponentUpdateToData(Update.GetUnderlying(), Data.Get(),funstr.c_str()) != 0;
	// Copy the component to prevent unbounded memory growth from appending the update to it.
	Data = OwningComponentDataPtr(Schema_CopyComponentData(Data.Get()));
	return bUpdateResult;
}

Schema_Object* ComponentData::GetFields() const
{
	check(Data.IsValid());
	return Schema_GetComponentDataFields(Data.Get());
}

Schema_ComponentData* ComponentData::GetUnderlying() const
{
	check(Data.IsValid());
	return Data.Get();
}

Worker_ComponentData ComponentData::GetWorkerComponentData() const
{
	check(Data.IsValid());
	return { nullptr, ComponentId, Data.Get(), nullptr };
}

Worker_ComponentId ComponentData::GetComponentId() const
{
	return ComponentId;
}

} // namespace SpatialGDK
