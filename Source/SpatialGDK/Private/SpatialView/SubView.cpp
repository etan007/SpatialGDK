// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "SpatialView/SubView.h"

#include "Algo/Copy.h"
#include "SpatialView/EntityComponentTypes.h"
#include "Utils/ComponentFactory.h"

namespace SpatialGDK
{
const FFilterPredicate FSubView::NoFilter = [](const Worker_EntityId&, const EntityViewElement&) {
	return true;
};
const TArray<FDispatcherRefreshCallback> FSubView::NoDispatcherCallbacks = TArray<FDispatcherRefreshCallback>{};
const FComponentChangeRefreshPredicate FSubView::NoComponentChangeRefreshPredicate = [](const FEntityComponentChange&) {
	return true;
};
const FAuthorityChangeRefreshPredicate FSubView::NoAuthorityChangeRefreshPredicate = [](const Worker_EntityId) {
	return true;
};

FSubView::FSubView(const Worker_ComponentId InTagComponentId, FFilterPredicate InFilter, const EntityView* InView, IDispatcher& Dispatcher,
				   const TArray<FDispatcherRefreshCallback>& DispatcherRefreshCallbacks)
	: TagComponentId(InTagComponentId)
	, Filter(MoveTemp(InFilter))
	, View(InView)
	, ScopedDispatcherCallbacks()
{
	RegisterTagCallbacks(Dispatcher);
	RegisterRefreshCallbacks(Dispatcher, DispatcherRefreshCallbacks);
}

void FSubView::Advance(const ViewDelta& Delta)
{
	// Note: Complete entities will be a longer list than the others for the majority of iterations under
	// probable normal usage. This sort could then become expensive, and a potential optimisation would be
	// to maintain the ordering of complete entities when merging in the newly complete entities and enforcing
	// that complete entities is always sorted. This would also need to be enforced in the temporarily incomplete case.
	// If this sort shows up in a profile it would be worth trying.
	// 注：对于下的大多数迭代，完整实体的列表将比其他实体的列表更长可能正常使用。这样做的代价可能会很高，一种潜在的优化方法是在合并新的完整实体并执行
	// 始终对完整实体进行排序。在暂时不完整的情况下，这也需要强制执行。如果这种类型出现在profile中，那么值得一试。
	Algo::Sort(CompleteEntities);
	Algo::Sort(NewlyCompleteEntities);
	Algo::Sort(NewlyIncompleteEntities);
	Algo::Sort(TemporarilyIncompleteEntities);

	Delta.Project(SubViewDelta, CompleteEntities, NewlyCompleteEntities, NewlyIncompleteEntities, TemporarilyIncompleteEntities);

	CompleteEntities.Append(NewlyCompleteEntities);
	NewlyCompleteEntities.Empty();
	NewlyIncompleteEntities.Empty();
	TemporarilyIncompleteEntities.Empty();
}

const FSubViewDelta& FSubView::GetViewDelta() const
{
	return SubViewDelta;
}

const TArray<Worker_EntityId>& FSubView::GetCompleteEntities() const
{
	return CompleteEntities;
}

void FSubView::Refresh()
{
	for (const Worker_EntityId_Key TaggedEntityId : TaggedEntities)
	{
		CheckEntityAgainstFilter(TaggedEntityId);
	}
}

void FSubView::RefreshEntity(const Worker_EntityId EntityId)
{
	if (TaggedEntities.Contains(EntityId))
	{
		CheckEntityAgainstFilter(EntityId);
	}
}

const EntityView& FSubView::GetView() const
{
	return *View;
}

bool FSubView::HasEntity(const Worker_EntityId EntityId) const
{
	const EntityViewElement* Entity = View->Find(EntityId);
	return Entity != nullptr;
}

bool FSubView::IsEntityComplete(const Worker_EntityId EntityId) const
{
	return CompleteEntities.Contains(EntityId);
}

bool FSubView::HasComponent(const Worker_EntityId EntityId, const Worker_ComponentId ComponentId) const
{
	const EntityViewElement* Entity = View->Find(EntityId);
	if (Entity == nullptr)
	{
		return false;
	}
	return Entity->Components.ContainsByPredicate(ComponentIdEquality{ ComponentId });
}

bool FSubView::HasAuthority(const Worker_EntityId EntityId, const Worker_ComponentId ComponentId) const
{
	const EntityViewElement* Entity = View->Find(EntityId);
	if (Entity == nullptr)
	{
		return false;
	}
	return Entity->Authority.Contains(ComponentId);
}

FDispatcherRefreshCallback FSubView::CreateComponentExistenceRefreshCallback(IDispatcher& Dispatcher, const Worker_ComponentId ComponentId,
																			 const FComponentChangeRefreshPredicate& RefreshPredicate)
{
	return [ComponentId, &Dispatcher, RefreshPredicate](const FRefreshCallback& Callback) {
		const CallbackId AddedCallbackId =
			Dispatcher.RegisterComponentAddedCallback(ComponentId, [RefreshPredicate, Callback](const FEntityComponentChange& Change) {
				if (RefreshPredicate(Change))
				{
					Callback(Change.EntityId);
				}
			});

		const CallbackId RemovedCallbackId =
			Dispatcher.RegisterComponentRemovedCallback(ComponentId, [RefreshPredicate, Callback](const FEntityComponentChange& Change) {
				if (RefreshPredicate(Change))
				{
					Callback(Change.EntityId);
				}
			});
		return TArray<CallbackId>({ AddedCallbackId, RemovedCallbackId });
	};
}

FDispatcherRefreshCallback FSubView::CreateComponentChangedRefreshCallback(IDispatcher& Dispatcher, const Worker_ComponentId ComponentId,
																		   const FComponentChangeRefreshPredicate& RefreshPredicate)
{
	return [ComponentId, &Dispatcher, RefreshPredicate](const FRefreshCallback& Callback) {
		const CallbackId ValueCallbackId =
			Dispatcher.RegisterComponentValueCallback(ComponentId, [RefreshPredicate, Callback](const FEntityComponentChange& Change) {
				if (RefreshPredicate(Change))
				{
					Callback(Change.EntityId);
				}
			});
		return TArray<CallbackId>({ ValueCallbackId });
	};
}

FDispatcherRefreshCallback FSubView::CreateAuthorityChangeRefreshCallback(IDispatcher& Dispatcher, const Worker_ComponentId ComponentId,
																		  const FAuthorityChangeRefreshPredicate& RefreshPredicate)
{
	return [ComponentId, &Dispatcher, RefreshPredicate](const FRefreshCallback& Callback) {
		const CallbackId GainedCallbackId =
			Dispatcher.RegisterAuthorityGainedCallback(ComponentId, [RefreshPredicate, Callback](const Worker_EntityId Id) {
				if (RefreshPredicate(Id))
				{
					Callback(Id);
				}
			});
		const CallbackId LostCallbackId =
			Dispatcher.RegisterAuthorityLostCallback(ComponentId, [RefreshPredicate, Callback](const Worker_EntityId Id) {
				if (RefreshPredicate(Id))
				{
					Callback(Id);
				}
			});
		return TArray<CallbackId>({ GainedCallbackId, LostCallbackId });
	};
}

void FSubView::RegisterTagCallbacks(IDispatcher& Dispatcher)
{
	CallbackId AddedCallbackId = Dispatcher.RegisterAndInvokeComponentAddedCallback(
		TagComponentId,
		[this](const FEntityComponentChange& Change) {
			OnTaggedEntityAdded(Change.EntityId);
		},
		*View);
	ScopedDispatcherCallbacks.Emplace(Dispatcher, AddedCallbackId);

	CallbackId RemovedCallbackId =
		Dispatcher.RegisterComponentRemovedCallback(TagComponentId, [this](const FEntityComponentChange& Change) {
			OnTaggedEntityRemoved(Change.EntityId);
		});
	ScopedDispatcherCallbacks.Emplace(Dispatcher, RemovedCallbackId);
}

void FSubView::RegisterRefreshCallbacks(IDispatcher& Dispatcher, const TArray<FDispatcherRefreshCallback>& DispatcherRefreshCallbacks)
{
	const FRefreshCallback RefreshEntityCallback = [this](const Worker_EntityId EntityId) {
		RefreshEntity(EntityId);
	};
	for (FDispatcherRefreshCallback Callback : DispatcherRefreshCallbacks)
	{
		const TArray<CallbackId> RegisteredCallbackIds = Callback(RefreshEntityCallback);
		for (const CallbackId& RegisteredCallbackId : RegisteredCallbackIds)
		{
			ScopedDispatcherCallbacks.Emplace(Dispatcher, RegisteredCallbackId);
		}
	}
}

void FSubView::OnTaggedEntityAdded(const Worker_EntityId EntityId)
{
	TaggedEntities.Add(EntityId);
	CheckEntityAgainstFilter(EntityId);
}

void FSubView::OnTaggedEntityRemoved(const Worker_EntityId EntityId)
{
	TaggedEntities.RemoveSingleSwap(EntityId);
	EntityIncomplete(EntityId);
}

void FSubView::CheckEntityAgainstFilter(const Worker_EntityId EntityId)
{
	if (View->Contains(EntityId) && Filter(EntityId, (*View)[EntityId]))
	{
		EntityComplete(EntityId);
		return;
	}
	EntityIncomplete(EntityId);
}

void FSubView::EntityComplete(const Worker_EntityId EntityId)
{
	// We were just about to remove this entity, but it has become complete again before the delta was read.
	// Mark it as temporarily incomplete, but otherwise treat it as if it hadn't gone incomplete.
	// 我们正要删除此实体，但在读取增量之前，它又完成了。
	// 将其标记为暂时不完整，否则将其视为未完成。
	if (NewlyIncompleteEntities.RemoveSingleSwap(EntityId))
	{
		CompleteEntities.Add(EntityId);
		TemporarilyIncompleteEntities.Add(EntityId);
		return;
	}
	// This is new to us. Mark it as newly complete.
	// 这对我们来说是新鲜事。将其标记为新完成。
	if (!NewlyCompleteEntities.Contains(EntityId) && !CompleteEntities.Contains(EntityId))
	{
		NewlyCompleteEntities.Add(EntityId);
	}
}

void FSubView::EntityIncomplete(const Worker_EntityId EntityId)
{
	// If we were about to add this, don't. It's as if we never saw it.
	if (NewlyCompleteEntities.RemoveSingleSwap(EntityId))
	{
		return;
	}
	// Otherwise, if it is currently complete, we need to remove it, and mark it as about to remove.
	if (CompleteEntities.RemoveSingleSwap(EntityId))
	{
		NewlyIncompleteEntities.Add(EntityId);
	}
}
} // namespace SpatialGDK
