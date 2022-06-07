// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "Interop/Connection/SpatialTraceEventDataBuilder.h"

#include <inttypes.h>

#include "EngineClasses/SpatialNetDriver.h"
#include "EngineClasses/SpatialPackageMapClient.h"
#include "Utils/SpatialActorUtils.h"

#include "Math/UnrealMathUtility.h"

DEFINE_LOG_CATEGORY(FSpatialTraceEventDataBuilderLog);

namespace SpatialGDK
{
// ---- FStringCache ----

int32 FSpatialTraceEventDataBuilder::FStringCache::CombineStrings(const char* A, const char* B)
{
	int32 InsertIndex = NextIndex;
	AddString(A);
	NextIndex--;
	AddString(B);
	return InsertIndex;
}

int32 FSpatialTraceEventDataBuilder::FStringCache::AddString(const char* String)
{
	if (NextIndex >= BufferSize)
	{
		return BufferSize - 1;
	}

	int32 InsertIndex = NextIndex;
	int32 InitialRemainingSize = BufferSize - NextIndex;
	FCStringAnsi::Strncpy(&Buffer[NextIndex], String, InitialRemainingSize);

	int32 CharSize = sizeof(char);
	int32 StringLength = CharSize * (FCStringAnsi::Strlen(&Buffer[NextIndex]) + 1);
	int32 RemainingSize = InitialRemainingSize - StringLength;

	NextIndex += InitialRemainingSize - RemainingSize;
	return InsertIndex;
}

int32 FSpatialTraceEventDataBuilder::FStringCache::AddFString(const FString& String)
{
	return AddString(TCHAR_TO_ANSI(*String));
}

int32 FSpatialTraceEventDataBuilder::FStringCache::AddUInt32(uint32 Value)
{
	return AddInteger(Value, "%" PRIu32);
}

int32 FSpatialTraceEventDataBuilder::FStringCache::AddUInt64(uint64 Value)
{
	return AddInteger(Value, "%" PRIu64);
}

int32 FSpatialTraceEventDataBuilder::FStringCache::AddInt32(int32 Value)
{
	return AddInteger(Value, "%" PRId32);
}

int32 FSpatialTraceEventDataBuilder::FStringCache::AddInt64(int64 Value)
{
	return AddInteger(Value, "%" PRId64);
}

const char* FSpatialTraceEventDataBuilder::FStringCache::Get(int32 Handle) const
{
	return &Buffer[Handle];
}

// ---- FSpatialTraceEventDataBuilder ----

FSpatialTraceEventDataBuilder::FSpatialTraceEventDataBuilder()
	: EventData(Trace_EventData_Create())
{
}

FSpatialTraceEventDataBuilder::~FSpatialTraceEventDataBuilder()
{
	Trace_EventData_Destroy(EventData);
}

void FSpatialTraceEventDataBuilder::AddObject(const UObject* Object, const char* Key /*="object"*/)
{
	if (Object != nullptr)
	{
		if (const AActor* Actor = Cast<AActor>(Object))
		{
			FString PositionString = Actor->GetTransform().GetTranslation().ToString();
			AddKeyValue(StringConverter.CombineStrings(Key, "actor_position"), StringConverter.AddFString(PositionString));
		}
		if (UWorld* World = Object->GetWorld())
		{
			if (USpatialNetDriver* NetDriver = Cast<USpatialNetDriver>(World->GetNetDriver()))
			{
				AddKeyValue(StringConverter.CombineStrings(Key, "entity_id"),
							StringConverter.AddInt64((NetDriver->PackageMap->GetEntityIdFromObject(Object))));
			}
		}
		AddKeyValue(Key, Object->GetName());
	}
}

void FSpatialTraceEventDataBuilder::AddFunction(const UFunction* Function, const char* Key /*="function"*/)
{
	if (Function != nullptr)
	{
		AddKeyValue(Key, Function->GetName());
	}
}

void FSpatialTraceEventDataBuilder::AddEntityId(const Worker_EntityId EntityId, const char* Key /*="entiy_id"*/)
{
	AddKeyValue(Key, static_cast<int64>(EntityId));
}

void FSpatialTraceEventDataBuilder::AddComponentId(const Worker_ComponentId ComponentId, const char* Key /*="component_id"*/)
{
	AddKeyValue(Key, static_cast<uint32>(ComponentId));
}

void FSpatialTraceEventDataBuilder::AddComponentSetId(const Worker_ComponentSetId ComponentSetId, const char* Key /*="component_set_id"*/)
{
	AddKeyValue(Key, static_cast<uint32>(ComponentSetId));
}

void FSpatialTraceEventDataBuilder::AddFieldId(const uint32 FieldId, const char* Key /*="field_id"*/)
{
	AddKeyValue(Key, FieldId);
}

void FSpatialTraceEventDataBuilder::AddWorkerId(const uint32 WorkerId, const char* Key /*="worker_id"*/)
{
	AddKeyValue(Key, WorkerId);
}

void FSpatialTraceEventDataBuilder::AddCommand(const char* Command, const char* Key /*="command"*/)
{
	AddKeyValue(Key, Command);
}

void FSpatialTraceEventDataBuilder::AddRequestId(const int64 RequestId, const char* Key /*="request_id"*/)
{
	AddKeyValue(Key, RequestId);
}

void FSpatialTraceEventDataBuilder::AddAuthority(const Worker_Authority Authority, const char* Key /*="authority"*/)
{
	AddKeyValue(Key, AuthorityToString(Authority));
}

void FSpatialTraceEventDataBuilder::AddLinearTraceId(const EventTraceUniqueId LinearTraceId, const char* Key /*="linear_trace_id"*/)
{
	AddKeyValue(Key, LinearTraceId.Get());
}

void FSpatialTraceEventDataBuilder::AddKeyValue(const char* Key, const char* Value)
{
	AddKeyValue(StringConverter.AddString(Key), StringConverter.AddString(Value));
}

void FSpatialTraceEventDataBuilder::AddKeyValue(const char* Key, const FString& Value)
{
	AddKeyValue(StringConverter.AddString(Key), StringConverter.AddFString(Value));
}

void FSpatialTraceEventDataBuilder::AddKeyValue(const FString& Key, const FString& Value)
{
	AddKeyValue(StringConverter.AddFString(Key), StringConverter.AddFString(Value));
}

void FSpatialTraceEventDataBuilder::AddKeyValue(const char* Key, const uint32 Value)
{
	AddKeyValue(StringConverter.AddString(Key), StringConverter.AddUInt32(Value));
}

void FSpatialTraceEventDataBuilder::AddKeyValue(const char* Key, const uint64 Value)
{
	AddKeyValue(StringConverter.AddString(Key), StringConverter.AddUInt64(Value));
}

void FSpatialTraceEventDataBuilder::AddKeyValue(const char* Key, const int32 Value)
{
	AddKeyValue(StringConverter.AddString(Key), StringConverter.AddInt32(Value));
}

void FSpatialTraceEventDataBuilder::AddKeyValue(const char* Key, const int64 Value)
{
	AddKeyValue(StringConverter.AddString(Key), StringConverter.AddInt64(Value));
}

void FSpatialTraceEventDataBuilder::AddKeyValue(const char* Key, const bool bValue)
{
	AddKeyValue(StringConverter.AddString(Key), StringConverter.AddString(bValue ? "true" : "false"));
}

void FSpatialTraceEventDataBuilder::AddKeyValue(int32 KeyHandle, int32 ValueHandle)
{
	const char* Key = StringConverter.Get(KeyHandle);
	const char* Value = StringConverter.Get(ValueHandle);
	Trace_EventData_AddStringFields(EventData, 1, &Key, &Value);
}

const char* FSpatialTraceEventDataBuilder::AuthorityToString(Worker_Authority Authority)
{
	switch (Authority)
	{
	case Worker_Authority::WORKER_AUTHORITY_NOT_AUTHORITATIVE:
		return "not_authoritative";
	case Worker_Authority::WORKER_AUTHORITY_AUTHORITATIVE:
		return "authoritative";
	default:
		return "unknown";
	}
}

} // namespace SpatialGDK
