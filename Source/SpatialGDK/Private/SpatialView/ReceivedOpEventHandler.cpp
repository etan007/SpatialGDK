// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "SpatialView/ReceivedOpEventHandler.h"
#include "SpatialGDK/Public/EngineClasses/SpatialNetDriver.h"
#include "Interop/Connection/SpatialWorkerConnection.h"
DECLARE_LOG_CATEGORY_EXTERN(LogReceivedOpEvent, Log, All);
DEFINE_LOG_CATEGORY(LogReceivedOpEvent);
namespace SpatialGDK
{
FReceivedOpEventHandler::FReceivedOpEventHandler(TSharedPtr<SpatialEventTracer> EventTracer)
	: EventTracer(MoveTemp(EventTracer))
{
}

void FReceivedOpEventHandler::ProcessOpLists(const OpList& Ops)
{
	for (uint32 i = 0; i < Ops.Count; ++i)
	{
		Worker_Op& Op = Ops.Ops[i];
		Worker_EntityId work_system_id = 0;
		USpatialNetDriver* SpatialNetDriver = Cast<USpatialNetDriver>(GWorld->GetWorld()->GetNetDriver());
		if(SpatialNetDriver)
			work_system_id = SpatialNetDriver->Connection->GetWorkerSystemEntityId();
		switch (static_cast<Worker_OpType>(Op.op_type))
		{
		case WORKER_OP_TYPE_ADD_ENTITY:
			//if(!GWorld->GetWorld()->IsServer())
			{
				if(0)
				{
					UE_LOG(LogReceivedOpEvent, Log, TEXT("work_system_id %lld,%s,AddEntity EntityId %lld"),
							   work_system_id, GWorld->GetWorld()->IsServer()?TEXT("Server"):TEXT("Client"),
							   Op.op.add_component.entity_id);
				}

			}
			//EventTracer->AddEntity(Op.op.add_entity, FSpatialGDKSpanId(Op.span_id));
			break;
		case WORKER_OP_TYPE_REMOVE_ENTITY:
			{
				if(0)
				{
					UE_LOG(LogReceivedOpEvent, Log, TEXT("work_system_id %lld,%s,RemoveEntity EntityId %lld"),
					   work_system_id, GWorld->GetWorld()->IsServer()?TEXT("Server"):TEXT("Client"),
					   Op.op.remove_entity.entity_id);
				}
			}
			//EventTracer->RemoveEntity(Op.op.remove_entity, FSpatialGDKSpanId(Op.span_id));
			break;
		case WORKER_OP_TYPE_ADD_COMPONENT:
			//if(!GWorld->GetWorld()->IsServer())
			if(0)
			{
				int a = 1;
				UE_LOG(LogReceivedOpEvent, Log,
						   TEXT("work_system_id %lld, %s,AddComponent EntityId %lld, add_component: %d"),
						   work_system_id, GWorld->GetWorld()->IsServer()?TEXT("Server"):TEXT("Client"),
						   Op.op.add_component.entity_id, Op.op.add_component.data.component_id);
			}
			//EventTracer->AddComponent(Op.op.add_component, FSpatialGDKSpanId(Op.span_id));
			break;
		case WORKER_OP_TYPE_REMOVE_COMPONENT:
			{
				if(0)
				{
					UE_LOG(LogReceivedOpEvent, Log, TEXT("work_system_id %lld,%s,RemoveComponent EntityId %lld cid %d" ),
						   work_system_id, GWorld->GetWorld()->IsServer()?TEXT("Server"):TEXT("Client"),
						   Op.op.remove_component.entity_id,Op.op.remove_component.component_id);
				}

			}
			//EventTracer->RemoveComponent(Op.op.remove_component, FSpatialGDKSpanId(Op.span_id));
			break;
		case WORKER_OP_TYPE_COMPONENT_SET_AUTHORITY_CHANGE:
			//if(!GWorld->GetWorld()->IsServer())
			if(0)
			{
				int a = 1;
				FString str_cids;
				for(uint32_t j=0;j<Op.op.component_set_authority_change.canonical_component_set_data_count;j++)
				{
					//FString str;
					//str.FromInt(Op.op.component_set_authority_change.canonical_component_set_data[j].component_id);
					//auto str = string::IntToString(Op.op.component_set_authority_change.canonical_component_set_data[j].component_id);
					wchar_t result[22u] = {};
					_itow_s(static_cast<int>(Op.op.component_set_authority_change.canonical_component_set_data[j].component_id), result, 10);
					std::wstring wstr(result);
					wstr += TEXT(" ");
					str_cids += wstr.c_str();
					//str_cids += UTF8_TO_TCHAR(str.c_str());
				}
				UE_LOG(LogReceivedOpEvent, Log,
						   TEXT(
							   "work_system_id %lld,%s,SET_AUTHORITY_CHANGE EntityId %lld, component_set_id: %d,authority:%d,cids:%s"
						   ), work_system_id, GWorld->GetWorld()->IsServer()?TEXT("Server"):TEXT("Client"),
						   Op.op.component_set_authority_change.entity_id,
						   Op.op.component_set_authority_change.component_set_id,
						   Op.op.component_set_authority_change.authority, *str_cids);

			}
			//EventTracer->AuthorityChange(Op.op.component_set_authority_change, FSpatialGDKSpanId(Op.span_id));
			break;
		case WORKER_OP_TYPE_COMPONENT_UPDATE:
			{
				if(0)
				{
					auto cid = Op.op.component_update.update.component_id;
					if(cid!=9977 && cid!=9978 && cid<10000)
					{
						//if(Op.op.component_update.update.component_id == 9977 || Op.op.component_update.update.component_id == 9978)
						UE_LOG(LogReceivedOpEvent, Log, TEXT("work_system_id %lld,%s,component_update EntityId %lld, component: %d"),work_system_id,GWorld->GetWorld()->IsServer()?TEXT("Server"):TEXT("Client"),
						  Op.op.component_update.entity_id, Op.op.component_update.update.component_id);

					}
				}

				//EventTracer->UpdateComponent(Op.op.component_update, FSpatialGDKSpanId(Op.span_id));
			}

			break;
		case WORKER_OP_TYPE_COMMAND_REQUEST:
			//EventTracer->CommandRequest(Op.op.command_request, FSpatialGDKSpanId(Op.span_id));
			break;
		default:
			break;
		}
	}

	if (EventTracer == nullptr)
	{
		return;
	}

	EventTracer->BeginOpsForFrame();
	for (uint32 i = 0; i < Ops.Count; ++i)
	{
		Worker_Op& Op = Ops.Ops[i];

		switch (static_cast<Worker_OpType>(Op.op_type))
		{
		case WORKER_OP_TYPE_ADD_ENTITY:
			if(!GWorld->GetWorld()->IsServer())
			{
				int a = 1;
			}
			EventTracer->AddEntity(Op.op.add_entity, FSpatialGDKSpanId(Op.span_id));
			break;
		case WORKER_OP_TYPE_REMOVE_ENTITY:
			EventTracer->RemoveEntity(Op.op.remove_entity, FSpatialGDKSpanId(Op.span_id));
			break;
		case WORKER_OP_TYPE_ADD_COMPONENT:
			if(!GWorld->GetWorld()->IsServer())
			{
				int a = 1;
			}
			EventTracer->AddComponent(Op.op.add_component, FSpatialGDKSpanId(Op.span_id));
			break;
		case WORKER_OP_TYPE_REMOVE_COMPONENT:
			EventTracer->RemoveComponent(Op.op.remove_component, FSpatialGDKSpanId(Op.span_id));
			break;
		case WORKER_OP_TYPE_COMPONENT_SET_AUTHORITY_CHANGE:
			EventTracer->AuthorityChange(Op.op.component_set_authority_change, FSpatialGDKSpanId(Op.span_id));
			break;
		case WORKER_OP_TYPE_COMPONENT_UPDATE:
			EventTracer->UpdateComponent(Op.op.component_update, FSpatialGDKSpanId(Op.span_id));
			break;
		case WORKER_OP_TYPE_COMMAND_REQUEST:
			EventTracer->CommandRequest(Op.op.command_request, FSpatialGDKSpanId(Op.span_id));
			break;
		default:
			break;
		}
	}
}

} // namespace SpatialGDK
