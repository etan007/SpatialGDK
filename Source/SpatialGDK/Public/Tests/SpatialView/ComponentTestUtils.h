// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "SpatialView/EntityComponentTypes.h"
#include "SpatialView/EntityDelta.h"

#include <algorithm>

namespace SpatialGDK
{
namespace EntityComponentTestUtils
{
const Schema_FieldId EVENT_ID = 1;
const Schema_FieldId EVENT_INT_FIELD_ID = 2;
const Schema_FieldId TEST_DOUBLE_FIELD_ID = 1;
} // namespace EntityComponentTestUtils

inline ComponentData CreateTestComponentData(const Worker_ComponentId Id, const double Value)
{
	ComponentData Data{ Id };
	Schema_Object* Fields = Data.GetFields();
	Schema_AddDouble(Fields, EntityComponentTestUtils::TEST_DOUBLE_FIELD_ID, Value);
	return MoveTemp(Data);
}

// Assumes the passed data has the TEST_DOUBLE_FIELD_ID field populated.
inline double GetValueFromTestComponentData(Schema_ComponentData* Data)
{
	return Schema_GetDouble(Schema_GetComponentDataFields(Data), EntityComponentTestUtils::TEST_DOUBLE_FIELD_ID);
}

inline ComponentUpdate CreateTestComponentUpdate(const Worker_ComponentId Id, const double Value)
{
	ComponentUpdate Update{ Id };
	Schema_Object* Fields = Update.GetFields();
	Schema_AddDouble(Fields, EntityComponentTestUtils::TEST_DOUBLE_FIELD_ID, Value);
	return Update;
}

inline void AddTestEvent(ComponentUpdate* Update, const int Value)
{
	Schema_Object* events = Update->GetEvents();
	Schema_Object* eventData = Schema_AddObject(events, EntityComponentTestUtils::EVENT_ID);
	Schema_AddInt32(eventData, EntityComponentTestUtils::EVENT_INT_FIELD_ID, Value);
}

inline ComponentUpdate CreateTestComponentEvent(const Worker_ComponentId Id, const int Value)
{
	ComponentUpdate Update{ Id };
	AddTestEvent(&Update, Value);
	return Update;
}

/** Returns true if Lhs and Rhs have the same serialized form. */
inline bool CompareSchemaObjects(const Schema_Object* Lhs, const Schema_Object* Rhs)
{
	if (Lhs == Rhs)
	{
		return true;
	}

	if (Lhs == nullptr || Rhs == nullptr)
	{
		return false;
	}

	const auto Length = Schema_GetWriteBufferLength(Lhs);
	if (Schema_GetWriteBufferLength(Rhs) != Length)
	{
		return false;
	}
	const TUniquePtr<uint8_t[]> LhsBuffer = MakeUnique<uint8_t[]>(Length);
	const TUniquePtr<uint8_t[]> RhsBuffer = MakeUnique<uint8_t[]>(Length);
	Schema_SerializeToBuffer(Lhs, LhsBuffer.Get(), Length);
	Schema_SerializeToBuffer(Rhs, RhsBuffer.Get(), Length);
	return FMemory::Memcmp(LhsBuffer.Get(), RhsBuffer.Get(), Length) == 0;
}

inline bool CompareSchemaComponentData(Schema_ComponentData* Lhs, Schema_ComponentData* Rhs)
{
	return CompareSchemaObjects(Schema_GetComponentDataFields(Lhs), Schema_GetComponentDataFields(Rhs));
}

inline bool CompareSchemaComponentUpdate(Schema_ComponentUpdate* Lhs, Schema_ComponentUpdate* Rhs)
{
	if (!CompareSchemaObjects(Schema_GetComponentUpdateFields(Lhs), Schema_GetComponentUpdateFields(Rhs)))
	{
		return false;
	}

	return CompareSchemaObjects(Schema_GetComponentUpdateEvents(Lhs), Schema_GetComponentUpdateEvents(Rhs));
}

inline bool CompareSchemaComponentRefresh(const CompleteUpdateData& Lhs, const CompleteUpdateData& Rhs)
{
	if (!CompareSchemaObjects(Schema_GetComponentDataFields(Lhs.Data), Schema_GetComponentDataFields(Rhs.Data)))
	{
		return false;
	}

	if (Lhs.Events == nullptr)
	{
		if (Lhs.Events == Rhs.Events)
		{
			return true;
		}

		return Schema_GetWriteBufferLength(Rhs.Events) == 0;
	}

	if (Rhs.Events == nullptr)
	{
		return Schema_GetWriteBufferLength(Lhs.Events) == 0;
	}

	return CompareSchemaObjects(Lhs.Events, Rhs.Events);
}

/** Returns true if Lhs and Rhs have the same component ID and state. */
inline bool CompareComponentData(const ComponentData& Lhs, const ComponentData& Rhs)
{
	if (Lhs.GetComponentId() != Rhs.GetComponentId())
	{
		return false;
	}
	return CompareSchemaObjects(Lhs.GetFields(), Rhs.GetFields());
}

inline bool CompareComponentChangeById(const ComponentChange& Lhs, const ComponentChange& Rhs)
{
	return Lhs.ComponentId < Rhs.ComponentId;
}

inline bool CompareComponentChanges(const ComponentChange& Lhs, const ComponentChange& Rhs)
{
	if (Lhs.ComponentId != Rhs.ComponentId)
	{
		return false;
	}

	if (Lhs.Type != Rhs.Type)
	{
		return false;
	}

	switch (Lhs.Type)
	{
	case ComponentChange::ADD:
		return CompareSchemaComponentData(Lhs.Data, Rhs.Data);
	case ComponentChange::UPDATE:
		return CompareSchemaComponentUpdate(Lhs.Update, Rhs.Update);
	case ComponentChange::COMPLETE_UPDATE:
		return CompareSchemaComponentRefresh(Lhs.CompleteUpdate, Rhs.CompleteUpdate);
	case ComponentChange::REMOVE:
		break;
	default:
		checkNoEntry();
	}

	return true;
}

inline bool CompareAuthorityChangeById(const AuthorityChange& Lhs, const AuthorityChange& Rhs)
{
	return Lhs.ComponentSetId < Rhs.ComponentSetId;
}

inline bool CompareAuthorityChanges(const AuthorityChange& Lhs, const AuthorityChange& Rhs)
{
	if (Lhs.ComponentSetId != Rhs.ComponentSetId)
	{
		return false;
	}

	if (Lhs.Type != Rhs.Type)
	{
		return false;
	}

	return true;
}

/** Returns true if Lhs and Rhs have the same component ID and events. */
inline bool CompareComponentUpdateEvents(const ComponentUpdate& Lhs, const ComponentUpdate& Rhs)
{
	if (Lhs.GetComponentId() != Rhs.GetComponentId())
	{
		return false;
	}
	return CompareSchemaObjects(Lhs.GetEvents(), Rhs.GetEvents());
}

/** Returns true if Lhs and Rhs have the same component ID and state. */
inline bool CompareComponentUpdates(const ComponentUpdate& Lhs, const ComponentUpdate& Rhs)
{
	if (Lhs.GetComponentId() != Rhs.GetComponentId())
	{
		return false;
	}
	return CompareSchemaObjects(Lhs.GetFields(), Rhs.GetFields()) && CompareSchemaObjects(Lhs.GetEvents(), Rhs.GetEvents());
}

/** Returns true if Lhs and Rhs have the same entity ID, component ID, and state. */
inline bool CompareEntityComponentData(const EntityComponentData& Lhs, const EntityComponentData& Rhs)
{
	if (Lhs.EntityId != Rhs.EntityId)
	{
		return false;
	}
	return CompareComponentData(Lhs.Data, Rhs.Data);
}

/** Returns true if Lhs and Rhs have the same entity ID, component ID, and events. */
inline bool CompareEntityComponentUpdateEvents(const EntityComponentUpdate& Lhs, const EntityComponentUpdate& Rhs)
{
	if (Lhs.EntityId != Rhs.EntityId)
	{
		return false;
	}
	return CompareComponentUpdateEvents(Lhs.Update, Rhs.Update);
}

/** Returns true if Lhs and Rhs have the same entity ID, component ID, state, and events. */
inline bool CompareEntityComponentUpdates(const EntityComponentUpdate& Lhs, const EntityComponentUpdate& Rhs)
{
	if (Lhs.EntityId != Rhs.EntityId)
	{
		return false;
	}
	return CompareComponentUpdates(Lhs.Update, Rhs.Update);
}

/** Returns true if Lhs and Rhs have the same ID, component ID, data, state and events. */
inline bool CompareEntityComponentCompleteUpdates(const EntityComponentCompleteUpdate& Lhs, const EntityComponentCompleteUpdate& Rhs)
{
	if (Lhs.EntityId != Rhs.EntityId)
	{
		return false;
	}
	return CompareComponentData(Lhs.CompleteUpdate, Rhs.CompleteUpdate) && CompareComponentUpdateEvents(Lhs.Events, Rhs.Events);
}

inline bool EntityComponentIdEquality(const EntityComponentId& Lhs, const EntityComponentId& Rhs)
{
	return Lhs == Rhs;
}

inline bool WorkerComponentIdEquality(const Worker_ComponentId Lhs, const Worker_ComponentId Rhs)
{
	return Lhs == Rhs;
}

inline bool WorkerEntityIdEquality(const Worker_EntityId Lhs, const Worker_EntityId Rhs)
{
	return Lhs == Rhs;
}

inline bool CompareWorkerEntityId(const Worker_EntityId Lhs, const Worker_EntityId Rhs)
{
	return Lhs < Rhs;
}

template <typename T, typename Predicate>
bool AreEquivalent(const TArray<T>& Lhs, const TArray<T>& Rhs, Predicate&& Compare)
{
	if (Lhs.Num() != Rhs.Num())
	{
		return false;
	}

	return std::is_permutation(Lhs.GetData(), Lhs.GetData() + Lhs.Num(), Rhs.GetData(), Forward<Predicate>(Compare));
}

inline bool AreEquivalent(const TArray<EntityComponentUpdate>& Lhs, const TArray<EntityComponentUpdate>& Rhs)
{
	return AreEquivalent(Lhs, Rhs, CompareEntityComponentUpdates);
}

inline bool AreEquivalent(const TArray<EntityComponentCompleteUpdate>& Lhs, const TArray<EntityComponentCompleteUpdate>& Rhs)
{
	return AreEquivalent(Lhs, Rhs, CompareEntityComponentCompleteUpdates);
}

inline bool AreEquivalent(const TArray<EntityComponentData>& Lhs, const TArray<EntityComponentData>& Rhs)
{
	return AreEquivalent(Lhs, Rhs, CompareEntityComponentData);
}

inline bool AreEquivalent(const TArray<EntityComponentId>& Lhs, const TArray<EntityComponentId>& Rhs)
{
	return AreEquivalent(Lhs, Rhs, EntityComponentIdEquality);
}

} // namespace SpatialGDK
