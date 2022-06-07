// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Interop/SpatialOSDispatcherInterface.h"
#include "SpatialCommonTypes.h"

#include <WorkerSDK/improbable/c_schema.h>
#include <WorkerSDK/improbable/c_worker.h>

#include "Connection/SpatialOSWorkerInterface.h"

DECLARE_CYCLE_STAT(TEXT("EntityQueryHandler"), STAT_EntityQueryHandler, STATGROUP_SpatialNet);

namespace SpatialGDK
{
class EntityQueryHandler
{
public:
	void ProcessOps(const TArray<Worker_Op>& Ops)
	{
		SCOPE_CYCLE_COUNTER(STAT_EntityQueryHandler);

		for (const Worker_Op& Op : Ops)
		{
			if (Op.op_type == WORKER_OP_TYPE_ENTITY_QUERY_RESPONSE)
			{
				const Worker_EntityQueryResponseOp& TypedOp = Op.op.entity_query_response;
				const Worker_RequestId& RequestId = TypedOp.request_id;

				EntityQueryDelegate CallableToCall;
				if (Handlers.RemoveAndCopyValue(RequestId, CallableToCall))
				{
					if (ensure(CallableToCall.IsBound()))
					{
						CallableToCall.Execute(TypedOp);
					}
				}
			}
		}
	}

	void AddRequest(Worker_RequestId RequestId, const EntityQueryDelegate& Callable) { Handlers.Add(RequestId, Callable); }

private:
	TMap<Worker_RequestId_Key, EntityQueryDelegate> Handlers;
};
} // namespace SpatialGDK
