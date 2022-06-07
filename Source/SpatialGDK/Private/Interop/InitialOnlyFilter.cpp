// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "Interop/InitialOnlyFilter.h"

#include "Interop/Connection/SpatialWorkerConnection.h"
#include "Interop/SpatialOSDispatcherInterface.h"
#include "Interop/SpatialReceiver.h"

DEFINE_LOG_CATEGORY(LogInitialOnlyFilter);

namespace SpatialGDK
{
InitialOnlyFilter::InitialOnlyFilter(USpatialWorkerConnection& InConnection)
	: Connection(InConnection)
{
}

bool InitialOnlyFilter::HasInitialOnlyData(Worker_EntityId EntityId) const
{
	return (RetrievedInitialOnlyData.Find(EntityId) != nullptr);
}

bool InitialOnlyFilter::HasInitialOnlyDataOrRequestIfAbsent(Worker_EntityId EntityId)
{
	if (HasInitialOnlyData(EntityId))
	{
		return true;
	}

	if (InflightInitialOnlyEntities.Find(EntityId) != nullptr)
	{
		return false;
	}

	PendingInitialOnlyEntities.Add(EntityId);
	return false;
}

void InitialOnlyFilter::FlushRequests()
{
	QueryHandler.ProcessOps(Connection.GetCoordinator().GetViewDelta().GetWorkerMessages());

	if (PendingInitialOnlyEntities.Num() == 0)
	{
		return;
	}

	TArray<Worker_Constraint> EntityConstraintArray;
	EntityConstraintArray.Reserve(PendingInitialOnlyEntities.Num());

	for (auto EntityId : PendingInitialOnlyEntities)
	{
		UE_LOG(LogInitialOnlyFilter, Verbose, TEXT("Requested initial only data for entity %lld."), EntityId);

		Worker_Constraint Constraints{};
		Constraints.constraint_type = WORKER_CONSTRAINT_TYPE_ENTITY_ID;
		Constraints.constraint.entity_id_constraint.entity_id = EntityId;

		EntityConstraintArray.Add(Constraints);

		InflightInitialOnlyEntities.Add(EntityId);
	}

	Worker_EntityQuery InitialOnlyQuery{};

	InitialOnlyQuery.constraint.constraint_type = WORKER_CONSTRAINT_TYPE_OR;
	InitialOnlyQuery.constraint.constraint.or_constraint.constraint_count = EntityConstraintArray.Num();
	InitialOnlyQuery.constraint.constraint.or_constraint.constraints = EntityConstraintArray.GetData();
	InitialOnlyQuery.snapshot_result_type_component_set_id_count = 1;
	InitialOnlyQuery.snapshot_result_type_component_set_ids = &SpatialConstants::INITIAL_ONLY_COMPONENT_SET_ID;

	const Worker_RequestId RequestID = Connection.SendEntityQueryRequest(&InitialOnlyQuery, SpatialGDK::RETRY_UNTIL_COMPLETE);
	EntityQueryDelegate InitialOnlyQueryDelegate;
	InitialOnlyQueryDelegate.BindRaw(this, &InitialOnlyFilter::HandleInitialOnlyResponse);

	QueryHandler.AddRequest(RequestID, InitialOnlyQueryDelegate);

	InflightInitialOnlyRequests.Add(RequestID, { MoveTemp(PendingInitialOnlyEntities) });
}

void InitialOnlyFilter::HandleInitialOnlyResponse(const Worker_EntityQueryResponseOp& Op)
{
	ClearRequest(Op.request_id);

	if (Op.status_code != WORKER_STATUS_CODE_SUCCESS)
	{
		UE_LOG(LogInitialOnlyFilter, Error, TEXT("Failed to retrieve initial only data. Code: %d, %s"), Op.status_code,
			   UTF8_TO_TCHAR(Op.message));
		return;
	}

	for (uint32_t i = 0; i < Op.result_count; ++i)
	{
		const Worker_Entity* Entity = &Op.results[i];
		const Worker_EntityId EntityId = Entity->entity_id;

		if (Connection.GetView().Find(EntityId) == nullptr)
		{
			UE_LOG(LogInitialOnlyFilter, Verbose, TEXT("Received initial only data for entity no longer in view. Entity: %lld."), EntityId);
			continue;
		}

		UE_LOG(LogInitialOnlyFilter, Verbose, TEXT("Received initial only data for entity. Entity: %lld."), EntityId);

		// Extract and store the initial only data.
		TArray<ComponentData>& ComponentDatas = RetrievedInitialOnlyData.FindOrAdd(EntityId);
		ComponentDatas.Reserve(ComponentDatas.Num() + Entity->component_count);
		for (uint32_t j = 0; j < Entity->component_count; ++j)
		{
			const Worker_ComponentData& ComponentData = Entity->components[j];
			ComponentDatas.Emplace(ComponentData::CreateCopy(ComponentData.schema_type, ComponentData.component_id));
		}

		Connection.GetCoordinator().RefreshEntityCompleteness(Entity->entity_id);
	}
}

const TArray<SpatialGDK::ComponentData>* InitialOnlyFilter::GetInitialOnlyData(Worker_EntityId EntityId) const
{
	return RetrievedInitialOnlyData.Find(EntityId);
}

void InitialOnlyFilter::RemoveInitialOnlyData(Worker_EntityId EntityId)
{
	UE_LOG(LogInitialOnlyFilter, Verbose, TEXT("Removed initial only data for entity %lld."), EntityId);
	RetrievedInitialOnlyData.FindAndRemoveChecked(EntityId);
}

void InitialOnlyFilter::ClearRequest(Worker_RequestId RequestId)
{
	for (auto EntityId : InflightInitialOnlyRequests.FindChecked(RequestId))
	{
		InflightInitialOnlyEntities.Remove(EntityId);
	}

	InflightInitialOnlyRequests.Remove(RequestId);
}

} // namespace SpatialGDK
