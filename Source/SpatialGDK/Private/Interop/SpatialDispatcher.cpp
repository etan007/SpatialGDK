// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "Interop/SpatialDispatcher.h"

#include "Interop/SpatialReceiver.h"
#include "Interop/SpatialWorkerFlags.h"
#include "UObject/UObjectIterator.h"
#include "Utils/OpUtils.h"
#include "Utils/SpatialMetrics.h"

#include "WorkerSDK/improbable/c_worker.h"

DEFINE_LOG_CATEGORY(LogSpatialView);

void SpatialDispatcher::Init(USpatialWorkerFlags* InSpatialWorkerFlags)
{
	SpatialWorkerFlags = InSpatialWorkerFlags;
}

void SpatialDispatcher::ProcessOps(const TArray<Worker_Op>& Ops)
{
	for (const Worker_Op& Op : Ops)
	{
		if (IsExternalSchemaOp(Op))
		{
			ProcessExternalSchemaOp(Op);
			continue;
		}

		switch (Op.op_type)
		{
		// Critical Section
		case WORKER_OP_TYPE_CRITICAL_SECTION:
			break;

		// World Command Responses
		case WORKER_OP_TYPE_FLAG_UPDATE:
			if (Op.op.flag_update.value == nullptr)
			{
				SpatialWorkerFlags->RemoveWorkerFlag(UTF8_TO_TCHAR(Op.op.flag_update.name));
			}
			else
			{
				SpatialWorkerFlags->SetWorkerFlag(UTF8_TO_TCHAR(Op.op.flag_update.name), UTF8_TO_TCHAR(Op.op.flag_update.value));
			}
			break;
		default:
			break;
		}
	}
}

bool SpatialDispatcher::IsExternalSchemaOp(const Worker_Op& Op) const
{
	Worker_ComponentId ComponentId = SpatialGDK::GetComponentId(Op);
	return SpatialConstants::MIN_EXTERNAL_SCHEMA_ID <= ComponentId && ComponentId <= SpatialConstants::MAX_EXTERNAL_SCHEMA_ID;
}

void SpatialDispatcher::ProcessExternalSchemaOp(const Worker_Op& Op)
{
	const Worker_ComponentId ComponentId = SpatialGDK::GetComponentId(Op);

	if (!ensureAlwaysMsgf(ComponentId != SpatialConstants::INVALID_COMPONENT_ID,
						  TEXT("Tried to process external schema op with invalid component ID")))
	{
		return;
	}

	switch (Op.op_type)
	{
	case WORKER_OP_TYPE_COMPONENT_SET_AUTHORITY_CHANGE:
	case WORKER_OP_TYPE_ADD_COMPONENT:
	case WORKER_OP_TYPE_REMOVE_COMPONENT:
	case WORKER_OP_TYPE_COMPONENT_UPDATE:
	case WORKER_OP_TYPE_COMMAND_REQUEST:
	case WORKER_OP_TYPE_COMMAND_RESPONSE:
		RunCallbacks(ComponentId, &Op);
		break;
	default:
		// This should never happen providing the GetComponentId function has
		// the same explicit cases as the switch in this method
		checkNoEntry();
		return;
	}
}

SpatialDispatcher::FCallbackId SpatialDispatcher::OnAddComponent(Worker_ComponentId ComponentId,
																 const TFunction<void(const Worker_AddComponentOp&)>& Callback)
{
	return AddGenericOpCallback(ComponentId, WORKER_OP_TYPE_ADD_COMPONENT, [Callback](const Worker_Op* Op) {
		Callback(Op->op.add_component);
	});
}

SpatialDispatcher::FCallbackId SpatialDispatcher::OnRemoveComponent(Worker_ComponentId ComponentId,
																	const TFunction<void(const Worker_RemoveComponentOp&)>& Callback)
{
	return AddGenericOpCallback(ComponentId, WORKER_OP_TYPE_REMOVE_COMPONENT, [Callback](const Worker_Op* Op) {
		Callback(Op->op.remove_component);
	});
}

SpatialDispatcher::FCallbackId SpatialDispatcher::OnAuthorityChange(
	Worker_ComponentId ComponentId, const TFunction<void(const Worker_ComponentSetAuthorityChangeOp&)>& Callback)
{
	return AddGenericOpCallback(ComponentId, WORKER_OP_TYPE_COMPONENT_SET_AUTHORITY_CHANGE, [Callback](const Worker_Op* Op) {
		Callback(Op->op.component_set_authority_change);
	});
}

SpatialDispatcher::FCallbackId SpatialDispatcher::OnComponentUpdate(Worker_ComponentId ComponentId,
																	const TFunction<void(const Worker_ComponentUpdateOp&)>& Callback)
{
	return AddGenericOpCallback(ComponentId, WORKER_OP_TYPE_COMPONENT_UPDATE, [Callback](const Worker_Op* Op) {
		Callback(Op->op.component_update);
	});
}

SpatialDispatcher::FCallbackId SpatialDispatcher::OnCommandRequest(Worker_ComponentId ComponentId,
																   const TFunction<void(const Worker_CommandRequestOp&)>& Callback)
{
	return AddGenericOpCallback(ComponentId, WORKER_OP_TYPE_COMMAND_REQUEST, [Callback](const Worker_Op* Op) {
		Callback(Op->op.command_request);
	});
}

SpatialDispatcher::FCallbackId SpatialDispatcher::OnCommandResponse(Worker_ComponentId ComponentId,
																	const TFunction<void(const Worker_CommandResponseOp&)>& Callback)
{
	return AddGenericOpCallback(ComponentId, WORKER_OP_TYPE_COMMAND_RESPONSE, [Callback](const Worker_Op* Op) {
		Callback(Op->op.command_response);
	});
}

SpatialDispatcher::FCallbackId SpatialDispatcher::AddGenericOpCallback(Worker_ComponentId ComponentId, Worker_OpType OpType,
																	   const TFunction<void(const Worker_Op*)>& Callback)
{
	if (!ensureAlwaysMsgf(
			SpatialConstants::MIN_EXTERNAL_SCHEMA_ID <= ComponentId && ComponentId <= SpatialConstants::MAX_EXTERNAL_SCHEMA_ID,
			TEXT("Tried to add op callback for external schema component ID outside of permitted range")))
	{
		return -1;
	}
	const FCallbackId NewCallbackId = NextCallbackId++;
	ComponentOpTypeToCallbacksMap.FindOrAdd(ComponentId).FindOrAdd(OpType).Add(UserOpCallbackData{ NewCallbackId, Callback });
	CallbackIdToDataMap.Add(NewCallbackId, CallbackIdData{ ComponentId, OpType });
	return NewCallbackId;
}

bool SpatialDispatcher::RemoveOpCallback(FCallbackId CallbackId)
{
	CallbackIdData* CallbackData = CallbackIdToDataMap.Find(CallbackId);
	if (CallbackData == nullptr)
	{
		return false;
	}

	OpTypeToCallbacksMap* OpTypesToCallbacks = ComponentOpTypeToCallbacksMap.Find(CallbackData->ComponentId);
	if (OpTypesToCallbacks == nullptr)
	{
		return false;
	}

	TArray<UserOpCallbackData>* ComponentCallbacks = OpTypesToCallbacks->Find(CallbackData->OpType);
	if (ComponentCallbacks == nullptr)
	{
		return false;
	}

	int32 CallbackIndex = ComponentCallbacks->IndexOfByPredicate([CallbackId](const UserOpCallbackData& Data) {
		return Data.Id == CallbackId;
	});
	if (CallbackIndex == INDEX_NONE)
	{
		return false;
	}

	// If removing the only callback for a component ID / op type, delete map entries as applicable
	if (ComponentCallbacks->Num() == 1)
	{
		if (OpTypesToCallbacks->Num() == 1)
		{
			ComponentOpTypeToCallbacksMap.Remove(CallbackData->ComponentId);
			return true;
		}
		OpTypesToCallbacks->Remove(CallbackData->OpType);
		return true;
	}

	ComponentCallbacks->RemoveAt(CallbackIndex);
	return true;
}

void SpatialDispatcher::RunCallbacks(Worker_ComponentId ComponentId, const Worker_Op* Op)
{
	OpTypeToCallbacksMap* OpTypeCallbacks = ComponentOpTypeToCallbacksMap.Find(ComponentId);
	if (OpTypeCallbacks == nullptr)
	{
		return;
	}

	TArray<UserOpCallbackData>* ComponentCallbacks = OpTypeCallbacks->Find(static_cast<Worker_OpType>(Op->op_type));
	if (ComponentCallbacks == nullptr)
	{
		return;
	}

	for (UserOpCallbackData CallbackData : *ComponentCallbacks)
	{
		CallbackData.Callback(Op);
	}
}
