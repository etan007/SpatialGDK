// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "Utils/SpatialLatencyTracer.h"

#include "Async/Async.h"
#include "Engine/World.h"
#include "EngineClasses/SpatialGameInstance.h"
#include "GeneralProjectSettings.h"
#include "Interop/Connection/OutgoingMessages.h"
#include "Utils/GDKPropertyMacros.h"
#include "Utils/SchemaUtils.h"

#include <sstream>

DEFINE_LOG_CATEGORY(LogSpatialLatencyTracing);

DECLARE_CYCLE_STAT(TEXT("ContinueLatencyTraceRPC_Internal"), STAT_ContinueLatencyTraceRPC_Internal, STATGROUP_SpatialNet);
DECLARE_CYCLE_STAT(TEXT("BeginLatencyTraceRPC_Internal"), STAT_BeginLatencyTraceRPC_Internal, STATGROUP_SpatialNet);

namespace
{
// Stream for piping trace lib output to UE output
class UEStream : public std::stringbuf
{
	int sync() override
	{
		UE_LOG(LogSpatialLatencyTracing, Verbose, TEXT("%s"), *FString(str().c_str()));
		str("");
		return std::stringbuf::sync();
	}

public:
	virtual ~UEStream() override { sync(); }
};

UEStream UStream;

#if TRACE_LIB_ACTIVE
const improbable::legacy::trace::SpanContext ReadSpanContext(const void* TraceBytes, const void* SpanBytes)
{
	improbable::legacy::trace::TraceId _TraceId;
	memcpy(&_TraceId[0], TraceBytes, sizeof(improbable::legacy::trace::TraceId));

	improbable::legacy::trace::SpanId _SpanId;
	memcpy(&_SpanId[0], SpanBytes, sizeof(improbable::legacy::trace::SpanId));

	return { _TraceId, _SpanId };
}
#endif
} // anonymous namespace

USpatialLatencyTracer::USpatialLatencyTracer()
{
#if TRACE_LIB_ACTIVE
	ResetWorkerId();
	FParse::Value(FCommandLine::Get(), TEXT("traceMetadata="), TraceMetadata);
#endif
}

void USpatialLatencyTracer::RegisterProject(UObject* WorldContextObject, const FString& ProjectId)
{
#if TRACE_LIB_ACTIVE
	using namespace improbable::legacy::exporters::trace;

	StackdriverExporter::Register({ TCHAR_TO_UTF8(*ProjectId) });

	if (UE_GET_LOG_VERBOSITY(LogSpatialLatencyTracing) >= ELogVerbosity::Verbose)
	{
		std::cout.rdbuf(&UStream);
		std::cerr.rdbuf(&UStream);

		StdoutExporter::Register();
	}
#endif // TRACE_LIB_ACTIVE
}

bool USpatialLatencyTracer::SetTraceMetadata(UObject* WorldContextObject, const FString& NewTraceMetadata)
{
#if TRACE_LIB_ACTIVE
	if (USpatialLatencyTracer* Tracer = GetTracer(WorldContextObject))
	{
		Tracer->TraceMetadata = NewTraceMetadata;
		return true;
	}
#endif // TRACE_LIB_ACTIVE
	return false;
}

bool USpatialLatencyTracer::BeginLatencyTrace(UObject* WorldContextObject, const FString& TraceDesc,
											  FSpatialLatencyPayload& OutLatencyPayload)
{
#if TRACE_LIB_ACTIVE
	if (USpatialLatencyTracer* Tracer = GetTracer(WorldContextObject))
	{
		return Tracer->BeginLatencyTrace_Internal(TraceDesc, OutLatencyPayload);
	}
#endif // TRACE_LIB_ACTIVE
	return false;
}

bool USpatialLatencyTracer::ContinueLatencyTraceRPC(UObject* WorldContextObject, const AActor* Actor, const FString& FunctionName,
													const FString& TraceDesc, const FSpatialLatencyPayload& LatencyPayload,
													FSpatialLatencyPayload& OutContinuedLatencyPayload)
{
#if TRACE_LIB_ACTIVE
	if (USpatialLatencyTracer* Tracer = GetTracer(WorldContextObject))
	{
		return Tracer->ContinueLatencyTrace_Internal(Actor, FunctionName, ETraceType::RPC, TraceDesc, LatencyPayload,
													 OutContinuedLatencyPayload);
	}
#endif // TRACE_LIB_ACTIVE
	return false;
}

bool USpatialLatencyTracer::ContinueLatencyTraceProperty(UObject* WorldContextObject, const AActor* Actor, const FString& PropertyName,
														 const FString& TraceDesc, const FSpatialLatencyPayload& LatencyPayload,
														 FSpatialLatencyPayload& OutContinuedLatencyPayload)
{
#if TRACE_LIB_ACTIVE
	if (USpatialLatencyTracer* Tracer = GetTracer(WorldContextObject))
	{
		return Tracer->ContinueLatencyTrace_Internal(Actor, PropertyName, ETraceType::Property, TraceDesc, LatencyPayload,
													 OutContinuedLatencyPayload);
	}
#endif // TRACE_LIB_ACTIVE
	return false;
}

bool USpatialLatencyTracer::ContinueLatencyTraceTagged(UObject* WorldContextObject, const AActor* Actor, const FString& Tag,
													   const FString& TraceDesc, const FSpatialLatencyPayload& LatencyPayload,
													   FSpatialLatencyPayload& OutContinuedLatencyPayload)
{
#if TRACE_LIB_ACTIVE
	if (USpatialLatencyTracer* Tracer = GetTracer(WorldContextObject))
	{
		return Tracer->ContinueLatencyTrace_Internal(Actor, Tag, ETraceType::Tagged, TraceDesc, LatencyPayload, OutContinuedLatencyPayload);
	}
#endif // TRACE_LIB_ACTIVE
	return false;
}

bool USpatialLatencyTracer::EndLatencyTrace(UObject* WorldContextObject, const FSpatialLatencyPayload& LatencyPayload)
{
#if TRACE_LIB_ACTIVE
	if (USpatialLatencyTracer* Tracer = GetTracer(WorldContextObject))
	{
		return Tracer->EndLatencyTrace_Internal(LatencyPayload);
	}
#endif // TRACE_LIB_ACTIVE
	return false;
}

FSpatialLatencyPayload USpatialLatencyTracer::RetrievePayload(UObject* WorldContextObject, const AActor* Actor, const FString& Tag)
{
#if TRACE_LIB_ACTIVE
	if (USpatialLatencyTracer* Tracer = GetTracer(WorldContextObject))
	{
		return Tracer->RetrievePayload_Internal(Actor, Tag);
	}
#endif
	return FSpatialLatencyPayload{};
}

USpatialLatencyTracer* USpatialLatencyTracer::GetTracer(UObject* WorldContextObject)
{
#if TRACE_LIB_ACTIVE
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (World == nullptr)
	{
		World = GWorld;
	}

	if (USpatialGameInstance* GameInstance = World->GetGameInstance<USpatialGameInstance>())
	{
		return GameInstance->GetSpatialLatencyTracer();
	}
#endif
	return nullptr;
}

FString USpatialLatencyTracer::GetTraceMetadata(UObject* WorldContextObject)
{
#if TRACE_LIB_ACTIVE
	if (USpatialLatencyTracer* Tracer = GetTracer(WorldContextObject))
	{
		return Tracer->TraceMetadata;
	}
#endif
	return TEXT("");
}

#if TRACE_LIB_ACTIVE
bool USpatialLatencyTracer::IsValidKey(const TraceKey Key)
{
	FScopeLock Lock(&Mutex);
	return (TraceMap.Find(Key) != nullptr);
}

TraceKey USpatialLatencyTracer::RetrievePendingTrace(const UObject* Obj, const FString& Tag)
{
	FScopeLock Lock(&Mutex);

	ActorTagKey EventKey{ Cast<AActor>(Obj), Tag };
	TraceKey ReturnKey = InvalidTraceKey;
	TrackingTags.RemoveAndCopyValue(EventKey, ReturnKey);
	return ReturnKey;
}

void USpatialLatencyTracer::WriteToLatencyTrace(const TraceKey Key, const FString& TraceDesc)
{
	FScopeLock Lock(&Mutex);

	if (TraceSpan* Trace = TraceMap.Find(Key))
	{
		WriteKeyFrameToTrace(Trace, TraceDesc);
	}
}

void USpatialLatencyTracer::WriteAndEndTrace(const TraceKey Key, const FString& TraceDesc, bool bOnlyEndIfTraceRootIsRemote)
{
	FScopeLock Lock(&Mutex);

	if (TraceSpan* Trace = TraceMap.Find(Key))
	{
		WriteKeyFrameToTrace(Trace, TraceDesc);

		// Check RootTraces to verify if this trace was started locally. If it was, we don't End the trace yet, but
		// wait for an explicit call to EndLatencyTrace.
		if (!bOnlyEndIfTraceRootIsRemote || RootTraces.Find(Key) == nullptr)
		{
			Trace->End();
			TraceMap.Remove(Key);
		}
	}
}

FSpatialLatencyPayload USpatialLatencyTracer::RetrievePayload_Internal(const UObject* Obj, const FString& Tag)
{
	FScopeLock Lock(&Mutex);

	TraceKey Key = RetrievePendingTrace(Obj, Tag);
	if (Key != InvalidTraceKey)
	{
		if (const TraceSpan* Span = TraceMap.Find(Key))
		{
			const improbable::legacy::trace::SpanContext& TraceContext = Span->context();

			TArray<uint8> TraceBytes =
				TArray<uint8_t>((const uint8_t*)&TraceContext.trace_id()[0], sizeof(improbable::legacy::trace::TraceId));
			TArray<uint8> SpanBytes =
				TArray<uint8_t>((const uint8_t*)&TraceContext.span_id()[0], sizeof(improbable::legacy::trace::SpanId));
			return FSpatialLatencyPayload(MoveTemp(TraceBytes), MoveTemp(SpanBytes), Key);
		}
	}
	return {};
}

void USpatialLatencyTracer::ResetWorkerId()
{
	WorkerId = TEXT("DeviceId_") + FPlatformMisc::GetDeviceId();
}

bool USpatialLatencyTracer::BeginLatencyTrace_Internal(const FString& TraceDesc, FSpatialLatencyPayload& OutLatencyPayload)
{
	// TODO: UNR-2787 - Improve mutex-related latency
	// This functions might spike because of the Mutex below
	SCOPE_CYCLE_COUNTER(STAT_BeginLatencyTraceRPC_Internal);
	FScopeLock Lock(&Mutex);

	FString SpanMsg = FormatMessage(TraceDesc, true);
	TraceSpan NewTrace = improbable::legacy::trace::Span::StartSpan(TCHAR_TO_UTF8(*SpanMsg), nullptr);

	// Construct payload data from trace
	const improbable::legacy::trace::SpanContext& TraceContext = NewTrace.context();

	{
		TArray<uint8> TraceBytes = TArray<uint8_t>((const uint8_t*)&TraceContext.trace_id()[0], sizeof(improbable::legacy::trace::TraceId));
		TArray<uint8> SpanBytes = TArray<uint8_t>((const uint8_t*)&TraceContext.span_id()[0], sizeof(improbable::legacy::trace::SpanId));
		OutLatencyPayload = FSpatialLatencyPayload(MoveTemp(TraceBytes), MoveTemp(SpanBytes), GenerateNewTraceKey());
	}

	// Add to internal tracking
	TraceMap.Add(OutLatencyPayload.Key, MoveTemp(NewTrace));

	// Store traces started on this worker, so we can persist them until they've been round trip returned.
	RootTraces.Add(OutLatencyPayload.Key);

	return true;
}

bool USpatialLatencyTracer::ContinueLatencyTrace_Internal(const AActor* Actor, const FString& Target, ETraceType::Type Type,
														  const FString& TraceDesc, const FSpatialLatencyPayload& LatencyPayload,
														  FSpatialLatencyPayload& OutLatencyPayload)
{
	// TODO: UNR-2787 - Improve mutex-related latency
	// This functions might spike because of the Mutex below
	SCOPE_CYCLE_COUNTER(STAT_ContinueLatencyTraceRPC_Internal);
	if (Actor == nullptr)
	{
		return false;
	}

	// We do minimal internal tracking for native rpcs/properties
	const bool bInternalTracking = Type == ETraceType::Tagged; // GDK now also ends traces in the same way native does, ticket here UNR-4672

	FScopeLock Lock(&Mutex);

	OutLatencyPayload = LatencyPayload;
	if (OutLatencyPayload.Key == InvalidTraceKey)
	{
		ResolveKeyInLatencyPayload(OutLatencyPayload);
	}

	const TraceKey Key = OutLatencyPayload.Key;
	const TraceSpan* ActiveTrace = TraceMap.Find(Key);
	if (ActiveTrace == nullptr)
	{
		UE_LOG(LogSpatialLatencyTracing, Warning, TEXT("(%s) : No active trace to continue (%s)"), *WorkerId, *TraceDesc);
		return false;
	}

	if (bInternalTracking)
	{
		if (!AddTrackingInfo(Actor, Target, Type, Key))
		{
			UE_LOG(LogSpatialLatencyTracing, Warning, TEXT("(%s) : Failed to create Actor/Func trace (%s)"), *WorkerId, *TraceDesc);
			return false;
		}
	}

	WriteKeyFrameToTrace(ActiveTrace, FString::Printf(TEXT("Continue [%s] %s - %s"), *TraceDesc, *UEnum::GetValueAsString(Type), *Target));

	// If we're not doing any further tracking, end the trace
	if (!bInternalTracking)
	{
		WriteAndEndTrace(Key, TEXT("Native - End of Tracking"), true);
	}

	return true;
}

bool USpatialLatencyTracer::EndLatencyTrace_Internal(const FSpatialLatencyPayload& LatencyPayload)
{
	FScopeLock Lock(&Mutex);

	// Create temp payload to resolve key
	FSpatialLatencyPayload LocalLatencyPayload = LatencyPayload;
	if (LocalLatencyPayload.Key == InvalidTraceKey)
	{
		ResolveKeyInLatencyPayload(LocalLatencyPayload);
	}

	const TraceKey Key = LocalLatencyPayload.Key;
	const TraceSpan* ActiveTrace = TraceMap.Find(Key);
	if (ActiveTrace == nullptr)
	{
		UE_LOG(LogSpatialLatencyTracing, Warning, TEXT("(%s) : No active trace to end"), *WorkerId);
		return false;
	}

	WriteKeyFrameToTrace(ActiveTrace, TEXT("End"));

	ActiveTrace->End();

	TraceMap.Remove(Key);
	RootTraces.Remove(Key);

	return true;
}

bool USpatialLatencyTracer::AddTrackingInfo(const AActor* Actor, const FString& Target, const ETraceType::Type Type, const TraceKey Key)
{
	if (Actor == nullptr)
	{
		return false;
	}

	if (UClass* ActorClass = Actor->GetClass())
	{
		switch (Type)
		{
		case ETraceType::RPC:
			if (const UFunction* Function = ActorClass->FindFunctionByName(*Target))
			{
				ActorFuncKey AFKey{ Actor, Function };
				if (TrackingRPCs.Find(AFKey) == nullptr)
				{
					TrackingRPCs.Add(AFKey, Key);
					return true;
				}
				UE_LOG(LogSpatialLatencyTracing, Warning, TEXT("(%s) : ActorFunc already exists for trace"), *WorkerId);
			}
			break;
		case ETraceType::Property:
			if (const GDK_PROPERTY(Property)* Property = ActorClass->FindPropertyByName(*Target))
			{
				ActorPropertyKey APKey{ Actor, Property };
				if (TrackingProperties.Find(APKey) == nullptr)
				{
					TrackingProperties.Add(APKey, Key);
					return true;
				}
				UE_LOG(LogSpatialLatencyTracing, Warning, TEXT("(%s) : ActorProperty already exists for trace"), *WorkerId);
			}
			break;
		case ETraceType::Tagged:
		{
			ActorTagKey ATKey{ Actor, Target };
			if (TrackingTags.Find(ATKey) == nullptr)
			{
				TrackingTags.Add(ATKey, Key);
				return true;
			}
			UE_LOG(LogSpatialLatencyTracing, Warning, TEXT("(%s) : ActorTag already exists for trace"), *WorkerId);
		}
		break;
		}
	}

	return false;
}

TraceKey USpatialLatencyTracer::GenerateNewTraceKey()
{
	return NextTraceKey++;
}

void USpatialLatencyTracer::ResolveKeyInLatencyPayload(FSpatialLatencyPayload& Payload)
{
	// Key isn't set, so attempt to find it in the trace map
	for (const auto& TracePair : TraceMap)
	{
		const TraceKey& Key = TracePair.Key;
		const TraceSpan& Span = TracePair.Value;

		if (memcmp(Span.context().trace_id().data(), Payload.TraceId.GetData(), Payload.TraceId.Num()) == 0)
		{
			WriteKeyFrameToTrace(&Span, TEXT("Local Trace - Payload Obj Read"));
			Payload.Key = Key;
			break;
		}
	}

	if (Payload.Key == InvalidTraceKey)
	{
		// Uninitialized key, generate and add to map
		Payload.Key = GenerateNewTraceKey();

		const improbable::legacy::trace::SpanContext DestContext = ReadSpanContext(Payload.TraceId.GetData(), Payload.SpanId.GetData());

		FString SpanMsg = FormatMessage(TEXT("Remote Parent Trace - Payload Obj Read"));
		TraceSpan RetrieveTrace = improbable::legacy::trace::Span::StartSpanWithRemoteParent(TCHAR_TO_UTF8(*SpanMsg), DestContext);

		TraceMap.Add(Payload.Key, MoveTemp(RetrieveTrace));
	}
}

void USpatialLatencyTracer::WriteKeyFrameToTrace(const TraceSpan* Trace, const FString& TraceDesc)
{
	if (Trace != nullptr)
	{
		FString TraceMsg = FormatMessage(TraceDesc);
		improbable::legacy::trace::Span::StartSpan(TCHAR_TO_UTF8(*TraceMsg), Trace).End();
	}
}

FString USpatialLatencyTracer::FormatMessage(const FString& Message, bool bIncludeMetadata) const
{
	if (bIncludeMetadata)
	{
		return FString::Printf(TEXT("%s (%s : %s)"), *Message, *TraceMetadata, *WorkerId.Left(18));
	}
	else
	{
		return FString::Printf(TEXT("%s (%s)"), *Message, *WorkerId.Left(18));
	}
}

#endif // TRACE_LIB_ACTIVE

void USpatialLatencyTracer::Debug_SendTestTrace()
{
#if TRACE_LIB_ACTIVE
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [] {
		using namespace improbable::legacy::trace;

		std::cout << "Sending test trace" << std::endl;

		Span RootSpan = Span::StartSpan("Example Span", nullptr);

		{
			Span SubSpan1 = Span::StartSpan("Sub span 1", &RootSpan);
			FPlatformProcess::Sleep(1);
			SubSpan1.End();
		}

		{
			Span SubSpan2 = Span::StartSpan("Sub span 2", &RootSpan);
			FPlatformProcess::Sleep(1);
			SubSpan2.End();
		}

		FPlatformProcess::Sleep(1);

		// recreate Span from context
		const SpanContext& SourceContext = RootSpan.context();
		auto TraceId = SourceContext.trace_id();
		auto SpanId = SourceContext.span_id();
		RootSpan.End();

		SpanContext DestContext(TraceId, SpanId);

		{
			Span SubSpan3 = Span::StartSpanWithRemoteParent("SubSpan 3", DestContext);
			SubSpan3.AddAnnotation("Starting sub span");
			FPlatformProcess::Sleep(1);
			SubSpan3.End();
		}
	});
#endif // TRACE_LIB_ACTIVE
}
