// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "SpatialView/ViewDelta.h"

#include "SpatialView/EntityComponentTypes.h"

#include "Algo/StableSort.h"
#include <algorithm>

namespace SpatialGDK
{
void ViewDelta::SetFromOpList(TArray<OpList> OpLists, EntityView& View, const FComponentSetData& ComponentSetData)
{
	Clear();
	for (OpList& Ops : OpLists)
	{
		ProcessOpList(Ops, View, ComponentSetData);
	}
	OpListStorage = MoveTemp(OpLists);

	PopulateEntityDeltas(View);
}

void ViewDelta::Project(FSubViewDelta& SubDelta, const TArray<Worker_EntityId>& CompleteEntities,
						const TArray<Worker_EntityId>& NewlyCompleteEntities, const TArray<Worker_EntityId>& NewlyIncompleteEntities,
						const TArray<Worker_EntityId>& TemporarilyIncompleteEntities) const
{
	SubDelta.EntityDeltas.Empty();

	// No projection is applied to worker messages, as they are not entity specific.
	// 没有投影应用于工作消息，因为它们不是特定于实体的。
	SubDelta.WorkerMessages = &WorkerMessages;

	// All arrays here are sorted by entity ID.
	auto DeltaIt = EntityDeltas.CreateConstIterator();
	auto CompleteIt = CompleteEntities.CreateConstIterator();
	auto NewlyCompleteIt = NewlyCompleteEntities.CreateConstIterator();
	auto NewlyIncompleteIt = NewlyIncompleteEntities.CreateConstIterator();
	auto TemporarilyIncompleteIt = TemporarilyIncompleteEntities.CreateConstIterator();

	for (;;)
	{
		const Worker_EntityId DeltaId = DeltaIt ? DeltaIt->EntityId : SENTINEL_ENTITY_ID;
		const Worker_EntityId CompleteId = CompleteIt ? *CompleteIt : SENTINEL_ENTITY_ID;
		const Worker_EntityId NewlyCompleteId = NewlyCompleteIt ? *NewlyCompleteIt : SENTINEL_ENTITY_ID;
		const Worker_EntityId NewlyIncompleteId = NewlyIncompleteIt ? *NewlyIncompleteIt : SENTINEL_ENTITY_ID;
		const Worker_EntityId TemporarilyIncompleteId = TemporarilyIncompleteIt ? *TemporarilyIncompleteIt : SENTINEL_ENTITY_ID;
		const uint64 MinEntityId = FMath::Min3(FMath::Min(static_cast<uint64>(DeltaId), static_cast<uint64>(CompleteId)),
											   FMath::Min(static_cast<uint64>(NewlyCompleteId), static_cast<uint64>(NewlyIncompleteId)),
											   static_cast<uint64>(TemporarilyIncompleteId));
		const Worker_EntityId CurrentEntityId = static_cast<Worker_EntityId>(MinEntityId);
		// If no list has elements left to read then stop.
		// 如果列表没有剩下要读取的元素，则停止。
		if (CurrentEntityId == SENTINEL_ENTITY_ID)
		{
			break;
		}

		// Find the intersection between complete entities and the entity IDs in the view delta, add them to this delta.
		// 在视图增量中找到完整实体和实体ID列表之间的交集，将它们添加到此增量中
		if (CompleteId == CurrentEntityId && DeltaId == CurrentEntityId)
		{
			EntityDelta CompleteDelta = *DeltaIt;
			if (TemporarilyIncompleteId == CurrentEntityId)
			{
				// This is a delta for a complete entity which was also temporarily removed. Change its type to reflect that.
				// 这是临时删除的完整实体的增量。更改其类型以反映这一点。
				CompleteDelta.Type = EntityDelta::TEMPORARILY_REMOVED;
				++TemporarilyIncompleteIt;
			}
			SubDelta.EntityDeltas.Emplace(CompleteDelta);
		}
		// Temporarily incomplete entities which aren't present in the projecting view delta are represented as marker
		// temporarily removed entities with no state.
		// 投影视图增量中不存在的临时不完整实体表示为标记，暂时删除了无状态的实体。
		else if (TemporarilyIncompleteId == CurrentEntityId)
		{
			SubDelta.EntityDeltas.Emplace(EntityDelta{ CurrentEntityId, EntityDelta::TEMPORARILY_REMOVED });
			++TemporarilyIncompleteIt;
		}
		// Newly complete entities are represented as marker add entities with no state.
		// 新完成的实体表示为无状态的标记添加实体。
		else if (NewlyCompleteId == CurrentEntityId)
		{
			SubDelta.EntityDeltas.Emplace(EntityDelta{ CurrentEntityId, EntityDelta::ADD });
			++NewlyCompleteIt;
		}
		// Newly incomplete entities are represented as marker remove entities with no state.
		// 新的不完整实体表示为无状态的标记删除实体。
		else if (NewlyIncompleteId == CurrentEntityId)
		{
			SubDelta.EntityDeltas.Emplace(EntityDelta{ CurrentEntityId, EntityDelta::REMOVE });
			++NewlyIncompleteIt;
		}

		// Logic for incrementing complete and delta iterators. If either iterator is done, null the other,as there can no longer be any intersection.
		// 递增完整迭代器和增量迭代器的逻辑。如果其中一个迭代器已完成，另一个迭代器为空，因为再也没有交集了。
		if (CompleteId == CurrentEntityId)
		{
			++CompleteIt;
			if (!CompleteIt)
			{
				DeltaIt.SetToEnd();
			}
		}
		if (DeltaId == CurrentEntityId)
		{
			++DeltaIt;
			if (!DeltaIt)
			{
				CompleteIt.SetToEnd();
			}
		}
	}
}

void ViewDelta::Clear()
{
	EntityChanges.Empty();
	ComponentChanges.Empty();
	AuthorityChanges.Empty();

	ConnectionStatusCode = 0;

	EntityDeltas.Empty();
	WorkerMessages.Empty();
	AuthorityGainedForDelta.Empty();
	AuthorityLostForDelta.Empty();
	AuthorityLostTempForDelta.Empty();
	ComponentsAddedForDelta.Empty();
	ComponentsRemovedForDelta.Empty();
	ComponentUpdatesForDelta.Empty();
	ComponentsRefreshedForDelta.Empty();
	OpListStorage.Empty();
}

const TArray<EntityDelta>& ViewDelta::GetEntityDeltas() const
{
	return EntityDeltas;
}

const TArray<Worker_Op>& ViewDelta::GetWorkerMessages() const
{
	return WorkerMessages;
}

bool ViewDelta::HasConnectionStatusChanged() const
{
	return ConnectionStatusCode != 0;
}

Worker_ConnectionStatusCode ViewDelta::GetConnectionStatusChange() const
{
	check(HasConnectionStatusChanged());
	return static_cast<Worker_ConnectionStatusCode>(ConnectionStatusCode);
}

FString ViewDelta::GetConnectionStatusChangeMessage() const
{
	check(HasConnectionStatusChanged());
	return ConnectionStatusMessage;
}

ViewDelta::ReceivedComponentChange::ReceivedComponentChange(const Worker_AddComponentOp& Op)
	: EntityId(Op.entity_id)
	, ComponentId(Op.data.component_id)
	, Type(ADD)
	, ComponentAdded(Op.data.schema_type)
{
}

ViewDelta::ReceivedComponentChange::ReceivedComponentChange(const Worker_ComponentUpdateOp& Op)
	: EntityId(Op.entity_id)
	, ComponentId(Op.update.component_id)
	, Type(UPDATE)
	, ComponentUpdate(Op.update.schema_type)
{
}

ViewDelta::ReceivedComponentChange::ReceivedComponentChange(const Worker_RemoveComponentOp& Op)
	: EntityId(Op.entity_id)
	, ComponentId(Op.component_id)
	, Type(REMOVE)
{
}

bool ViewDelta::DifferentEntity::operator()(const ReceivedEntityChange& E) const
{
	return E.EntityId != EntityId;
}

bool ViewDelta::DifferentEntity::operator()(const ReceivedComponentChange& Op) const
{
	return Op.EntityId != EntityId;
}

bool ViewDelta::DifferentEntity::operator()(const Worker_ComponentSetAuthorityChangeOp& Op) const
{
	return Op.entity_id != EntityId;
}

bool ViewDelta::DifferentEntityComponent::operator()(const ReceivedComponentChange& Op) const
{
	return Op.ComponentId != ComponentId || Op.EntityId != EntityId;
}

bool ViewDelta::DifferentEntityComponent::operator()(const Worker_ComponentSetAuthorityChangeOp& Op) const
{
	return Op.component_set_id != ComponentId || Op.entity_id != EntityId;
}

bool ViewDelta::EntityComponentComparison::operator()(const ReceivedComponentChange& Lhs, const ReceivedComponentChange& Rhs) const
{
	if (Lhs.EntityId != Rhs.EntityId)
	{
		return Lhs.EntityId < Rhs.EntityId;
	}
	return Lhs.ComponentId < Rhs.ComponentId;
}

bool ViewDelta::EntityComponentComparison::operator()(const Worker_ComponentSetAuthorityChangeOp& Lhs,
													  const Worker_ComponentSetAuthorityChangeOp& Rhs) const
{
	if (Lhs.entity_id != Rhs.entity_id)
	{
		return Lhs.entity_id < Rhs.entity_id;
	}
	return Lhs.component_set_id < Rhs.component_set_id;
}

bool ViewDelta::EntityComparison::operator()(const ReceivedEntityChange& Lhs, const ReceivedEntityChange& Rhs) const
{
	return Lhs.EntityId < Rhs.EntityId;
}

ComponentChange ViewDelta::CalculateAdd(ReceivedComponentChange* Start, ReceivedComponentChange* End, TArray<ComponentData>& Components)
{
	// There must be at least one component add; anything before it can be ignored.
	ReceivedComponentChange* It = std::find_if(Start, End, [](const ReceivedComponentChange& Op) {
		return Op.Type == ReceivedComponentChange::ADD;
	});

	Schema_ComponentData* Data = It->ComponentAdded;
	++It;

	while (It != End)
	{
		switch (It->Type)
		{
		case ReceivedComponentChange::ADD:
			Data = It->ComponentAdded;
			break;
		case ReceivedComponentChange::UPDATE:
			Schema_ApplyComponentUpdateToData(It->ComponentUpdate, Data);
			break;
		case ReceivedComponentChange::REMOVE:
			break;
		}
		++It;
	}
	Components.Emplace(ComponentData::CreateCopy(Data, Start->ComponentId));
	// We don't want to reference the component in the view as is isn't stable.
	// 我们不想引用视图中的组件，因为它不稳定。
	return ComponentChange(Start->ComponentId, Data);
}

ComponentChange ViewDelta::CalculateCompleteUpdate(ReceivedComponentChange* Start, ReceivedComponentChange* End, Schema_ComponentData* Data,
												   Schema_ComponentUpdate* Events, ComponentData& Component)
{
	for (auto It = Start; It != End; ++It)
	{
		switch (It->Type)
		{
		case ReceivedComponentChange::ADD:
			Data = It->ComponentAdded;
			break;
		case ReceivedComponentChange::UPDATE:
			if (Data)
			{
				Schema_ApplyComponentUpdateToData(It->ComponentUpdate, Data);
			}
			if (Events)
			{
				Schema_MergeComponentUpdateIntoUpdate(It->ComponentUpdate, Events);
			}
			else
			{
				Events = It->ComponentUpdate;
			}
			break;
		case ReceivedComponentChange::REMOVE:
			break;
		}
	}

	Component = ComponentData::CreateCopy(Data, Start->ComponentId);
	Schema_Object* EventsObj = Events ? Schema_GetComponentUpdateEvents(Events) : nullptr;
	// Use the data from the op list as pointers from the view aren't stable.
	return ComponentChange(Start->ComponentId, Data, EventsObj);
}

ComponentChange ViewDelta::CalculateUpdate(ReceivedComponentChange* Start, ReceivedComponentChange* End, ComponentData& Component)
{
	// For an update we don't know if we are calculating a complete-update or a regular update.
	// So the first message processed might be an add or an update.
	auto It = std::find_if(Start, End, [](const ReceivedComponentChange& Op) {
		return Op.Type != ReceivedComponentChange::REMOVE;
	});

	// If the first message is an add then calculate a complete-update.
	if (It->Type == ReceivedComponentChange::ADD)
	{
		return CalculateCompleteUpdate(It + 1, End, It->ComponentAdded, nullptr, Component);
	}

	Schema_ComponentUpdate* Update = It->ComponentUpdate;
	++It;
	while (It != End)
	{
		switch (It->Type)
		{
		case ReceivedComponentChange::ADD:
			return CalculateCompleteUpdate(It + 1, End, It->ComponentAdded, Update, Component);
		case ReceivedComponentChange::UPDATE:
			Schema_MergeComponentUpdateIntoUpdate(It->ComponentUpdate, Update);
			break;
		case ReceivedComponentChange::REMOVE:
			return CalculateCompleteUpdate(It + 1, End, nullptr, nullptr, Component);
		}
		++It;
	}

	Schema_ApplyComponentUpdateToData(Update, Component.GetUnderlying());
	Component = Component.DeepCopy();
	return ComponentChange(Start->ComponentId, Update);
}

void ViewDelta::ProcessOpList(const OpList& Ops, const EntityView& View, const FComponentSetData& ComponentSetData)
{
	for (uint32 i = 0; i < Ops.Count; ++i)
	{
		const Worker_Op& Op = Ops.Ops[i];
		switch (static_cast<Worker_OpType>(Op.op_type))
		{
		case WORKER_OP_TYPE_DISCONNECT:
			ConnectionStatusCode = Op.op.disconnect.connection_status_code;
			ConnectionStatusMessage = Op.op.disconnect.reason;
			break;
		case WORKER_OP_TYPE_CRITICAL_SECTION:
			// Ignore critical sections.
			break;
		case WORKER_OP_TYPE_ADD_ENTITY:
			EntityChanges.Push(ReceivedEntityChange{ Op.op.add_entity.entity_id, true });
			break;
		case WORKER_OP_TYPE_REMOVE_ENTITY:
			EntityChanges.Push(ReceivedEntityChange{ Op.op.remove_entity.entity_id, false });
			break;
		case WORKER_OP_TYPE_METRICS:
		case WORKER_OP_TYPE_FLAG_UPDATE:
		case WORKER_OP_TYPE_RESERVE_ENTITY_IDS_RESPONSE:
		case WORKER_OP_TYPE_CREATE_ENTITY_RESPONSE:
		case WORKER_OP_TYPE_DELETE_ENTITY_RESPONSE:
		case WORKER_OP_TYPE_ENTITY_QUERY_RESPONSE:
		case WORKER_OP_TYPE_COMMAND_REQUEST:
		case WORKER_OP_TYPE_COMMAND_RESPONSE:
			WorkerMessages.Push(Op);
			break;
		case WORKER_OP_TYPE_ADD_COMPONENT:
			ComponentChanges.Emplace(Op.op.add_component);
			break;
		case WORKER_OP_TYPE_REMOVE_COMPONENT:
			ComponentChanges.Emplace(Op.op.remove_component);
			break;
		case WORKER_OP_TYPE_COMPONENT_SET_AUTHORITY_CHANGE:
			GenerateComponentChangesFromSetData(Op.op.component_set_authority_change, View, ComponentSetData);
			AuthorityChanges.Emplace(Op.op.component_set_authority_change);
			break;
		case WORKER_OP_TYPE_COMPONENT_UPDATE:
			ComponentChanges.Emplace(Op.op.component_update);
			break;
		default:
			break;
		}
	}
}

void ViewDelta::GenerateComponentChangesFromSetData(const Worker_ComponentSetAuthorityChangeOp& Op, const EntityView& View,
													const FComponentSetData& ComponentSetData)
{
	// Generate component changes to:
	// * Remove all components on the entity, that are in the component set.
	// * Add all components the with data in the op.
	// If one component is both removed and added then this is interpreted as component refresh in the view delta.
	// Otherwise the component will be added or removed as appropriate.
	// 生成组件更改以：
	// *删除实体上组件集中的所有组件。
	// *添加op中包含数据的所有组件。
	// 如果同时删除和添加了一个组件，则这将被解释为视图增量中的组件刷新。否则，将酌情添加或删除组件。
	if (ComponentSetData.ComponentSets.Contains(Op.component_set_id))
	{
		const TSet<Worker_ComponentId>& Set = ComponentSetData.ComponentSets[Op.component_set_id];

		// If a component on the entity is in the set then generate a remove operation.
		// 如果实体上的组件在集合中，则生成移除操作。
		if (const EntityViewElement* Entity = View.Find(Op.entity_id))
		{
			for (const ComponentData& Component : Entity->Components)
			{
				const Worker_ComponentId ComponentId = Component.GetComponentId();
				if (Set.Contains(ComponentId))
				{
					Worker_RemoveComponentOp RemoveOp = { Op.entity_id, ComponentId };
					ComponentChanges.Emplace(RemoveOp);
				}
			}
		}
	}
 



	// If the component has data in the authority op then generate an add operation.
	// 如果组件在权限op中有数据，则生成添加操作。
	for (uint32 i = 0; i < Op.canonical_component_set_data_count; ++i)
	{
		Worker_AddComponentOp AddOp = { Op.entity_id, Op.canonical_component_set_data[i] };
		ComponentChanges.Emplace(AddOp);
	}
}

void ViewDelta::PopulateEntityDeltas(EntityView& View)
{
	// Make sure there is enough space in the view delta storage.
	// This allows us to rely on stable pointers as we add new elements.
	ComponentsAddedForDelta.Reserve(ComponentChanges.Num());
	ComponentsRemovedForDelta.Reserve(ComponentChanges.Num());
	ComponentUpdatesForDelta.Reserve(ComponentChanges.Num());
	ComponentsRefreshedForDelta.Reserve(ComponentChanges.Num());
	AuthorityGainedForDelta.Reserve(AuthorityChanges.Num());
	AuthorityLostForDelta.Reserve(AuthorityChanges.Num());
	AuthorityLostTempForDelta.Reserve(AuthorityChanges.Num());
    // 根据EntityID排序
	Algo::StableSort(ComponentChanges, EntityComponentComparison{});
	Algo::StableSort(AuthorityChanges, EntityComponentComparison{});
	Algo::StableSort(EntityChanges, EntityComparison{});

	// Add sentinel elements to the ends of the arrays.Prevents the need for bounds checks on the iterators.
	// 将sentinel元素添加到阵列的末端。防止需要对迭代器进行边界检查。
	ComponentChanges.Emplace(Worker_RemoveComponentOp{ SENTINEL_ENTITY_ID, 0 });
	AuthorityChanges.Emplace(Worker_ComponentSetAuthorityChangeOp{ SENTINEL_ENTITY_ID, 0, WORKER_AUTHORITY_NOT_AUTHORITATIVE,0, });
	EntityChanges.Emplace(ReceivedEntityChange{ SENTINEL_ENTITY_ID, false });

	auto ComponentIt = ComponentChanges.GetData();
	auto AuthorityIt = AuthorityChanges.GetData();
	auto EntityIt = EntityChanges.GetData();

	ReceivedComponentChange* ComponentChangesEnd = ComponentIt + ComponentChanges.Num();
	Worker_ComponentSetAuthorityChangeOp* AuthorityChangesEnd = AuthorityIt + AuthorityChanges.Num();
	ReceivedEntityChange* EntityChangesEnd = EntityIt + EntityChanges.Num();

	// At the beginning of each loop each iterator should point to the first element for an entity.
	// Each loop we want to work with a single entity ID.
	// We check the entities each iterator is pointing to and pick the smallest one.
	// If that is the sentinel ID then stop.
	for (;;)
	{
		// Get the next entity ID. We want to pick the smallest entity referenced by the iterators.
		// Convert to uint64 to ensure the sentinel value is larger than all valid IDs.
		const uint64 MinEntityId = FMath::Min3(static_cast<uint64>(ComponentIt->EntityId), static_cast<uint64>(AuthorityIt->entity_id),
											   static_cast<uint64>(EntityIt->EntityId));

		// If no list has elements left to read then stop.
		if (static_cast<Worker_EntityId>(MinEntityId) == SENTINEL_ENTITY_ID)
		{
			break;
		}

		const Worker_EntityId CurrentEntityId = static_cast<Worker_EntityId>(MinEntityId);

		EntityDelta Delta = {};
		Delta.EntityId = CurrentEntityId;

		EntityViewElement* ViewElement = View.Find(CurrentEntityId);
		const bool bAlreadyExisted = ViewElement != nullptr;

		if (ViewElement == nullptr)
		{
			ViewElement = &View.Add(CurrentEntityId);
		}

		if (ComponentIt->EntityId == CurrentEntityId)
		{
			ComponentIt = ProcessEntityComponentChanges(ComponentIt, ComponentChangesEnd, ViewElement->Components, Delta);
		}

		if (AuthorityIt->entity_id == CurrentEntityId)
		{
			AuthorityIt = ProcessEntityAuthorityChanges(AuthorityIt, AuthorityChangesEnd, ViewElement->Authority, Delta);
		}

		if (EntityIt->EntityId == CurrentEntityId)
		{
			EntityIt = ProcessEntityExistenceChange(EntityIt, EntityChangesEnd, Delta, bAlreadyExisted, View);
			// Did the entity flicker into view for less than a tick.
			if (Delta.Type == EntityDelta::UPDATE && !bAlreadyExisted)
			{
				View.Remove(CurrentEntityId);
				continue;
			}
		}

		EntityDeltas.Push(Delta);
	}
}

ViewDelta::ReceivedComponentChange* ViewDelta::ProcessEntityComponentChanges(ReceivedComponentChange* It, ReceivedComponentChange* End,
																			 TArray<ComponentData>& Components, EntityDelta& Delta)
{
	int32 AddCount = 0;
	int32 UpdateCount = 0;
	int32 RemoveCount = 0;
	int32 RefreshCount = 0;

	const Worker_EntityId EntityId = It->EntityId;
	// At the end of each loop `It` should point to the first element for an entity-component.
	// Stop and return when the component is for a different entity.
	// There will always be at least one iteration of the loop.
	for (;;)
	{
		ReceivedComponentChange* NextComponentIt = std::find_if(It, End, DifferentEntityComponent{ EntityId, It->ComponentId });

		ComponentData* Component = Components.FindByPredicate(ComponentIdEquality{ It->ComponentId });
		const bool bComponentExists = Component != nullptr;

		// The element one before NextComponentIt must be the last element for this component.
		switch ((NextComponentIt - 1)->Type)
		{
		case ReceivedComponentChange::ADD:
			if (bComponentExists)
			{
				// 全部更新
				ComponentsRefreshedForDelta.Emplace(CalculateCompleteUpdate(It, NextComponentIt, nullptr, nullptr, *Component));
				++RefreshCount;
			}
			else
			{
				// 增量添加
				ComponentsAddedForDelta.Emplace(CalculateAdd(It, NextComponentIt, Components));
				++AddCount;
			}
			break;
		case ReceivedComponentChange::UPDATE:
			if (bComponentExists)
			{
				ComponentChange Update = CalculateUpdate(It, NextComponentIt, *Component);
				if (Update.Type == ComponentChange::COMPLETE_UPDATE)
				{
					// 全部更新
					ComponentsRefreshedForDelta.Emplace(Update);
					++RefreshCount;
				}
				else
				{
					// 增量更新
					ComponentUpdatesForDelta.Emplace(Update);
					++UpdateCount;
				}
			}
			else
			{
				// 组件新增
				ComponentsAddedForDelta.Emplace(CalculateAdd(It, NextComponentIt, Components));
				++AddCount;
			}
			break;
		case ReceivedComponentChange::REMOVE:
			if (bComponentExists)
			{
				// 移除增量
				ComponentsRemovedForDelta.Emplace(It->ComponentId);
				// 移除索引组件
				Components.RemoveAtSwap(Component - Components.GetData());
				++RemoveCount;
			}
			break;
		}

		if (NextComponentIt->EntityId != EntityId)
		{
			Delta.ComponentsAdded = { ComponentsAddedForDelta.GetData() + ComponentsAddedForDelta.Num() - AddCount, AddCount };
			Delta.ComponentsRemoved = { ComponentsRemovedForDelta.GetData() + ComponentsRemovedForDelta.Num() - RemoveCount, RemoveCount };
			Delta.ComponentUpdates = { ComponentUpdatesForDelta.GetData() + ComponentUpdatesForDelta.Num() - UpdateCount, UpdateCount };
			Delta.ComponentsRefreshed = { ComponentsRefreshedForDelta.GetData() + ComponentsRefreshedForDelta.Num() - RefreshCount,
										  RefreshCount };
			return NextComponentIt;
		}

		It = NextComponentIt;
	}
}

Worker_ComponentSetAuthorityChangeOp* ViewDelta::ProcessEntityAuthorityChanges(Worker_ComponentSetAuthorityChangeOp* It,
																			   Worker_ComponentSetAuthorityChangeOp* End,
																			   TArray<Worker_ComponentSetId>& EntityAuthority,
																			   EntityDelta& Delta)
{
	int32 GainCount = 0;
	int32 LossCount = 0;
	int32 LossTempCount = 0;

	const Worker_EntityId EntityId = It->entity_id;
	// After each loop the iterator points to the first op relating to the next entity-component.
	// Stop and return when that component is for a different entity.
	// There will always be at least one iteration of the loop.
	for (;;)
	{
		// Find the last element for this entity-component.
		const Worker_ComponentSetId ComponentSetId = It->component_set_id;
		It = std::find_if(It, End, DifferentEntityComponent{ EntityId, ComponentSetId }) - 1;
		const int32 AuthorityIndex = EntityAuthority.Find(ComponentSetId);
		const bool bHasAuthority = AuthorityIndex != INDEX_NONE;

		if (It->authority == WORKER_AUTHORITY_AUTHORITATIVE)
		{
			if (bHasAuthority)
			{
				AuthorityLostTempForDelta.Emplace(ComponentSetId, AuthorityChange::AUTHORITY_LOST_TEMPORARILY);
				++LossTempCount;
			}
			else
			{
				EntityAuthority.Push(ComponentSetId);
				AuthorityGainedForDelta.Emplace(ComponentSetId, AuthorityChange::AUTHORITY_GAINED);
				++GainCount;
			}
		}
		else if (bHasAuthority)
		{
			AuthorityLostForDelta.Emplace(ComponentSetId, AuthorityChange::AUTHORITY_LOST);
			EntityAuthority.RemoveAtSwap(AuthorityIndex);
			++LossCount;
		}

		// Move to the next entity-component.
		++It;

		if (It->entity_id != EntityId)
		{
			Delta.AuthorityGained = { AuthorityGainedForDelta.GetData() + AuthorityGainedForDelta.Num() - GainCount, GainCount };
			Delta.AuthorityLost = { AuthorityLostForDelta.GetData() + AuthorityLostForDelta.Num() - LossCount, LossCount };
			Delta.AuthorityLostTemporarily = { AuthorityLostTempForDelta.GetData() + AuthorityLostTempForDelta.Num() - LossTempCount,
											   LossTempCount };
			return It;
		}
	}
}

ViewDelta::ReceivedEntityChange* ViewDelta::ProcessEntityExistenceChange(ReceivedEntityChange* It, ReceivedEntityChange* End,
																		 EntityDelta& Delta, bool bAlreadyInView, EntityView& View)
{
	// Find the last element relating to the same entity.
	const Worker_EntityId EntityId = It->EntityId;
	It = std::find_if(It, End, DifferentEntity{ EntityId }) - 1;

	const bool bEntityAdded = It->bAdded;

	// If the entity's presence has not changed then it's an update.
	if (bEntityAdded == bAlreadyInView)
	{
		Delta.Type = EntityDelta::UPDATE;
		return It + 1;
	}

	if (bEntityAdded)
	{
		Delta.Type = EntityDelta::ADD;
	}
	else
	{
		Delta.Type = EntityDelta::REMOVE;
		View.Remove(EntityId);
	}

	return It + 1;
}

} // namespace SpatialGDK
