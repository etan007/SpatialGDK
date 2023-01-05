#pragma once
#include "WorkerSDK/improbable/pch.h"
#include "WorkerSDK/improbable/c_io.h"
//#include "SpatialEventTracer.h"

#define TRACE_SPAN_ID_SIZE_BYTES 4

using Trace_SpanIdType = uint8_t;
// TODO:SKYCELL
typedef struct Trace_EventData
{
	
}Trace_EventData;

typedef struct Trace_EventTracer
{
	
}Trace_EventTracer;

typedef struct  Trace_Query
{
	
}Trace_Query;

enum TRACE_TYPE_EVENT
{
	TRACE_ITEM_TYPE_EVENT = 0,
	TRACE_ITEM_TYPE_SPAN,
};

typedef struct Trace_Event
{
	const Trace_SpanIdType* span_id;
	int flag;
	const char* Message;
	const char* type;
	const Trace_EventData* data;
}Trace_Event;

typedef struct Trace_Span
{
	Trace_SpanIdType* id;
	uint64_t cause_count;
	Trace_SpanIdType* causes;
}Trace_Span;

 

typedef struct  Trace_Item
{
	TRACE_TYPE_EVENT item_type;
	//Trace_Item_Detail item;
	union item
	{
		Trace_Event event;
		Trace_Span  span;
	}item;
}Trace_Item;





enum  Trace_SamplingMode
{
	TRACE_SAMPLING_MODE_PROBABILISTIC = 1,
	
};

typedef struct Trace_SpanSamplingProbability
{
	const char* name;
	double value;
	
}Trace_SpanSamplingProbability;

typedef struct Trace_ProbabilisticParameters
{
	double default_probability;
	int32_t probability_count;
	Trace_SpanSamplingProbability* probabilities;
}Trace_ProbabilisticParameters;


typedef struct Span_Sampline_Paramters
{
	Trace_SamplingMode sampling_mode;
	Trace_ProbabilisticParameters probabilistic_parameters;
}Span_Sampline_Paramters;

 

typedef struct Event_Filter_Parameters
{
	 void* simple_query;
}Event_Filter_Parameters;


typedef struct Trace_FilterParameters
{
	Event_Filter_Parameters event_pre_filter_parameters;
	Event_Filter_Parameters event_post_filter_parameters;
}Trace_FilterParameters;

 
typedef struct Trace_EventTracer_Parameters
{
	void* user_data;
	void* callback;
	bool  enabled;
	Span_Sampline_Paramters span_sampling_parameters;
	Trace_FilterParameters filter_parameters;
}Trace_EventTracer_Parameters;



WORKERSDK_API Trace_SpanIdType
	Trace_SpanId_IsNull(const Trace_SpanIdType Id[TRACE_SPAN_ID_SIZE_BYTES]);

WORKERSDK_API Trace_SpanIdType* Trace_SpanId_Null() ;

WORKERSDK_API void Trace_Query_Destroy(Trace_Query* Quer);

WORKERSDK_API uint32_t Trace_GetSerializedItemSize(const Trace_Item* Item);

WORKERSDK_API int Trace_SerializeItemToStream(Io_Stream* Stream,const Trace_Item* Item, uint32_t ItemSize);

WORKERSDK_API char* Trace_GetLastError();

WORKERSDK_API void Trace_EventTracer_ClearActiveSpanId(Trace_EventTracer *EventTracer);

WORKERSDK_API void Trace_EventTracer_SetActiveSpanId( Trace_EventTracer* InEventTracer,const Trace_SpanIdType* id );

WORKERSDK_API Trace_Query* Trace_ParseSimpleQuery(char* str);

WORKERSDK_API bool Trace_EventTracer_ShouldSampleSpan(Trace_EventTracer* EventTracer, const Trace_SpanIdType* Causes, int32_t NumCauses,Trace_Event* Event);

WORKERSDK_API Trace_EventData* Trace_EventData_Create();

WORKERSDK_API void Trace_EventData_Destroy(Trace_EventData *EventData) ;

WORKERSDK_API void Trace_EventTracer_AddSpan(Trace_EventTracer* EventTracer, const Trace_SpanIdType* Causes, int32_t NumCauses,Trace_Event* Event, const Trace_SpanIdType* id);

WORKERSDK_API bool Trace_EventTracer_PreFilterAcceptsEvent(Trace_EventTracer* EventTracer, Trace_Event* Event);

WORKERSDK_API Trace_EventTracer* Trace_EventTracer_Create(Trace_EventTracer_Parameters *Param);

WORKERSDK_API void Trace_EventTracer_AddEvent(Trace_EventTracer* EventTracer,Trace_Event* Event);

WORKERSDK_API void Trace_EventData_AddStringFields(Trace_EventData* EventData, int num, const char** Key, const char** Value);

WORKERSDK_API void Trace_EventTracer_Destroy(Trace_EventTracer* EventTracer);

WORKERSDK_API uint32_t Trace_GetNextSerializedItemSize(Io_Stream *Stream);

WORKERSDK_API Trace_Item* Trace_Item_GetThreadLocal();

WORKERSDK_API int8_t Trace_DeserializeItemFromStream(Io_Stream *Stream, Trace_Item* Item, uint32_t BytesToRead);
