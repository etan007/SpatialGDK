// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "Interop/RPCExecutor.h"

#include "Interop/Connection/SpatialEventTracer.h"
#include "Interop/SpatialPlayerSpawner.h"
#include "Interop/SpatialReceiver.h"
#include "Interop/SpatialSender.h"
#include "Utils/RepLayoutUtils.h"

namespace SpatialGDK
{
RPCExecutor::RPCExecutor(USpatialNetDriver* InNetDriver, SpatialEventTracer* EventTracer)
	: NetDriver(InNetDriver)
	, EventTracer(EventTracer)
{
}

bool RPCExecutor::ExecuteCommand(const FCrossServerRPCParams& Params)
{
	const TWeakObjectPtr<UObject> TargetObjectWeakPtr = NetDriver->PackageMap->GetObjectFromUnrealObjectRef(Params.ObjectRef);
	if (!TargetObjectWeakPtr.IsValid())
	{
		return false;
	}

	UObject* TargetObject = TargetObjectWeakPtr.Get();
	const FClassInfo& ClassInfo = NetDriver->ClassInfoManager->GetOrCreateClassInfoByObject(TargetObjectWeakPtr.Get());
	UFunction* Function = ClassInfo.RPCs[Params.Payload.Index];
	if (Function == nullptr)
	{
		return true;
	}

	uint8* Parms = static_cast<uint8*>(FMemory_Alloca(Function->ParmsSize));
	FMemory::Memzero(Parms, Function->ParmsSize);

	TSet<FUnrealObjectRef> UnresolvedRefs;
	TSet<FUnrealObjectRef> MappedRefs;
	RPCPayload PayloadCopy = Params.Payload;
	FSpatialNetBitReader PayloadReader(NetDriver->PackageMap, PayloadCopy.PayloadData.GetData(), PayloadCopy.CountDataBits(), MappedRefs,
									   UnresolvedRefs);

	TSharedPtr<FRepLayout> RepLayout = NetDriver->GetFunctionRepLayout(Function);
	RepLayout_ReceivePropertiesForRPC(*RepLayout, PayloadReader, Parms);

	const USpatialGDKSettings* SpatialSettings = GetDefault<USpatialGDKSettings>();

	const float TimeQueued = (FDateTime::Now() - Params.Timestamp).GetTotalSeconds();
	bool CanProcessRPC = UnresolvedRefs.Num() == 0 || SpatialSettings->QueuedIncomingRPCWaitTime < TimeQueued;

	if (CanProcessRPC)
	{
		if (EventTracer != nullptr)
		{
			FSpatialGDKSpanId SpanId =
				EventTracer->TraceEvent(APPLY_CROSS_SERVER_RPC_EVENT_NAME, "", Params.SpanId.GetConstId(), /* NumCauses */ 1,
										[TargetObject, Function](FSpatialTraceEventDataBuilder& EventBuilder) {
											EventBuilder.AddObject(TargetObject);
											EventBuilder.AddFunction(Function);
										});
			EventTracer->AddToStack(SpanId);
		}

		TargetObject->ProcessEvent(Function, Parms);

		if (EventTracer != nullptr)
		{
			EventTracer->PopFromStack();
		}
	}

	// Destroy the parameters.
	// warning: highly dependent on UObject::ProcessEvent freeing of parms!
	for (TFieldIterator<GDK_PROPERTY(Property)> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		It->DestroyValue_InContainer(Parms);
	}

	return CanProcessRPC;
}

TOptional<FCrossServerRPCParams> RPCExecutor::TryRetrieveCrossServerRPCParams(const Worker_Op& Op)
{
	Schema_Object* RequestObject = Schema_GetCommandRequestObject(Op.op.command_request.request.schema_type);
	RPCPayload Payload(RequestObject);
	const FUnrealObjectRef ObjectRef = FUnrealObjectRef(Op.op.command_request.entity_id, Payload.Offset);
	const TWeakObjectPtr<UObject> TargetObjectWeakPtr = NetDriver->PackageMap->GetObjectFromUnrealObjectRef(ObjectRef);
	if (!TargetObjectWeakPtr.IsValid())
	{
		return {};
	}

	UObject* TargetObject = TargetObjectWeakPtr.Get();
	const FClassInfo& ClassInfo = NetDriver->ClassInfoManager->GetOrCreateClassInfoByObject(TargetObject);

	if (Payload.Index >= static_cast<uint32>(ClassInfo.RPCs.Num()))
	{
		// This should only happen if there's a class layout disagreement between workers, which would indicate incompatible binaries.
		return {};
	}

	UFunction* Function = ClassInfo.RPCs[Payload.Index];
	if (Function == nullptr)
	{
		return {};
	}

	const FRPCInfo& RPCInfo = NetDriver->ClassInfoManager->GetRPCInfo(TargetObject, Function);
	if (RPCInfo.Type != ERPCType::CrossServer)
	{
		return {};
	}

	AActor* TargetActor = Cast<AActor>(NetDriver->PackageMap->GetObjectFromEntityId(Op.op.command_request.entity_id));
	FSpatialGDKSpanId SpanId;
	if (EventTracer != nullptr)
	{
		SpanId = EventTracer->TraceEvent(RECEIVE_COMMAND_REQUEST_EVENT_NAME, "", /* Causes */ nullptr, /* NumCauses */ 0,
										 [TargetActor, TargetObject, Function, Op](FSpatialTraceEventDataBuilder& EventBuilder) {
											 EventBuilder.AddCommand("RPC_COMMAND_REQUEST");
											 EventBuilder.AddObject(TargetActor);
											 EventBuilder.AddObject(TargetActor != TargetObject ? TargetObject : nullptr, "target_object");
											 EventBuilder.AddFunction(Function);
											 EventBuilder.AddRequestId(Op.op.command_request.request_id);
										 });
	}

	FCrossServerRPCParams Params(ObjectRef, Op.op.command_request.request_id, MoveTemp(Payload), SpanId);
	return TOptional<FCrossServerRPCParams>(MoveTemp(Params));
}
} // namespace SpatialGDK
