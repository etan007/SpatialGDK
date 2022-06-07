// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "SpatialView/ComponentSetData.h"
#include "Tests/SpatialView/SpatialViewUtils.h"
#include "Tests/TestDefinitions.h"

#include "SpatialView/OpList/EntityComponentOpList.h"
#include "SpatialView/ViewDelta.h"
#include "Tests/SpatialView/ExpectedViewDelta.h"

#define VIEWDELTA_TEST(TestName) GDK_TEST(Core, ViewDelta, TestName)

namespace
{
const Worker_EntityId TestEntityId = 1;
const Worker_EntityId OtherTestEntityId = 2;
const Worker_EntityId AnotherTestEntityId = 3;
const Worker_EntityId YetAnotherTestEntityId = 4;
const Worker_ComponentId TestComponentId = 1;
const Worker_ComponentId OtherTestComponentId = 2;
const Worker_ComponentSetId TestComponentSetId = 3;
const double TestComponentValue = 20;
const double OtherTestComponentValue = 30;
const double TestEventValue = 25;

const SpatialGDK::FComponentSetData ComponentSetData = { { { TestComponentSetId, { TestComponentId, OtherTestComponentId } } } };
} // anonymous namespace

namespace SpatialGDK
{
struct ViewDeltaTestFixture
{
	ViewDeltaTestFixture() { ComponentSetData.ComponentSets.Add(TestComponentSetId).Add(TestComponentId); }

	Worker_EntityId TestEntityId = 1;
	Worker_EntityId OtherTestEntityId = 2;
	Worker_EntityId AnotherTestEntityId = 3;
	Worker_EntityId YetAnotherTestEntityId = 4;
	Worker_ComponentId TestComponentId = 1;
	Worker_ComponentSetId TestComponentSetId = 1;
	double TestComponentValue = 20;
	double OtherTestComponentValue = 30;
	double TestEventValue = 25;
	FComponentSetData ComponentSetData;
};

VIEWDELTA_TEST(GIVEN_empty_view_WHEN_add_entity_THEN_get_entity_in_view_and_delta)
{
	ViewDelta InputDelta;
	EntityView InputView;

	EntityComponentOpListBuilder OpListBuilder;
	OpListBuilder.AddEntity(TestEntityId);
	SetFromOpList(InputDelta, InputView, MoveTemp(OpListBuilder), ComponentSetData);

	EntityView ExpectedView;
	AddEntityToView(ExpectedView, TestEntityId);

	ExpectedViewDelta ExpectedDelta;
	ExpectedDelta.AddEntityDelta(TestEntityId, ExpectedViewDelta::ADD);

	TestTrue("View Deltas are equal", ExpectedDelta.Compare(InputDelta));
	TestTrue("Views are equal", CompareViews(InputView, ExpectedView));

	return true;
}

VIEWDELTA_TEST(GIVEN_entity_in_view_WHEN_remove_entity_THEN_empty_view)
{
	ViewDelta InputDelta;
	EntityView InputView;
	AddEntityToView(InputView, TestEntityId);

	EntityComponentOpListBuilder OpListBuilder;
	OpListBuilder.RemoveEntity(TestEntityId);
	SetFromOpList(InputDelta, InputView, MoveTemp(OpListBuilder), ComponentSetData);

	EntityView ExpectedView;
	ExpectedViewDelta ExpectedDelta;
	ExpectedDelta.AddEntityDelta(TestEntityId, ExpectedViewDelta::REMOVE);

	TestTrue("View Deltas are equal", ExpectedDelta.Compare(InputDelta));
	TestTrue("Views are equal", CompareViews(InputView, ExpectedView));

	return true;
}

VIEWDELTA_TEST(GIVEN_entity_in_view_WHEN_add_component_THEN_entity_and_component_in_view)
{
	ViewDelta InputDelta;
	EntityView InputView;
	AddEntityToView(InputView, TestEntityId);

	EntityComponentOpListBuilder OpListBuilder;
	OpListBuilder.AddComponent(TestEntityId, CreateTestComponentData(TestComponentId, TestComponentValue));
	SetFromOpList(InputDelta, InputView, MoveTemp(OpListBuilder), ComponentSetData);

	EntityView ExpectedView;
	AddEntityToView(ExpectedView, TestEntityId);
	AddComponentToView(ExpectedView, TestEntityId, CreateTestComponentData(TestComponentId, TestComponentValue));

	ExpectedViewDelta ExpectedDelta;
	ExpectedDelta.AddEntityDelta(TestEntityId, ExpectedViewDelta::UPDATE);
	ExpectedDelta.AddComponentAdded(TestEntityId, CreateTestComponentData(TestComponentId, TestComponentValue));

	TestTrue("View Deltas are equal", ExpectedDelta.Compare(InputDelta));
	TestTrue("Views are equal", CompareViews(InputView, ExpectedView));

	return true;
}

VIEWDELTA_TEST(GIVEN_entity_and_component_in_view_WHEN_update_component_THEN_component_udated_in_view)
{
	ViewDelta InputDelta;
	EntityView InputView;
	AddEntityToView(InputView, TestEntityId);
	AddComponentToView(InputView, TestEntityId, CreateTestComponentData(TestComponentId, TestComponentValue));

	EntityComponentOpListBuilder OpListBuilder;
	OpListBuilder.UpdateComponent(TestEntityId, CreateTestComponentUpdate(TestComponentId, OtherTestComponentValue));
	SetFromOpList(InputDelta, InputView, MoveTemp(OpListBuilder), ComponentSetData);

	EntityView ExpectedView;
	AddEntityToView(ExpectedView, TestEntityId);
	AddComponentToView(ExpectedView, TestEntityId, CreateTestComponentData(TestComponentId, OtherTestComponentValue));

	ExpectedViewDelta ExpectedDelta;
	ExpectedDelta.AddEntityDelta(TestEntityId, ExpectedViewDelta::UPDATE);
	ExpectedDelta.AddComponentUpdate(TestEntityId, CreateTestComponentUpdate(TestComponentId, OtherTestComponentValue));

	TestTrue("View Deltas are equal", ExpectedDelta.Compare(InputDelta));
	TestTrue("Views are equal", CompareViews(InputView, ExpectedView));

	return true;
}

VIEWDELTA_TEST(GIVEN_entity_and_component_in_view_WHEN_remove_component_THEN_component_not_in_view)
{
	ViewDelta InputDelta;
	EntityView InputView;
	AddEntityToView(InputView, TestEntityId);
	AddComponentToView(InputView, TestEntityId, CreateTestComponentData(TestComponentId, TestComponentValue));

	EntityComponentOpListBuilder OpListBuilder;
	OpListBuilder.RemoveComponent(TestEntityId, TestComponentId);
	SetFromOpList(InputDelta, InputView, MoveTemp(OpListBuilder), ComponentSetData);

	EntityView ExpectedView;
	AddEntityToView(ExpectedView, TestEntityId);

	ExpectedViewDelta ExpectedDelta;
	ExpectedDelta.AddEntityDelta(TestEntityId, ExpectedViewDelta::UPDATE);
	ExpectedDelta.AddComponentRemoved(TestEntityId, TestComponentId);

	TestTrue("View Deltas are equal", ExpectedDelta.Compare(InputDelta));
	TestTrue("Views are equal", CompareViews(InputView, ExpectedView));

	return true;
}

VIEWDELTA_TEST(GIVEN_entity_and_component_in_view_WHEN_authority_gained_THEN_authority_in_view)
{
	ViewDelta InputDelta;
	EntityView InputView;
	ComponentData TestComponentData = CreateTestComponentData(TestComponentId, TestComponentValue);

	AddEntityToView(InputView, TestEntityId);
	AddComponentToView(InputView, TestEntityId, TestComponentData.DeepCopy());

	EntityComponentOpListBuilder OpListBuilder;
	TArray<ComponentData> ComponentsInSet;
	ComponentsInSet.Add(TestComponentData.DeepCopy());
	OpListBuilder.SetAuthority(TestEntityId, TestComponentSetId, WORKER_AUTHORITY_AUTHORITATIVE, MoveTemp(ComponentsInSet));
	SetFromOpList(InputDelta, InputView, MoveTemp(OpListBuilder), ComponentSetData);

	EntityView ExpectedView;
	AddEntityToView(ExpectedView, TestEntityId);
	AddComponentToView(ExpectedView, TestEntityId, TestComponentData.DeepCopy());
	AddAuthorityToView(ExpectedView, TestEntityId, TestComponentSetId);

	ExpectedViewDelta ExpectedDelta;
	ExpectedDelta.AddEntityDelta(TestEntityId, ExpectedViewDelta::UPDATE);
	ExpectedDelta.AddAuthorityGained(TestEntityId, TestComponentSetId);
	ExpectedDelta.AddComponentRefreshed(TestEntityId, ComponentUpdate(TestComponentId), TestComponentData.DeepCopy());

	TestTrue("View Deltas are equal", ExpectedDelta.Compare(InputDelta));
	TestTrue("Views are equal", CompareViews(InputView, ExpectedView));

	return true;
}

VIEWDELTA_TEST(GIVEN_entity_and_auth_component_in_view_WHEN_authority_lost_THEN_unauth_component_in_view)
{
	ViewDelta InputDelta;
	EntityView InputView;
	ComponentData TestComponentData = CreateTestComponentData(TestComponentId, TestComponentValue);

	AddEntityToView(InputView, TestEntityId);
	AddComponentToView(InputView, TestEntityId, TestComponentData.DeepCopy());
	AddAuthorityToView(InputView, TestEntityId, TestComponentSetId);

	EntityComponentOpListBuilder OpListBuilder;
	OpListBuilder.SetAuthority(TestEntityId, TestComponentSetId, WORKER_AUTHORITY_NOT_AUTHORITATIVE,
							   CopyComponentSetOnEntity(TestEntityId, TestComponentSetId, InputView, ComponentSetData));
	SetFromOpList(InputDelta, InputView, MoveTemp(OpListBuilder), ComponentSetData);

	EntityView ExpectedView;
	AddEntityToView(ExpectedView, TestEntityId);
	AddComponentToView(ExpectedView, TestEntityId, TestComponentData.DeepCopy());

	ExpectedViewDelta ExpectedDelta;
	ExpectedDelta.AddEntityDelta(TestEntityId, ExpectedViewDelta::UPDATE);
	ExpectedDelta.AddComponentRefreshed(TestEntityId, ComponentUpdate(TestComponentId), TestComponentData.DeepCopy());
	ExpectedDelta.AddAuthorityLost(TestEntityId, TestComponentSetId);

	TestTrue("View Deltas are equal", ExpectedDelta.Compare(InputDelta));
	TestTrue("Views are equal", CompareViews(InputView, ExpectedView));

	return true;
}

VIEWDELTA_TEST(GIVEN_entity_and_components_in_view_WHEN_authority_gained_with_no_component_data_THEN_components_removed_from_view)
{
	ViewDelta InputDelta;
	EntityView InputView;
	ComponentData TestComponentData = CreateTestComponentData(TestComponentId, TestComponentValue);
	ComponentData OtherTestComponentData = CreateTestComponentData(OtherTestComponentId, TestComponentValue);

	AddEntityToView(InputView, TestEntityId);
	AddComponentToView(InputView, TestEntityId, TestComponentData.DeepCopy());
	AddComponentToView(InputView, TestEntityId, OtherTestComponentData.DeepCopy());

	EntityComponentOpListBuilder OpListBuilder;
	// Set authority with no component data - implying the components should be removed.
	OpListBuilder.SetAuthority(TestEntityId, TestComponentSetId, WORKER_AUTHORITY_AUTHORITATIVE, {});
	SetFromOpList(InputDelta, InputView, MoveTemp(OpListBuilder), ComponentSetData);

	EntityView ExpectedView;
	AddEntityToView(ExpectedView, TestEntityId);
	AddAuthorityToView(ExpectedView, TestEntityId, TestComponentSetId);

	ExpectedViewDelta ExpectedDelta;
	ExpectedDelta.AddEntityDelta(TestEntityId, ExpectedViewDelta::UPDATE);
	ExpectedDelta.AddComponentRemoved(TestEntityId, TestComponentId);
	ExpectedDelta.AddComponentRemoved(TestEntityId, OtherTestComponentId);
	ExpectedDelta.AddAuthorityGained(TestEntityId, TestComponentSetId);

	TestTrue("View Deltas are equal", ExpectedDelta.Compare(InputDelta));
	TestTrue("Views are equal", CompareViews(InputView, ExpectedView));

	return true;
}

VIEWDELTA_TEST(GIVEN_entity_with_no_components_WHEN_authority_gained_with_non_empty_component_data_THEN_component_added_to_view)
{
	ViewDelta InputDelta;
	EntityView InputView;
	ComponentData TestComponentData = CreateTestComponentData(TestComponentId, TestComponentValue);
	ComponentData OtherTestComponentData = CreateTestComponentData(OtherTestComponentId, TestComponentValue);

	AddEntityToView(InputView, TestEntityId);

	EntityComponentOpListBuilder OpListBuilder;
	TArray<ComponentData> CanonicalSetData;
	CanonicalSetData.Add(TestComponentData.DeepCopy());
	CanonicalSetData.Add(OtherTestComponentData.DeepCopy());
	OpListBuilder.SetAuthority(TestEntityId, TestComponentSetId, WORKER_AUTHORITY_AUTHORITATIVE, MoveTemp(CanonicalSetData));
	SetFromOpList(InputDelta, InputView, MoveTemp(OpListBuilder), ComponentSetData);

	EntityView ExpectedView;
	AddEntityToView(ExpectedView, TestEntityId);
	AddAuthorityToView(ExpectedView, TestEntityId, TestComponentSetId);
	AddComponentToView(ExpectedView, TestEntityId, TestComponentData.DeepCopy());
	AddComponentToView(ExpectedView, TestEntityId, OtherTestComponentData.DeepCopy());

	ExpectedViewDelta ExpectedDelta;
	ExpectedDelta.AddEntityDelta(TestEntityId, ExpectedViewDelta::UPDATE);
	ExpectedDelta.AddComponentAdded(TestEntityId, TestComponentData.DeepCopy());
	ExpectedDelta.AddComponentAdded(TestEntityId, OtherTestComponentData.DeepCopy());
	ExpectedDelta.AddAuthorityGained(TestEntityId, TestComponentSetId);

	TestTrue("View Deltas are equal", ExpectedDelta.Compare(InputDelta));
	TestTrue("Views are equal", CompareViews(InputView, ExpectedView));

	return true;
}

// There are two components in the component set, call them X and Y. We start with X on the entity but not Y.
// We add an authority delegation that has only Y in the canonical data.
// We expect to see Y added and X removed.
VIEWDELTA_TEST(GIVEN_one_component_from_set_WHEN_set_delegation_with_only_other_component_THEN_removes_old_and_adds_new)
{
	ViewDelta InputDelta;
	EntityView InputView;
	ComponentData TestComponentData = CreateTestComponentData(TestComponentId, TestComponentValue);
	ComponentData OtherTestComponentData = CreateTestComponentData(OtherTestComponentId, TestComponentValue);

	AddEntityToView(InputView, TestEntityId);
	AddComponentToView(InputView, TestEntityId, TestComponentData.DeepCopy());

	EntityComponentOpListBuilder OpListBuilder;
	TArray<ComponentData> CanonicalSetData;
	CanonicalSetData.Add(OtherTestComponentData.DeepCopy());
	OpListBuilder.SetAuthority(TestEntityId, TestComponentSetId, WORKER_AUTHORITY_AUTHORITATIVE, MoveTemp(CanonicalSetData));
	SetFromOpList(InputDelta, InputView, MoveTemp(OpListBuilder), ComponentSetData);

	EntityView ExpectedView;
	AddEntityToView(ExpectedView, TestEntityId);
	AddAuthorityToView(ExpectedView, TestEntityId, TestComponentSetId);
	AddComponentToView(ExpectedView, TestEntityId, OtherTestComponentData.DeepCopy());

	ExpectedViewDelta ExpectedDelta;
	ExpectedDelta.AddEntityDelta(TestEntityId, ExpectedViewDelta::UPDATE);
	ExpectedDelta.AddComponentRemoved(TestEntityId, TestComponentId);
	ExpectedDelta.AddComponentAdded(TestEntityId, OtherTestComponentData.DeepCopy());
	ExpectedDelta.AddAuthorityGained(TestEntityId, TestComponentSetId);

	TestTrue("View Deltas are equal", ExpectedDelta.Compare(InputDelta));
	TestTrue("Views are equal", CompareViews(InputView, ExpectedView));

	return true;
}

VIEWDELTA_TEST(GIVEN_connected_view_WHEN_disconnect_op_THEN_disconnected_view)
{
	ViewDelta InputDelta;
	EntityView InputView;

	EntityComponentOpListBuilder OpListBuilder;
	OpListBuilder.SetDisconnect(WORKER_CONNECTION_STATUS_CODE_REJECTED, StringStorage("Test disconnection reason"));
	SetFromOpList(InputDelta, InputView, MoveTemp(OpListBuilder), ComponentSetData);

	EntityView ExpectedView;

	ExpectedViewDelta ExpectedDelta;
	ExpectedDelta.AddDisconnect(WORKER_CONNECTION_STATUS_CODE_REJECTED, TEXT("Test disconnection reason"));

	TestTrue("View Deltas are equal", ExpectedDelta.Compare(InputDelta));
	TestTrue("Views are equal", CompareViews(InputView, ExpectedView));

	return true;
}

VIEWDELTA_TEST(GIVEN_entity_and_auth_component_in_view_WHEN_authority_lost_and_gained_THEN_authority_lost_temporarily)
{
	ViewDelta InputDelta;
	EntityView InputView;
	ComponentData TestComponentData = CreateTestComponentData(TestComponentId, TestComponentValue);

	AddEntityToView(InputView, TestEntityId);
	AddComponentToView(InputView, TestEntityId, TestComponentData.DeepCopy());
	AddAuthorityToView(InputView, TestEntityId, TestComponentSetId);

	EntityComponentOpListBuilder OpListBuilder;
	OpListBuilder.SetAuthority(TestEntityId, TestComponentSetId, WORKER_AUTHORITY_NOT_AUTHORITATIVE,
							   CopyComponentSetOnEntity(TestEntityId, TestComponentSetId, InputView, ComponentSetData));
	OpListBuilder.SetAuthority(TestEntityId, TestComponentSetId, WORKER_AUTHORITY_AUTHORITATIVE,
							   CopyComponentSetOnEntity(TestEntityId, TestComponentSetId, InputView, ComponentSetData));
	SetFromOpList(InputDelta, InputView, MoveTemp(OpListBuilder), ComponentSetData);

	EntityView ExpectedView;
	AddEntityToView(ExpectedView, TestEntityId);
	AddComponentToView(ExpectedView, TestEntityId, TestComponentData.DeepCopy());
	AddAuthorityToView(ExpectedView, TestEntityId, TestComponentSetId);

	ExpectedViewDelta ExpectedDelta;
	ExpectedDelta.AddEntityDelta(TestEntityId, ExpectedViewDelta::UPDATE);
	ExpectedDelta.AddAuthorityLostTemporarily(TestEntityId, TestComponentSetId);
	ExpectedDelta.AddComponentRefreshed(TestEntityId, ComponentUpdate(TestComponentId), TestComponentData.DeepCopy());

	TestTrue("View Deltas are equal", ExpectedDelta.Compare(InputDelta));
	TestTrue("Views are equal", CompareViews(InputView, ExpectedView));

	return true;
}

VIEWDELTA_TEST(GIVEN_empty_view_WHEN_add_remove_THEN_get_empty_view_and_delta)
{
	ViewDelta InputDelta;
	EntityView InputView;

	EntityComponentOpListBuilder OpListBuilder;
	OpListBuilder.AddEntity(TestEntityId);
	OpListBuilder.RemoveEntity(TestEntityId);
	SetFromOpList(InputDelta, InputView, MoveTemp(OpListBuilder), ComponentSetData);

	EntityView ExpectedView;
	ExpectedViewDelta ExpectedDelta;

	TestTrue("View Deltas are equal", ExpectedDelta.Compare(InputDelta));
	TestTrue("Views are equal", CompareViews(InputView, ExpectedView));

	return true;
}

VIEWDELTA_TEST(GIVEN_entity_and_component_in_view_WHEN_update_and_add_component_THEN_component_refresh)
{
	ViewDelta InputDelta;
	EntityView InputView;
	AddEntityToView(InputView, TestEntityId);
	AddComponentToView(InputView, TestEntityId, CreateTestComponentData(TestComponentId, TestComponentValue));

	EntityComponentOpListBuilder OpListBuilder;
	OpListBuilder.UpdateComponent(TestEntityId, CreateTestComponentEvent(TestComponentId, TestEventValue));
	OpListBuilder.AddComponent(TestEntityId, CreateTestComponentData(TestComponentId, OtherTestComponentValue));
	SetFromOpList(InputDelta, InputView, MoveTemp(OpListBuilder), ComponentSetData);

	EntityView ExpectedView;
	AddEntityToView(ExpectedView, TestEntityId);
	AddComponentToView(ExpectedView, TestEntityId, CreateTestComponentData(TestComponentId, OtherTestComponentValue));

	ExpectedViewDelta ExpectedDelta;
	ExpectedDelta.AddEntityDelta(TestEntityId, ExpectedViewDelta::UPDATE);
	ExpectedDelta.AddComponentRefreshed(TestEntityId, CreateTestComponentEvent(TestComponentId, TestEventValue),
										CreateTestComponentData(TestComponentId, OtherTestComponentValue));

	TestTrue("View Deltas are equal", ExpectedDelta.Compare(InputDelta));
	TestTrue("Views are equal", CompareViews(InputView, ExpectedView));

	return true;
}

VIEWDELTA_TEST(GIVEN_entity_and_component_in_view_WHEN_remove_and_add_component_THEN_component_refresh)
{
	ViewDelta InputDelta;
	EntityView InputView;
	AddEntityToView(InputView, TestEntityId);
	AddComponentToView(InputView, TestEntityId, CreateTestComponentData(TestComponentId, TestComponentValue));

	EntityComponentOpListBuilder OpListBuilder;
	OpListBuilder.RemoveComponent(TestEntityId, TestComponentId);
	OpListBuilder.AddComponent(TestEntityId, CreateTestComponentData(TestComponentId, OtherTestComponentValue));
	SetFromOpList(InputDelta, InputView, MoveTemp(OpListBuilder), ComponentSetData);

	EntityView ExpectedView;
	AddEntityToView(ExpectedView, TestEntityId);
	AddComponentToView(ExpectedView, TestEntityId, CreateTestComponentData(TestComponentId, OtherTestComponentValue));

	ExpectedViewDelta ExpectedDelta;
	ExpectedDelta.AddEntityDelta(TestEntityId, ExpectedViewDelta::UPDATE);
	ExpectedDelta.AddComponentRefreshed(TestEntityId, ComponentUpdate(TestComponentId),
										CreateTestComponentData(TestComponentId, OtherTestComponentValue));

	TestTrue("View Deltas are equal", ExpectedDelta.Compare(InputDelta));
	TestTrue("Views are equal", CompareViews(InputView, ExpectedView));

	return true;
}

VIEWDELTA_TEST(GIVEN_entity_view_WHEN_entity_remove_and_add_THEN_no_entity_flag)
{
	ViewDelta InputDelta;
	EntityView InputView;
	AddEntityToView(InputView, TestEntityId);

	EntityComponentOpListBuilder OpListBuilder;
	OpListBuilder.RemoveEntity(TestEntityId);
	OpListBuilder.AddEntity(TestEntityId);
	SetFromOpList(InputDelta, InputView, MoveTemp(OpListBuilder), ComponentSetData);

	EntityView ExpectedView;
	AddEntityToView(ExpectedView, TestEntityId);

	ExpectedViewDelta ExpectedDelta;
	ExpectedDelta.AddEntityDelta(TestEntityId, ExpectedViewDelta::UPDATE);

	TestTrue("View Deltas are equal", ExpectedDelta.Compare(InputDelta));
	TestTrue("Views are equal", CompareViews(InputView, ExpectedView));

	return true;
}

VIEWDELTA_TEST(GIVEN_empty_view_WHEN_add_remove_add_THEN_entity_in_view_and_delta)
{
	ViewDelta InputDelta;
	EntityView InputView;

	EntityComponentOpListBuilder OpListBuilder;
	OpListBuilder.AddEntity(TestEntityId);
	OpListBuilder.RemoveEntity(TestEntityId);
	OpListBuilder.AddEntity(TestEntityId);
	SetFromOpList(InputDelta, InputView, MoveTemp(OpListBuilder), ComponentSetData);

	EntityView ExpectedView;
	AddEntityToView(ExpectedView, TestEntityId);

	ExpectedViewDelta ExpectedDelta;
	ExpectedDelta.AddEntityDelta(TestEntityId, ExpectedViewDelta::EntityChangeType::ADD);

	TestTrue("View Deltas are equal", ExpectedDelta.Compare(InputDelta));
	TestTrue("Views are equal", CompareViews(InputView, ExpectedView));

	return true;
}

VIEWDELTA_TEST(GIVEN_empty_view_WHEN_add_entity_add_component_THEN_entity_and_component_in_view_and_delta)
{
	ViewDelta InputDelta;
	EntityView InputView;

	EntityComponentOpListBuilder OpListBuilder;
	OpListBuilder.AddEntity(TestEntityId);
	OpListBuilder.AddComponent(TestEntityId, CreateTestComponentData(TestComponentId, TestComponentValue));
	SetFromOpList(InputDelta, InputView, MoveTemp(OpListBuilder), ComponentSetData);

	EntityView ExpectedView;
	AddEntityToView(ExpectedView, TestEntityId);
	AddComponentToView(ExpectedView, TestEntityId, CreateTestComponentData(TestComponentId, TestComponentValue));

	ExpectedViewDelta ExpectedDelta;
	ExpectedDelta.AddEntityDelta(TestEntityId, ExpectedViewDelta::EntityChangeType::ADD);
	ExpectedDelta.AddComponentAdded(TestEntityId, CreateTestComponentData(TestComponentId, TestComponentValue));

	TestTrue("View Deltas are equal", ExpectedDelta.Compare(InputDelta));
	TestTrue("Views are equal", CompareViews(InputView, ExpectedView));
	return true;
}

VIEWDELTA_TEST(GIVEN_entity_and_component_in_view_WHEN_remove_entity_THEN_empty_view_remove_ops_in_delta)
{
	ViewDelta InputDelta;
	EntityView InputView;
	AddEntityToView(InputView, TestEntityId);
	AddComponentToView(InputView, TestEntityId, CreateTestComponentData(TestComponentId, TestComponentValue));

	EntityComponentOpListBuilder OpListBuilder;
	OpListBuilder.RemoveComponent(TestEntityId, TestComponentId);
	OpListBuilder.RemoveEntity(TestEntityId);
	SetFromOpList(InputDelta, InputView, MoveTemp(OpListBuilder), ComponentSetData);

	EntityView ExpectedView;

	ExpectedViewDelta ExpectedDelta;
	ExpectedDelta.AddEntityDelta(TestEntityId, ExpectedViewDelta::EntityChangeType::REMOVE);
	ExpectedDelta.AddComponentRemoved(TestEntityId, TestComponentId);

	TestTrue("View Deltas are equal", ExpectedDelta.Compare(InputDelta));
	TestTrue("Views are equal", CompareViews(InputView, ExpectedView));

	return true;
}

// Projection Tests
VIEWDELTA_TEST(GIVEN_view_delta_with_update_for_entity_complete_WHEN_project_THEN_contains_update)
{
	ViewDelta Delta;
	FSubViewDelta SubViewDelta;
	EntityView View;
	AddEntityToView(View, TestEntityId);
	AddComponentToView(View, TestEntityId, CreateTestComponentData(TestComponentId, TestComponentValue));

	EntityComponentOpListBuilder OpListBuilder;
	OpListBuilder.UpdateComponent(TestEntityId, CreateTestComponentUpdate(TestComponentId, OtherTestComponentValue));
	SetFromOpList(Delta, View, MoveTemp(OpListBuilder), ComponentSetData);

	Delta.Project(SubViewDelta, TArray<Worker_EntityId>{ TestEntityId }, TArray<Worker_EntityId>{}, TArray<Worker_EntityId>{},
				  TArray<Worker_EntityId>{});

	ExpectedViewDelta ExpectedSubViewDelta;
	ExpectedSubViewDelta.AddEntityDelta(TestEntityId, ExpectedViewDelta::UPDATE);
	ExpectedSubViewDelta.AddComponentUpdate(TestEntityId, CreateTestComponentUpdate(TestComponentId, OtherTestComponentValue));

	TestTrue("View Deltas are equal", ExpectedSubViewDelta.Compare(SubViewDelta));

	return true;
}

VIEWDELTA_TEST(GIVEN_empty_view_delta_with_newly_complete_entity_WHEN_project_THEN_contains_marker_add)
{
	ViewDelta Delta;
	FSubViewDelta SubViewDelta;
	EntityView View;
	AddEntityToView(View, TestEntityId);
	AddComponentToView(View, TestEntityId, CreateTestComponentData(TestComponentId, TestComponentValue));

	Delta.Project(SubViewDelta, TArray<Worker_EntityId>{}, TArray<Worker_EntityId>{ TestEntityId }, TArray<Worker_EntityId>{},
				  TArray<Worker_EntityId>{});

	ExpectedViewDelta ExpectedSubViewDelta;
	ExpectedSubViewDelta.AddEntityDelta(TestEntityId, ExpectedViewDelta::ADD);

	TestTrue("View Deltas are equal", ExpectedSubViewDelta.Compare(SubViewDelta));

	return true;
}

VIEWDELTA_TEST(GIVEN_empty_view_delta_with_newly_incomplete_entity_WHEN_project_THEN_contains_marker_remove)
{
	ViewDelta Delta;
	FSubViewDelta SubViewDelta;
	EntityView View;
	AddEntityToView(View, TestEntityId);
	AddComponentToView(View, TestEntityId, CreateTestComponentData(TestComponentId, TestComponentValue));

	Delta.Project(SubViewDelta, TArray<Worker_EntityId>{}, TArray<Worker_EntityId>{}, TArray<Worker_EntityId>{ TestEntityId },
				  TArray<Worker_EntityId>{});

	ExpectedViewDelta ExpectedSubViewDelta;
	ExpectedSubViewDelta.AddEntityDelta(TestEntityId, ExpectedViewDelta::REMOVE);

	TestTrue("View Deltas are equal", ExpectedSubViewDelta.Compare(SubViewDelta));

	return true;
}

VIEWDELTA_TEST(GIVEN_empty_view_delta_with_temporarily_incomplete_entity_WHEN_project_THEN_contains_marker_temporary_remove)
{
	ViewDelta Delta;
	FSubViewDelta SubViewDelta;
	EntityView View;
	AddEntityToView(View, TestEntityId);
	AddComponentToView(View, TestEntityId, CreateTestComponentData(TestComponentId, TestComponentValue));

	Delta.Project(SubViewDelta, TArray<Worker_EntityId>{}, TArray<Worker_EntityId>{}, TArray<Worker_EntityId>{},
				  TArray<Worker_EntityId>{ TestEntityId });

	ExpectedViewDelta ExpectedSubViewDelta;
	ExpectedSubViewDelta.AddEntityDelta(TestEntityId, ExpectedViewDelta::TEMPORARILY_REMOVED);

	TestTrue("View Deltas are equal", ExpectedSubViewDelta.Compare(SubViewDelta));

	return true;
}

VIEWDELTA_TEST(GIVEN_arbitrary_delta_and_completeness_WHEN_project_THEN_subview_delta_correct)
{
	ViewDelta Delta;
	FSubViewDelta SubViewDelta;
	EntityView View;
	AddEntityToView(View, TestEntityId);
	AddComponentToView(View, TestEntityId, CreateTestComponentData(TestComponentId, TestComponentValue));
	AddEntityToView(View, OtherTestEntityId);
	AddEntityToView(View, AnotherTestEntityId);
	AddEntityToView(View, YetAnotherTestEntityId);
	AddComponentToView(View, YetAnotherTestEntityId, CreateTestComponentData(TestComponentId, OtherTestComponentValue));

	TArray<OpList> OpLists;
	EntityComponentOpListBuilder OpListBuilder;
	OpListBuilder.UpdateComponent(TestEntityId, CreateTestComponentUpdate(TestComponentId, OtherTestComponentValue));
	OpLists.Push(MoveTemp(OpListBuilder).CreateOpList());
	OpListBuilder = EntityComponentOpListBuilder{};
	OpListBuilder.UpdateComponent(YetAnotherTestEntityId, CreateTestComponentUpdate(TestComponentId, TestComponentValue));
	OpLists.Push(MoveTemp(OpListBuilder).CreateOpList());
	Delta.SetFromOpList(MoveTemp(OpLists), View, ComponentSetData);

	Delta.Project(SubViewDelta, TArray<Worker_EntityId>{ TestEntityId, YetAnotherTestEntityId },
				  TArray<Worker_EntityId>{ OtherTestEntityId }, TArray<Worker_EntityId>{ AnotherTestEntityId }, TArray<Worker_EntityId>{});

	ExpectedViewDelta ExpectedSubViewDelta;
	ExpectedSubViewDelta.AddEntityDelta(TestEntityId, ExpectedViewDelta::UPDATE);
	ExpectedSubViewDelta.AddComponentUpdate(TestEntityId, CreateTestComponentUpdate(TestComponentId, OtherTestComponentValue));
	ExpectedSubViewDelta.AddEntityDelta(OtherTestEntityId, ExpectedViewDelta::ADD);
	ExpectedSubViewDelta.AddEntityDelta(AnotherTestEntityId, ExpectedViewDelta::REMOVE);
	ExpectedSubViewDelta.AddEntityDelta(YetAnotherTestEntityId, ExpectedViewDelta::UPDATE);
	ExpectedSubViewDelta.AddComponentUpdate(YetAnotherTestEntityId, CreateTestComponentUpdate(TestComponentId, TestComponentValue));

	TestTrue("View Deltas are equal", ExpectedSubViewDelta.Compare(SubViewDelta));

	return true;
}
} // namespace SpatialGDK
