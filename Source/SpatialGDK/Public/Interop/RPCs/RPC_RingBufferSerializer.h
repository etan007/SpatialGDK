// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "Interop/RPCs/RPCTypes.h"

namespace SpatialGDK
{
template <typename Payload>
class RingBufferSerializer_Schema
{
public:
	RingBufferSerializer_Schema(Worker_ComponentId InComponentId, Schema_FieldId InCountFieldId,
								Schema_FieldId InFirstRingBufferSlotFieldId, Worker_ComponentId InACKComponentId,
								Schema_FieldId InACKCountFieldId)
		: ComponentId(InComponentId)
		, CountFieldId(InCountFieldId)
		, FirstRingBufferSlotFieldId(InFirstRingBufferSlotFieldId)
		, ACKComponentId(InACKComponentId)
		, ACKCountFieldId(InACKCountFieldId)
	{
		check(ComponentId != 0);
		check(ACKComponentId != 0);
	}

	Worker_ComponentId GetComponentId() { return ComponentId; }

	Worker_ComponentId GetACKComponentId() { return ACKComponentId; }

	TOptional<uint64> ReadRPCCount(const RPCReadingContext& Ctx)
	{
		if (Schema_GetUint64Count(Ctx.Fields, CountFieldId))
		{
			return Schema_GetUint64(Ctx.Fields, CountFieldId);
		}
		return {};
	}

	TOptional<uint64> ReadACKCount(const RPCReadingContext& Ctx)
	{
		if (Schema_GetUint64Count(Ctx.Fields, ACKCountFieldId))
		{
			return Schema_GetUint64(Ctx.Fields, ACKCountFieldId);
		}
		return {};
	}

	bool ReadRPC(const RPCReadingContext& Ctx, uint32 Slot, Payload& OutPayload)
	{
		Schema_Object* PayloadObject = Schema_GetObject(Ctx.Fields, FirstRingBufferSlotFieldId + Slot);
		if(!PayloadObject)
			return false;
		if (ensure(PayloadObject))
		{
			OutPayload.ReadFromSchema(PayloadObject);
		}
		return true;
	}

	void WriteRPC(RPCWritingContext::EntityWrite& Ctx, uint32 Slot, const Payload& InPayload)
	{
		Schema_Object* NewField = Schema_AddObject(Ctx.GetFieldsToWrite(), Slot + FirstRingBufferSlotFieldId);
		InPayload.WriteToSchema(NewField);
	}

	void WriteRPCCount(RPCWritingContext::EntityWrite& Ctx, uint64 Count)
	{
		bool isserver = GWorld->GetWorld()->IsServer();
		if(!isserver)
		{
			int aaa = 1;
		}

		Schema_AddUint64(Ctx.GetFieldsToWrite(), CountFieldId, Count);
	}

	void WriteACKCount(RPCWritingContext::EntityWrite& Ctx, uint64 Count)
	{
		Schema_AddUint64(Ctx.GetFieldsToWrite(), ACKCountFieldId, Count);
	}

private:
	const Worker_ComponentId ComponentId;
	const Schema_FieldId CountFieldId;
	const Schema_FieldId FirstRingBufferSlotFieldId;

	const Worker_ComponentId ACKComponentId;
	const Schema_FieldId ACKCountFieldId;
};

} // namespace SpatialGDK
