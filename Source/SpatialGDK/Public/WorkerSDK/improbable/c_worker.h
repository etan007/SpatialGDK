/* Copyright (c) Improbable Worlds Ltd, All Rights Reserved */
#ifndef WORKER_SDK_C_INCLUDE_IMPROBABLE_C_WORKER_H
#define WORKER_SDK_C_INCLUDE_IMPROBABLE_C_WORKER_H
#include "WorkerSDK/improbable/pch.h"
#include <evntrace.h>
#include <stddef.h>
#include <stdint.h>
#include <string>
 
	/**
	 * API version information. You can get the version information that was defined when the library
	 * was compiled by calling "SPATIALGDK_APIVersion()" or "SPATIALGDK_APIVersionStr()".
	 */
	/* clang-format off */
	#define SPATIALGDK_API_VERSION_MAJOR 14
#define SPATIALGDK_API_VERSION_MINOR 6
#define SPATIALGDK_API_VERSION_PATCH 1
#define SPATIALGDK_API_VERSION ((SPATIALGDK_API_VERSION_MAJOR << 16) | (SPATIALGDK_API_VERSION_MINOR << 8) | SPATIALGDK_API_VERSION_PATCH)
#define SPATIALGDK_API_VERSION_STR "14.6.1"
	/* clang-format on */

 
 
 

	#ifndef WORKER_SDK_C_INCLUDE_IMPROBABLE_C_COMMON
#define WORKER_SDK_C_INCLUDE_IMPROBABLE_C_COMMON
	/* Types common to c_worker.h and c_schema.h. */
	typedef struct Schema_Bundle Schema_Bundle;
	typedef struct Schema_GenericData Schema_GenericData;
	typedef struct Schema_CommandRequest Schema_CommandRequest;
	typedef struct Schema_CommandResponse Schema_CommandResponse;
	typedef struct Schema_ComponentData Schema_ComponentData;
	typedef struct Schema_ComponentUpdate Schema_ComponentUpdate;
	typedef struct Schema_JsonParameters Schema_JsonParameters;
#endif /* WORKER_SDK_C_INCLUDE_IMPROBABLE_C_COMMON */

	typedef int64_t Worker_EntityId;
	typedef uint32_t Worker_ComponentId;
	typedef uint32_t Worker_CommandIndex;
	typedef int64_t Worker_RequestId;
    typedef uint32_t Worker_ComponentSetId;

	typedef struct Worker_Connection Worker_Connection;
	typedef struct Worker_ConnectionFuture Worker_ConnectionFuture;
	typedef struct Worker_Constraint Worker_Constraint;
	typedef struct Worker_DeploymentListFuture Worker_DeploymentListFuture;
	typedef struct Worker_PlayerIdentityTokenResponseFuture
		Worker_PlayerIdentityTokenResponseFuture;
	typedef struct Worker_LoginTokensResponseFuture Worker_LoginTokensResponseFuture;
	typedef struct Worker_Locator Worker_Locator;
	typedef struct Worker_OpList Worker_OpList;
	typedef struct Worker_SnapshotInputStream Worker_SnapshotInputStream;
	typedef struct Worker_SnapshotOutputStream Worker_SnapshotOutputStream;

	/**
	 * Defaults.
	 */
	/* clang-format off */
	/* General asynchronous IO. */
	#define WORKER_DEFAULTS_SEND_QUEUE_CAPACITY 4096
#define WORKER_DEFAULTS_RECEIVE_QUEUE_CAPACITY 4096
#define WORKER_DEFAULTS_LOG_MESSAGE_QUEUE_CAPACITY 256
#define WORKER_DEFAULTS_BUILT_IN_METRICS_REPORT_PERIOD_MILLIS 5000
	/* General networking. */
	#define WORKER_DEFAULTS_NETWORK_CONNECTION_TYPE WORKER_NETWORK_CONNECTION_TYPE_MODULAR_TCP
#define WORKER_DEFAULTS_NETWORK_SECURITY_TYPE WORKER_NETWORK_SECURITY_TYPE_TLS
#define WORKER_DEFAULTS_CONNECTION_TIMEOUT_MILLIS 60000
#define WORKER_DEFAULTS_DEFAULT_COMMAND_TIMEOUT_MILLIS 5000
#define WORKER_DEFAULTS_ERASURE_CODEC_ORIGINAL_PACKET_COUNT 10
#define WORKER_DEFAULTS_ERASURE_CODEC_RECOVERY_PACKET_COUNT 2
#define WORKER_DEFAULTS_ERASURE_CODEC_WINDOW_SIZE 16
#define WORKER_DEFAULTS_HEARTBEAT_INTERVAL_MILLIS 10000
#define WORKER_DEFAULTS_HEARTBEAT_TIMEOUT_MILLIS 60000
#define WORKER_DEFAULTS_FLOW_CONTROL_DOWNSTREAM_WINDOW_SIZE_BYTES 262144
#define WORKER_DEFAULTS_FLOW_CONTROL_UPSTREAM_WINDOW_SIZE_BYTES 262144
	/* TCP. */
	#define WORKER_DEFAULTS_MODULAR_TCP_MULTIPLEX_LEVEL 1
#define WORKER_DEFAULTS_TCP_MULTIPLEX_LEVEL 32
#define WORKER_DEFAULTS_TCP_SEND_BUFFER_SIZE 65536
#define WORKER_DEFAULTS_TCP_RECEIVE_BUFFER_SIZE 65536
#define WORKER_DEFAULTS_TCP_NO_DELAY false
#define WORKER_DEFAULTS_TCP_FLUSH_DELAY_MILLIS 1
	/* RakNet. */
	#define WORKER_DEFAULTS_RAKNET_HEARTBEAT_TIMEOUT_MILLIS 60000
	/* KCP. */
	#define WORKER_DEFAULTS_KCP_FAST_RETRANSMISSION 1
#define WORKER_DEFAULTS_KCP_EARLY_RETRANSMISSION 1
#define WORKER_DEFAULTS_KCP_NON_CONCESSIONAL_FLOW_CONTROL 1
#define WORKER_DEFAULTS_KCP_DISABLE_CONGESTION_CONTROL 1
#define WORKER_DEFAULTS_KCP_MULTIPLEX_LEVEL 1
#define WORKER_DEFAULTS_KCP_UPDATE_INTERVAL_MILLIS 10
#define WORKER_DEFAULTS_KCP_FLUSH_INTERVAL_MILLIS 1
#define WORKER_DEFAULTS_KCP_MIN_RTO_MILLIS 10
#define WORKER_DEFAULTS_KCP_SEND_WINDOW_SIZE 500
#define WORKER_DEFAULTS_KCP_RECV_WINDOW_SIZE 1000
#define WORKER_DEFAULTS_KCP_ENABLE_ERASURE_CODEC 0
#define WORKER_DEFAULTS_KCP_NETWORK_SECURITY_TYPE WORKER_NETWORK_SECURITY_TYPE_TLS
	/* Protocol logging. */
	#define WORKER_DEFAULTS_LOG_PREFIX "protocol-log-"
#define WORKER_DEFAULTS_MAX_LOG_FILES 10
#define WORKER_DEFAULTS_MAX_LOG_FILE_SIZE_BYTES  1024 * 1024
#define WORKER_DEFAULTS_ENABLE_DYNAMIC_COMPONENTS 0


	/* clang-format on */


 
	/**
	 * Enum defining the severities of log messages that can be sent to SpatialOS and received from the
	 * SDK.
	 */
	typedef enum Worker_LogLevel {
		WORKER_LOG_LEVEL_DEBUG = 1,
		WORKER_LOG_LEVEL_INFO = 2,
		WORKER_LOG_LEVEL_WARN = 3,
		WORKER_LOG_LEVEL_ERROR = 4,
		WORKER_LOG_LEVEL_FATAL = 5 /** deprecated */
	  } Worker_LogLevel;

	/**
	 * Enum defining the available categories for log messages. Each log message has one or more of
	 * these categories attached.
	 */
	typedef enum Worker_LogCategory {
		WORKER_LOG_CATEGORY_RECEIVE = 0x01,
		WORKER_LOG_CATEGORY_SEND = 0x02,
		WORKER_LOG_CATEGORY_NETWORK_STATUS = 0x04,
		WORKER_LOG_CATEGORY_NETWORK_TRAFFIC = 0x08,
		WORKER_LOG_CATEGORY_LOGIN = 0x10,
		WORKER_LOG_CATEGORY_API = 0x20,
		WORKER_LOG_CATEGORY_PARAMETERS = 0x40,

		WORKER_LOG_CATEGORY_ALL = 0x7F
	  } Worker_LogCategory;

	/** Enum defining the types of logsinks that can receive log messages from the the SDK. */
	typedef enum Worker_LogsinkType {
		/** Log messages are logged into a set of rotating files with a specific maximum size. */
		WORKER_LOGSINK_TYPE_ROTATING_FILE = 1,
		/** For each received log message a user callback is called.  */
		WORKER_LOGSINK_TYPE_CALLBACK = 2,
		/** Log messages are sent to stdout. */
		WORKER_LOGSINK_TYPE_STDOUT = 3,
		/** Log messages are sent to stdout and are ANSI color coded. */
		WORKER_LOGSINK_TYPE_STDOUT_ANSI = 4,
		/** Log messages are sent to stderr. */
		WORKER_LOGSINK_TYPE_STDERR = 5,
		/** Log messages are sent to stderr and are ANSI color coded. */
		WORKER_LOGSINK_TYPE_STDERR_ANSI = 6
	  } Worker_LogsinkType;

	/** Enum defining possible command status codes. */
	typedef enum Worker_StatusCode {
		/** The request was successfully executed and returned a response. */
		WORKER_STATUS_CODE_SUCCESS = 1,
		/**
		 * The request timed out before a response was received. It can be retried, but carefully - this
		 * usually means the deployment is overloaded, so some sort of backoff should be used to avoid
		 * making the problem worse. This can also be caused by the target worker's handling code failing
		 * to respond to the command at all, perhaps due to a bug in its implementation.
		 */
		WORKER_STATUS_CODE_TIMEOUT = 2,
		/**
		 * The target entity did not exist, or did not have the target component. This probably means the
		 * entity either hasn't been created yet or has already been deleted. It might make sense to retry
		 * the request if there is reason to believe the entity hasn't yet been created but will be soon.
		 */
		WORKER_STATUS_CODE_NOT_FOUND = 3,
		/**
		 * The request could not be executed by a worker, either because the worker lost authority over
		 * the entity while handling the request, the entity was deleted while handling the request, or no
		 * worker was authoritative over the entity at all. Assuming the deployment isn't irrecoverably
		 * broken (e.g. due to misconfigured loadbalancing or crash-looping workers) this is a transient
		 * failure and can be retried immediately.
		 */
		WORKER_STATUS_CODE_AUTHORITY_LOST = 4,
		/**
		 * The worker did not have the required permissions to make the request. Permissions do not change
		 * at runtime, so it doesn't make sense to retry the request.
		 */
		WORKER_STATUS_CODE_PERMISSION_DENIED = 5,
		/**
		 * The command was delivered successfully, but the handler rejected it. Either the command was
		 * delivered to a worker that explicitly rejected it by calling
		 * Worker_Connection_SendCommandFailure, or the request data was rejected as invalid by SpatialOS
		 * itself. In the latter case, in particular, Worker_Connection_SendCreateEntityRequest will
		 * return kApplicationError if an entity ID reservation has expired, and
		 * Worker_Connection_SendEntityQueryResult will return kApplicationError if the result set is
		 * incomplete.
		 */
		WORKER_STATUS_CODE_APPLICATION_ERROR = 6,
		/** Some other error occurred. This likely indicates a bug in SpatialOS and should be reported. */
		WORKER_STATUS_CODE_INTERNAL_ERROR = 7
	  } Worker_StatusCode;

	/** Possible status codes for a remote call, connection attempt, or connection migration attempt. */
	typedef enum Worker_ConnectionStatusCode {
		/** The remote call was successful, or we are successfully connected. */
		WORKER_CONNECTION_STATUS_CODE_SUCCESS = 1,
		/**
		 * Protocol violation, or some part of the system otherwise behaved in an unexpected way. Not
		 * expected to occur in normal operation.
		 */
		WORKER_CONNECTION_STATUS_CODE_INTERNAL_ERROR = 2,
		/**
		 * An argument provided by the caller was determined to be invalid. This is a local failure; no
		 * actual attempt was made to contact the host. Not retryable.
		 */
		WORKER_CONNECTION_STATUS_CODE_INVALID_ARGUMENT = 3,
		/** Failed due to a networking issue or otherwise unreachable host. */
		WORKER_CONNECTION_STATUS_CODE_NETWORK_ERROR = 4,
		/** A timeout provided by the caller or enforced by the system was exceeded. Can be retried. */
		WORKER_CONNECTION_STATUS_CODE_TIMEOUT = 5,
		/** Attempt was cancelled by the caller. Currently shouldn't happen; reserved for future use. */
		WORKER_CONNECTION_STATUS_CODE_CANCELLED = 6,
		/**
		 * Made contact with the host, but the request was explicitly rejected. Unlikely to be retryable.
		 * Possible causes include: the request was made to the wrong host; the host considered the
		 * request invalid for some other reason.
		 */
		WORKER_CONNECTION_STATUS_CODE_REJECTED = 7,
		/** The player identity token provided by the caller has expired. Generate a new one and retry. */
		WORKER_CONNECTION_STATUS_CODE_PLAYER_IDENTITY_TOKEN_EXPIRED = 8,
		/** The login token provided by the caller has expired. Generate a new one and retry. */
		WORKER_CONNECTION_STATUS_CODE_LOGIN_TOKEN_EXPIRED = 9,
		/**
		 * Failed because the deployment associated with the provided login token was at capacity.
		 * Retryable.
		 */
		WORKER_CONNECTION_STATUS_CODE_CAPACITY_EXCEEDED = 10,
		/**
		 * Failed due to rate-limiting of new connections to the deployment associated with the provided
		 * login token. Retryable.
		 */
		WORKER_CONNECTION_STATUS_CODE_RATE_EXCEEDED = 11,
		/**
		 * After a successful connection attempt, the server later explicitly terminated the connection.
		 * Possible causes include: the deployment was stopped; the worker was killed due to
		 * unresponsiveness.
		 */
		WORKER_CONNECTION_STATUS_CODE_SERVER_SHUTDOWN = 12
	  } Worker_ConnectionStatusCode;

	/**
	 * Enum defining possible result codes for API methods.
	 * WORKER_RESULT_FAILURE is consistent with invalid Worker_RequestId -1.
	 */
	typedef enum Worker_Result { WORKER_RESULT_FAILURE = -1, WORKER_RESULT_SUCCESS = 0 } Worker_Result;

	/** Enum defining the possible authority states for an entity component. */
	typedef enum Worker_Authority {
		WORKER_AUTHORITY_NOT_AUTHORITATIVE = 0,
		WORKER_AUTHORITY_AUTHORITATIVE = 1,
		WORKER_AUTHORITY_AUTHORITY_LOSS_IMMINENT = 2
	  } Worker_Authority;

	/** Enum defining the possible modes of loopback when updating a component. */
	typedef enum Worker_ComponentUpdateLoopback {
		/** The component update will not be returned in a subsequent call to Worker_GetOpList. */
		WORKER_COMPONENT_UPDATE_LOOPBACK_NONE = 0,
		/** The component update will also be returned in a subsequent call to Worker_GetOpList. */
		WORKER_COMPONENT_UPDATE_LOOPBACK_SHORT_CIRCUITED = 1
	  } Worker_ComponentUpdateLoopback;


    typedef struct Worker_NameVersionPair
    {
	    std::string name;
    	std::string version;
    	
    }Worker_NameVersionPair;

	/** Parameters for sending a log message to SpatialOS. */
	typedef struct Worker_LogMessage {
		/** The severity of the log message; defined in the Worker_LogLevel enumeration. */
		uint8_t level;
		/** The name of the logger. */
		const char* logger_name;
		/** The full log message. */
		const char* message;
		/** The ID of the entity this message relates to, or NULL for none. */
		const Worker_EntityId* entity_id;
	} Worker_LogMessage;

	/** Parameters for a gauge metric. */
	typedef struct Worker_GaugeMetric {
		/* The name of the metric. */
		const char* key;
		/* The current value of the metric. */
		double value;
	} Worker_GaugeMetric;

	/* Parameters for a histogram metric bucket. */
	typedef struct Worker_HistogramMetricBucket {
		/* The upper bound. */
		double upper_bound;
		/* The number of observations that were less than or equal to the upper bound. */
		uint32_t samples;
	} Worker_HistogramMetricBucket;

	/* Parameters for a histogram metric. */
	typedef struct Worker_HistogramMetric {
		/* The name of the metric. */
		const char* key;
		/* The sum of all observations. */
		double sum;
		/* The number of buckets. */
		uint32_t bucket_count;
		/* Array of buckets. */
		const Worker_HistogramMetricBucket* buckets;
	} Worker_HistogramMetric;

	/** Parameters for sending metrics to SpatialOS. */
	typedef struct Worker_Metrics {
		/** The load value of this worker. If NULL, do not report load. */
		const double* load;
		/** The number of gauge metrics. */
		uint32_t gauge_metric_count;
		/** Array of gauge metrics. */
		const Worker_GaugeMetric* gauge_metrics;
		/** The number of histogram metrics. */
		uint32_t histogram_metric_count;
		/** Array of histogram metrics. */
		const Worker_HistogramMetric* histogram_metrics;
	} Worker_Metrics;

	typedef void Worker_CommandRequestHandle;
	typedef void Worker_CommandResponseHandle;
	typedef void Worker_ComponentDataHandle;
	typedef void Worker_ComponentUpdateHandle;

	typedef void Worker_CommandRequestFree(Worker_ComponentId component_id,
										   Worker_CommandIndex command_index, void* user_data,
										   Worker_CommandRequestHandle* handle);
	typedef void Worker_CommandResponseFree(Worker_ComponentId component_id,
											Worker_CommandIndex command_index, void* user_data,
											Worker_CommandResponseHandle* handle);
	typedef void Worker_ComponentDataFree(Worker_ComponentId component_id, void* user_data,
										  Worker_ComponentDataHandle* handle);
	typedef void Worker_ComponentUpdateFree(Worker_ComponentId component_id, void* user_data,
											Worker_ComponentUpdateHandle* handle);

	typedef Worker_CommandRequestHandle* Worker_CommandRequestCopy(Worker_ComponentId component_id,
																   Worker_CommandIndex command_index,
																   void* user_data,
																   Worker_CommandRequestHandle* handle);
	typedef Worker_CommandResponseHandle*
	Worker_CommandResponseCopy(Worker_ComponentId component_id, Worker_CommandIndex command_index,
							   void* user_data, Worker_CommandResponseHandle* handle);
	typedef Worker_ComponentDataHandle* Worker_ComponentDataCopy(Worker_ComponentId component_id,
																 void* user_data,
																 Worker_ComponentDataHandle* handle);
	typedef Worker_ComponentUpdateHandle*
	Worker_ComponentUpdateCopy(Worker_ComponentId component_id, void* user_data,
							   Worker_ComponentUpdateHandle* handle);

	/* Ensure to return 1 to indicate success. If there was a failure when deserializing, you can
	 * instead return 0, and the SDK will treat this as a deserialization failure and log an error
	 * message. */

	typedef uint8_t Worker_CommandRequestDeserialize(Worker_ComponentId component_id,
													 Worker_CommandIndex command_index, void* user_data,
													 Schema_CommandRequest* source,
													 Worker_CommandRequestHandle** handle_out);
	typedef uint8_t Worker_CommandResponseDeserialize(Worker_ComponentId component_id,
													  Worker_CommandIndex command_index,
													  void* user_data, Schema_CommandResponse* source,
													  Worker_CommandResponseHandle** handle_out);
	typedef uint8_t Worker_ComponentDataDeserialize(Worker_ComponentId component_id, void* user_data,
													Schema_ComponentData* source,
													Worker_ComponentDataHandle** handle_out);
	typedef uint8_t Worker_ComponentUpdateDeserialize(Worker_ComponentId component_id, void* user_data,
													  Schema_ComponentUpdate* source,
													  Worker_ComponentUpdateHandle** handle_out);

	/* Note that if target_out is not assigned to a valid schema object, the SDK will treat this as a
	 * failure to serialize and will therefore shut down the connection. Ensure to assign `target_out`
	 * if you encounter a recoverable serialization failure. */

	typedef void Worker_CommandRequestSerialize(Worker_ComponentId component_id,
												Worker_CommandIndex command_index, void* user_data,
												Worker_CommandRequestHandle* handle,
												Schema_CommandRequest** target_out);
	typedef void Worker_CommandResponseSerialize(Worker_ComponentId component_id,
												 Worker_CommandIndex command_index, void* user_data,
												 Worker_CommandResponseHandle* handle,
												 Schema_CommandResponse** target_out);
	typedef void Worker_ComponentDataSerialize(Worker_ComponentId component_id, void* user_data,
											   Worker_ComponentDataHandle* handle,
											   Schema_ComponentData** target_out);
	typedef void Worker_ComponentUpdateSerialize(Worker_ComponentId component_id, void* user_data,
												 Worker_ComponentUpdateHandle* handle,
												 Schema_ComponentUpdate** target_out);

	typedef struct Worker_ComponentVtable {
		/**
		 * Component ID that this vtable is for. If this is the default vtable, this field is ignored.
		 */
		Worker_ComponentId component_id;
		/** User data which will be passed directly to the callbacks supplied below. */
		void* user_data;

		/**
		 * The function pointers below are only necessary in order to use the user_handle fields present
		 * in each of the Worker_CommandRequest, Worker_CommandResponse, Worker_ComponentData and
		 * Worker_ComponentUpdate types, for the given component ID (or for all components without an
		 * explicit vtable, if this is the default vtable), in order to offload serialization and
		 * deserialization work to internal SDK threads.
		 *
		 * For simplest usage of the SDK, all function pointers can be set to NULL, and only the
		 * schema_type field should be used in each type.
		 *
		 * In order to support usage of the user_handle field on instances of the corresponding type when
		 * used as input data to the SDK, X_serialize() must be provided.
		 *
		 * In order to support usage of the user_handle field on instances of the corresponding type when
		 * received as output data to the SDK, X_deserialize() must be provided.
		 *
		 * X_free() should free resources associated with the result of calling X_deserialize() or
		 * X_copy() (if provided).
		 *
		 * This decision can be made on a per-component, per-handle-type, and per-direction (input or
		 * output) basis. In the case of providing data to the SDK, the asynchronous serialization flow
		 * can be disabled even on a per-call basis by providing a non-NULL schema_type pointer instead of
		 * a user_handle pointer. The concrete types pointed to by the user_handle fields may differ
		 * between components or between handle types.
		 *
		 * All of the functions below, if provided, will be called from arbitrary internal SDK threads,
		 * and therefore must be thread-safe. A single user_handle pointer will not be passed to multiple
		 * callbacks concurrently, but a user_handle may be copied twice and the _results_ of those copies
		 * may be used concurrently.
		 *
		 * For a concrete example, consider calling Worker_Connection_SendComponentUpdate() with
		 * short-circuiting enabled. The SDK will call component_update_copy() twice on the provided
		 * user_handle. One copy will be used for the outgoing flow, and will be serialized with
		 * component_update_serialize() and subsequently freed with component_update_free(). Concurrently,
		 * the other copy will be passed back to the user as part of a Worker_OpList and freed with
		 * component_update_free() when the OpList is deallocated (or, if its lifetime is extended with
		 * Worker_AcquireComponentUpdate(), when the last reference is released by the user with
		 * Worker_ReleaseComponentUpdate()).
		 *
		 * In general, the two most obvious strategies are:
		 * 1) reference-counting. Have X_copy() (atomically) increase a reference count and return the
		 *    same pointer it was given, have X_free() (atomically) decrease the reference count and
		 *    deallocate if zero. X_deserialize() should allocate a new object with reference count of 1,
		 *    set the reference count of any new handle passed into the SDK to 1 initially and call
		 *    X_free() manually afterwards. In this case, data owned by the user_handle should never be
		 *    mutated after its first use. (This is the approach used internally for the schema_type.)
		 * 2) deep-copying. Have X_copy() allocate an entirely new deep copy of the object, and X_free()
		 *    deallocate directly. In this case, user_handles can be mutated freely. */

		Worker_CommandRequestFree* command_request_free;
		Worker_CommandRequestCopy* command_request_copy;
		Worker_CommandRequestDeserialize* command_request_deserialize;
		Worker_CommandRequestSerialize* command_request_serialize;

		Worker_CommandResponseFree* command_response_free;
		Worker_CommandResponseCopy* command_response_copy;
		Worker_CommandResponseDeserialize* command_response_deserialize;
		Worker_CommandResponseSerialize* command_response_serialize;

		Worker_ComponentDataFree* component_data_free;
		Worker_ComponentDataCopy* component_data_copy;
		Worker_ComponentDataDeserialize* component_data_deserialize;
		Worker_ComponentDataSerialize* component_data_serialize;

		Worker_ComponentUpdateFree* component_update_free;
		Worker_ComponentUpdateCopy* component_update_copy;
		Worker_ComponentUpdateDeserialize* component_update_deserialize;
		Worker_ComponentUpdateSerialize* component_update_serialize;
	} Worker_ComponentVtable;

	/* The four handle types below behave similarly. They support both direct use of schema data types,
	 * and alternatively conversion between schema types and custom user-defined handle types on worker
	 * threads.
	 *
	 * When passing an object into the API, either:
	 * - assign a new object created via the schema API (e.g. Schema_CreateComponentUpdate()) to the
	 *   schema_type field. In this case, the API takes ownership of the schema object.
	 * - leave the schema_type field NULL, and provide a custom pointer in the user_handle field. In
	 *   this case, the corresponding vtable for the component must supply copy, free and serialize
	 *   functions. The API will call X_copy() zero or more times, call X_serialize() if necessary to
	 *   convert to a new schema object, and call X_free() on each copy.
	 * In both cases, the user does not need to explicitly deallocate schema object (e.g. with
	 * Schema_DestroyComponentUpdate()).
	 *
	 * When the API passes an object to the user, either:
	 * - if no deserialize() function is provided in the corresponding vtable for the component, only
	 *   the schema_type field will be non-NULL. The API owns this object, and it will usually be
	 *   deallocated when the user-supplied callback returns. To extend the lifetime of the data, call
	 *   the relevant Worker_AcquireX() function (e.g. Worker_AcquireComponentUpdate()) and use the
	 *   resulting pointer. This must then be explicitly deallocated by calling the corresponding
	 *   Worker_ReleaseX() function (e.g. Worker_ReleaseComponentUpdate()) to avoid memory leaks.
	 * - if an X_deserialize() function is provided, in which case a X_free() function should also be
	 *   provided, both the schema_type and user_handle fields will be non-NULL, the latter filled with
	 *   the result of calling the X_deserialize() function. Again, the API owns these objects and will
	 *   usually deallocate them. The relevant Worker_AcquireX() function works as before, and will
	 *   extend the lifetime of both the schema_type and the user_handle (by calling the user-provided
	 *   X_copy() function in the latter case). If only the user_handle needs to be preserved, this is
	 *   possible by manually calling the user-provided copy() and free() functions (or otherwise, since
	 *   the semantics of the user_handles is up to the user).
	 *
	 * Note that objects pointed-to by the schema_type fields must _not_ be mutated by the user when
	 * owned by the SDK (either because they have been passed as input data to the SDK, or because they
	 * were passed out of the SDK to user code), as the SDK may be using them internal concurrently.
	 *
	 * Similarly, the user must ensure any use of a SDK-owned user_handle is safe with respect to the
	 * SDK passing other copies of the handle to the vtable concurrently.
	 *
	 * Since the schema_type is deallocated when the last copy of a user_handle is freed, it is
	 * generally safe for a user_handle produced by X_deserialize() to depend on data owned by the
	 * schema_type, and for a schema_type produced by X_serialize() to depend on data owned by the
	 * user_handle. */

	/**
	 * An object used to represent a command request by either raw schema data or some user-defined
	 * handle type.
	 */
	typedef struct Worker_CommandRequest {
		void* reserved;
		Worker_ComponentId component_id;
		Worker_CommandIndex command_index;
		Schema_CommandRequest* schema_type;
		Worker_CommandRequestHandle* user_handle;
	} Worker_CommandRequest;

	/**
	 * An object used to represent a command response by either raw schema data or some user-defined
	 * handle type.
	 */
	typedef struct Worker_CommandResponse {
		void* reserved;
		Worker_ComponentId component_id;
		Worker_CommandIndex command_index;
		Schema_CommandResponse* schema_type;
		Worker_CommandResponseHandle* user_handle;
	} Worker_CommandResponse;

	/**
	 * An object used to represent a component data snapshot by either raw schema data or some
	 * user-defined handle type.
	 */
	typedef struct Worker_ComponentData {
		void* reserved;
		Worker_ComponentId component_id;
		Schema_ComponentData* schema_type;
		Worker_ComponentDataHandle* user_handle;
	} Worker_ComponentData;

	/**
	 * An object used to represent a component update by either raw schema data or some user-defined
	 * handle type.
	 */
	typedef struct Worker_ComponentUpdate {
		void* reserved;
		Worker_ComponentId component_id;
		Schema_ComponentUpdate* schema_type;
		Worker_ComponentUpdateHandle* user_handle;
	} Worker_ComponentUpdate;

   
	/**
	 * Acquire a reference to extend the lifetime of a command request managed by the SDK, by returning
	 * a new command request container object _not_ managed by the SDK. The data contained within the
	 * object will be identical to the original data, but it is not safe to mutate the contained data
	 * without explicitly copying it first. The lifetime of the original container object is unchanged.
	 */
	WORKERSDK_API Worker_CommandRequest*
	Worker_AcquireCommandRequest(const Worker_CommandRequest* request);
	/**
	 * Acquire a reference to extend the lifetime of a command response managed by the SDK, by
	 * returning a new command response container object _not_ managed by the SDK. The data contained
	 * within the object will be identical to the original data, but it is not safe to mutate the
	 * contained data without explicitly copying it first. The lifetime of the original container object
	 * is unchanged.
	 */
	WORKERSDK_API Worker_CommandResponse*
	Worker_AcquireCommandResponse(const Worker_CommandResponse* response);
	/**
	 * Acquire a reference to extend the lifetime of some component data managed by the SDK, by
	 * returning a new component data container object _not_ managed by the SDK. The data contained
	 * within the object will be identical to the original data, but it is not safe to mutate the
	 * contained data without explicitly copying it first. The lifetime of the original container object
	 * is unchanged.
	 */
	WORKERSDK_API Worker_ComponentData* Worker_AcquireComponentData(const Worker_ComponentData* data);
	/**
	 * Acquire a reference to extend the lifetime of a component update managed by the SDK, by
	 * returning a new component update container object _not_ managed by the SDK. The data contained
	 * within the object will be identical to the original data, but it is not safe to mutate the
	 * contained data without explicitly copying it first. The lifetime of the original container object
	 * is unchanged.
	 */
	WORKERSDK_API Worker_ComponentUpdate*
	Worker_AcquireComponentUpdate(const Worker_ComponentUpdate* update);
	/** Release a reference obtained by Worker_AcquireCommandRequest. */
	WORKERSDK_API void Worker_ReleaseCommandRequest(Worker_CommandRequest* request);
	/** Release a reference obtained by Worker_AcquireCommandResponse. */
	WORKERSDK_API void Worker_ReleaseCommandResponse(Worker_CommandResponse* response);
	/** Release a reference obtained by Worker_AcquireComponentData. */
	WORKERSDK_API void Worker_ReleaseComponentData(Worker_ComponentData* data);
	/** Release a reference obtained by Worker_AcquireComponentUpdate. */
	WORKERSDK_API void Worker_ReleaseComponentUpdate(Worker_ComponentUpdate* update);

	/** Represents an entity with an ID and a component data snapshot. */
	typedef struct Worker_Entity {
		/** The ID of the entity. */
		Worker_EntityId entity_id;
		/** Number of components for the entity. */
		uint32_t component_count;
		/** Array of initial component data for the entity. */
		const Worker_ComponentData* components;
	} Worker_Entity;

	typedef enum Worker_ConstraintType {
		WORKER_CONSTRAINT_TYPE_ENTITY_ID = 1,
		WORKER_CONSTRAINT_TYPE_COMPONENT = 2,
		WORKER_CONSTRAINT_TYPE_SPHERE = 3,
		WORKER_CONSTRAINT_TYPE_AND = 4,
		WORKER_CONSTRAINT_TYPE_OR = 5,
		WORKER_CONSTRAINT_TYPE_NOT = 6
	  } Worker_ConstraintType;

	typedef struct Worker_EntityIdConstraint {
		Worker_EntityId entity_id;
	} Worker_EntityIdConstraint;

	typedef struct Worker_ComponentConstraint {
		Worker_ComponentId component_id;
	} Worker_ComponentConstraint;

	typedef struct Worker_SphereConstraint {
		double x;
		double y;
		double z;
		double radius;
	} Worker_SphereConstraint;

	typedef struct Worker_AndConstraint {
		uint32_t constraint_count;
		Worker_Constraint* constraints;
	} Worker_AndConstraint;

	typedef struct Worker_OrConstraint {
		uint32_t constraint_count;
		Worker_Constraint* constraints;
	} Worker_OrConstraint;

	typedef struct Worker_NotConstraint {
		Worker_Constraint* constraint;
	} Worker_NotConstraint;

	typedef union Worker_Constraint_Union {
		Worker_EntityIdConstraint entity_id_constraint;
		Worker_ComponentConstraint component_constraint;
		Worker_SphereConstraint sphere_constraint;
		Worker_AndConstraint and_constraint;
		Worker_OrConstraint or_constraint;
		Worker_NotConstraint not_constraint;
	} Worker_Constraint_Union;

	/** A single query constraint. */
	struct Worker_Constraint {
		/** The type of constraint, defined using Worker_ConstraintType. */
		uint8_t constraint_type;
		/** Union with fields corresponding to each constraint type. */
		Worker_Constraint_Union constraint;
	};

	typedef enum Worker_ResultType {
		WORKER_RESULT_TYPE_COUNT = 1,
		WORKER_RESULT_TYPE_SNAPSHOT = 2
	  } Worker_ResultType;

	/** An entity query. */
	typedef struct Worker_EntityQuery {
		/** The constraint for this query. */
		Worker_Constraint constraint;
		/** Number of component IDs in the array for a snapshot result type. */
		uint32_t snapshot_result_type_component_id_count;
		/** Pointer to component ID data for a snapshot result type. NULL means all component IDs. */
		const Worker_ComponentId* snapshot_result_type_component_ids;
		
		uint32_t snapshot_result_type_component_set_id_count;
		
		const Worker_ComponentSetId* snapshot_result_type_component_set_ids;

		/** Result type for this query, using Worker_ResultType. */
		//uint8_t result_type;
	} Worker_EntityQuery;

	/** (Deprecated) An interest override for a particular (entity ID, component ID) pair. */
	typedef struct Worker_InterestOverride {
		/** The ID of the component for which interest is being overridden. */
		uint32_t component_id;
		/** Whether the worker is interested in this component. */
		uint8_t is_interested;
	} Worker_InterestOverride;

	/** Worker attributes that are part of a worker's runtime configuration. */
	typedef struct Worker_WorkerAttributes {
		/** Number of worker attributes. */
		uint32_t attribute_count;
		/** Will be NULL if there are no attributes associated with the worker. */
		const char** attributes;
	} Worker_WorkerAttributes;

	/* The ops are placed in the same order everywhere. This order should be used everywhere there is
	   some similar code for each op (including wrapper APIs, definitions, function calls, thunks, and
	   so on).
	
	   (SECTION 1) GLOBAL ops, which do not depend on any entity. */

	/** Data for a disconnect message from the SDK. */
	typedef struct Worker_DisconnectOp {
		/** A value from the Worker_ConnectionStatusCode enumeration. */
		uint8_t connection_status_code;
		/** A string giving detailed information on the reason for disconnecting. */
		const char* reason;
	} Worker_DisconnectOp;

	/** Data for a FlagUpdate operation. */
	typedef struct Worker_FlagUpdateOp {
		/** The name of the updated worker flag. */
		const char* name;
		/**
		 * The new value of the updated worker flag.
		 * A null value indicates that the flag has been deleted.
		 */
		const char* value;
	} Worker_FlagUpdateOp;

	/**
	 * Data for a log message from the SDK.
	 * Note: Worker_LogMessageOp has been deprecated and will be removed in a future version of
	 * SpatialOS.
	 */
	typedef struct Worker_LogMessageOp {
		/** The severity of the log message; defined in the Worker_LogLevel enumeration. */
		uint8_t level;
		/** The message. */
		const char* message;
	} Worker_LogMessageOp;

	/** Data for a set of built-in metrics reported by the SDK. */
	typedef struct Worker_MetricsOp {
		Worker_Metrics metrics;
	} Worker_MetricsOp;

	/** Data for a critical section boundary (enter or leave) operation. */
	typedef struct Worker_CriticalSectionOp {
		/** Whether the protocol is entering a critical section (true) or leaving it (false). */
		uint8_t in_critical_section;
	} Worker_CriticalSectionOp;

	/* (SECTION 2) ENTITY-SPECIFIC ops, which do not depend on any component. */

	/** Data for an AddEntity operation. */
	typedef struct Worker_AddEntityOp {
		/** The ID of the entity that was added to the worker's view of the simulation. */
		Worker_EntityId entity_id;
	} Worker_AddEntityOp;

	/** Data for a RemoveEntity operation. */
	typedef struct Worker_RemoveEntityOp {
		/** The ID of the entity that was removed from the worker's view of the simulation. */
		Worker_EntityId entity_id;
	} Worker_RemoveEntityOp;

	/** Data for a ReserveEntityIdsResponse operation. */
	typedef struct Worker_ReserveEntityIdsResponseOp {
		/** The ID of the reserve entity ID request for which there was a response. */
		Worker_RequestId request_id;
		/** Status code of the response, using Worker_StatusCode. */
		uint8_t status_code;
		/** The error message. */
		const char* message;
		/**
		 * If successful, an ID which is the first in a contiguous range of newly allocated entity
		 * IDs which are guaranteed to be unused in the current deployment.
		 */
		Worker_EntityId first_entity_id;
		/** If successful, the number of IDs reserved in the contiguous range, otherwise 0. */
		uint32_t number_of_entity_ids;
	} Worker_ReserveEntityIdsResponseOp;

	/** Data for a CreateEntity operation. */
	typedef struct Worker_CreateEntityResponseOp {
		/** The ID of the request for which there was a response. */
		Worker_RequestId request_id;
		/** Status code of the response, using Worker_StatusCode. */
		uint8_t status_code;
		/** The error message. */
		const char* message;
		/** If successful, the entity ID of the newly created entity. */
		Worker_EntityId entity_id;
	} Worker_CreateEntityResponseOp;

	/** Data for a DeleteEntity operation. */
	typedef struct Worker_DeleteEntityResponseOp {
		/** The ID of the delete entity request for which there was a command response. */
		Worker_RequestId request_id;
		/** The ID of the target entity of this request. */
		Worker_EntityId entity_id;
		/** Status code of the response, using Worker_StatusCode. */
		uint8_t status_code;
		/** The error message. */
		const char* message;
	} Worker_DeleteEntityResponseOp;

	/** A response indicating the result of an entity query request. */
	typedef struct Worker_EntityQueryResponseOp {
		/** The ID of the entity query request for which there was a response. */
		Worker_RequestId request_id;
		/** Status code of the response, using Worker_StatusCode. */
		uint8_t status_code;
		/** The error message. */
		const char* message;
		/**
		 * Number of entities in the result set. Reused to indicate the result itself for CountResultType
		 * queries.
		 */
		uint32_t result_count;
		/**
		 * Array of entities in the result set. Will be NULL if the query was a count query. Snapshot data
		 * in the result is deserialized with the corresponding vtable deserialize function and freed with
		 * the vtable free function when the OpList is destroyed.
		 */
		const Worker_Entity* results;
	} Worker_EntityQueryResponseOp;

	/* (SECTION 3) COMPONENT-SPECIFIC ops. */

	/** Data for an AddComponent operation. */
	typedef struct Worker_AddComponentOp {
		/** The ID of the entity for which a component was added. */
		Worker_EntityId entity_id;
		/**
		 * The initial data for the new component. Deserialized with the corresponding vtable deserialize
		 * function and freed with the vtable free function when the OpList is destroyed.
		 */
		Worker_ComponentData data;
	} Worker_AddComponentOp;

	/** Data for a RemoveComponent operation. */
	typedef struct Worker_RemoveComponentOp {
		/** The ID of the entity for which a component was removed. */
		Worker_EntityId entity_id;
		/** The ID of the component that was removed. */
		Worker_ComponentId component_id;
	} Worker_RemoveComponentOp;

	/** Data for an AuthorityChange operation. */
	typedef struct Worker_AuthorityChangeOp {
		/** The ID of the entity for which there was an authority change. */
		Worker_EntityId entity_id;
		/** The ID of the component over which the worker's authority has changed. */
		Worker_ComponentId component_id;
		/** The authority state of the component, using the Worker_Authority enumeration. */
		uint8_t authority;
	} Worker_AuthorityChangeOp;

	/** Data for a ComponentUpdate operation. */
	typedef struct Worker_ComponentUpdateOp {
		/** The ID of the entity for which there was a component update. */
		Worker_EntityId entity_id;
		/**
		 * The new component data for the updated entity. Deserialized with the corresponding vtable
		 * deserialize function and freed with the vtable free function when the OpList is destroyed.
		 */
		Worker_ComponentUpdate update;
	} Worker_ComponentUpdateOp;

     
    typedef struct Worker_ComponentSetAuthorityChangeOp
    {
    	Worker_EntityId entity_id;
    	Worker_ComponentSetId component_set_id;
    	Worker_Authority authority;
    	uint32_t canonical_component_set_data_count;
    	Worker_ComponentData* canonical_component_set_data;
    } Worker_ComponentSetAuthorityChangeOp;
	/** Data for a CommandRequest operation. */
	typedef struct Worker_CommandRequestOp {
		/** The incoming command request ID. */
		Worker_RequestId request_id;
		/** The ID of the entity for which there was a command request. */
		Worker_EntityId entity_id;
		/** Upper bound on request timeout provided by the platform. */
		uint32_t timeout_millis;
		/** The ID of the worker that sent the request. */
		const char* caller_worker_id;
		Worker_EntityId caller_worker_entity_id;
		/** The attributes of the worker that sent the request. */
		Worker_WorkerAttributes caller_attribute_set;
		/**
		 * The command request data. Deserialized with the corresponding vtable deserialize function and
		 * freed with the vtable free function when the OpList is destroyed.
		 */
		Worker_CommandRequest request;
	} Worker_CommandRequestOp;

	/** Data for a CommandResponse operation. */
	typedef struct Worker_CommandResponseOp {
		/** The ID of the command request for which there was a command response. */
		Worker_RequestId request_id;
		/** The ID of the entity originally targeted by the command request. */
		Worker_EntityId entity_id;
		/** Status code of the response, using Worker_StatusCode. */
		uint8_t status_code;
		/** The error message. */
		const char* message;
		/**
		 * The command response data. Deserialized with the corresponding vtable deserialize function and
		 * freed with the vtable free function when the OpList is destroyed.
		 */
		Worker_CommandResponse response;
	} Worker_CommandResponseOp;

    typedef struct component_set_authority_changeOp
    {
    	Worker_EntityId entity_id;
    	Worker_ComponentId component_set_id;
    	Worker_Authority authority;
    }component_set_authority_changeOp;
    

	/** Enum defining different possible op types. */
	typedef enum Worker_OpType {
		WORKER_OP_TYPE_DISCONNECT = 1,
		WORKER_OP_TYPE_FLAG_UPDATE = 2,
		WORKER_OP_TYPE_LOG_MESSAGE = 3,
		WORKER_OP_TYPE_METRICS = 4,
		WORKER_OP_TYPE_CRITICAL_SECTION = 5,
		WORKER_OP_TYPE_ADD_ENTITY = 6,
		WORKER_OP_TYPE_REMOVE_ENTITY = 7,
		WORKER_OP_TYPE_RESERVE_ENTITY_IDS_RESPONSE = 8,
		WORKER_OP_TYPE_CREATE_ENTITY_RESPONSE = 9,
		WORKER_OP_TYPE_DELETE_ENTITY_RESPONSE = 10,
		WORKER_OP_TYPE_ENTITY_QUERY_RESPONSE = 11,
		WORKER_OP_TYPE_ADD_COMPONENT = 12,
		WORKER_OP_TYPE_REMOVE_COMPONENT = 13,
		WORKER_OP_TYPE_AUTHORITY_CHANGE = 14,
		WORKER_OP_TYPE_COMPONENT_UPDATE = 15,
		WORKER_OP_TYPE_COMMAND_REQUEST = 16,
		WORKER_OP_TYPE_COMMAND_RESPONSE = 17,
		WORKER_OP_TYPE_COMPONENT_SET_AUTHORITY_CHANGE = 18,
		
	  } Worker_OpType;

	typedef union Worker_Op_Union {
		Worker_DisconnectOp disconnect;
		Worker_FlagUpdateOp flag_update;
		Worker_LogMessageOp log_message;
		Worker_MetricsOp metrics;
		Worker_CriticalSectionOp critical_section;
		Worker_AddEntityOp add_entity;
		Worker_RemoveEntityOp remove_entity;
		Worker_ReserveEntityIdsResponseOp reserve_entity_ids_response;
		Worker_CreateEntityResponseOp create_entity_response;
		Worker_DeleteEntityResponseOp delete_entity_response;
		Worker_EntityQueryResponseOp entity_query_response;
		Worker_AddComponentOp add_component;
		Worker_RemoveComponentOp remove_component;
		Worker_AuthorityChangeOp authority_change;
		Worker_ComponentUpdateOp component_update;
		Worker_CommandRequestOp command_request;
		Worker_CommandResponseOp command_response;
		Worker_ComponentSetAuthorityChangeOp component_set_authority_change;
	} Worker_Op_Union;
     
	/** Data for a single op contained within an op list. */
	typedef struct Worker_Op {
		/** The type of this op, defined in Worker_OpType. */
		uint8_t op_type;
		Worker_Op_Union op;
		const uint8_t* span_id;
	} Worker_Op;

	/** An op list, usually returned by Worker_Connection_GetOpList. */
	struct Worker_OpList {
		Worker_Op* ops;
		uint32_t op_count;
	};

	/** Enum defining the possible network security types. */
	typedef enum Worker_NetworkSecurityType {
		/** No encryption or security. Only safe for use in trusted environments. */
		WORKER_NETWORK_SECURITY_TYPE_INSECURE = 0,
		/** Uses DTLS or TLS as approriate for UDP-based and TCP-based connections respectively. */
		WORKER_NETWORK_SECURITY_TYPE_TLS = 1,
		/** An alias for WORKER_NETWORK_SECURITY_TYPE_TLS. */
		WORKER_NETWORK_SECURITY_TYPE_DTLS = WORKER_NETWORK_SECURITY_TYPE_TLS
	  } Worker_NetworkSecurityType;

	/**
	 * Parameters for configuring a RakNet connection. Used by Worker_NetworkParameters.
	 * DEPRECATED: The RakNet stack has been deprecated and will be removed in a future version of
	 * SpatialOS.
	 */
	typedef struct Worker_RakNetNetworkParameters {
		/** Time (in milliseconds) that RakNet should use for its heartbeat protocol. */
		uint32_t heartbeat_timeout_millis;
	} Worker_RakNetNetworkParameters;

    typedef struct Worker_Streamtcp
    {
    	uint32_t flush_delay_millis;
    }Worker_Streamtcp;

    typedef struct Worker_StreamKcp
    {
    	uint32_t flush_interval_millis;
    }Worker_StreamKcp;

	/**
	 * Parameters to configure erasure coding, a forward error correction technique which
	 * increases bandwidth usage but may improve latency on unreliable networks.
	 */
	typedef struct Worker_ErasureCodecParameters {
		/** Number of consecutive packets to send before sending redundant recovery packets. */
		uint8_t original_packet_count;
		/**
		 * Number of redundant recovery packets to send for each group of consecutive original
		 * packets. These packets are used to recover up to the same number of lost original packets.
		 */
		uint8_t recovery_packet_count;
		/**
		 * Number of batches that can be stored in memory, where a batch contains packets belonging to
		 * the same group of consecutive original packets and the corresponding recovery packets. Each
		 * batch contains up to OriginalPacketCount plus RecoveryPacketCount packets.
		 */
		uint8_t window_size;
	} Worker_ErasureCodecParameters;

	/**
	 * Parameters to configure flow control. Used by Worker_ModularKcpNetworkParameters and
	 * Worker_ModularTcpNetworkParameters.
	 */
	typedef struct Worker_FlowControlParameters {
		/**
		 * The maximum number of bytes of serialized messages sent by SpatialOS which can be held in
		 * memory on the worker at any one time.
		 */
		uint32_t downstream_window_size_bytes;
		/**
		 * The maximum number of bytes of serialized messages sent by the worker which can be held in
		 * memory on the bridge at any one time.
		 */
		uint32_t upstream_window_size_bytes;
	} Worker_FlowControlParameters;

	/**
	 * Parameters to configure internal heartbeating which can detect unresponsive peers. If an
	 * unresponsive peer is detected, a Worker_DisconnectOp will be enqueued in the op list.
	 */
	typedef struct Worker_HeartbeatParameters {
		/**
		 * Minimum interval, in milliseconds, between which heartbeat messages are sent to the
		 * peer. A new heartbeat won't be sent before a response for the original heartbeat is received.
		 */
		uint64_t interval_millis;
		/** Time, in milliseconds, after which the peer will be deemed unresponsive. */
		uint64_t timeout_millis;
	} Worker_HeartbeatParameters;


/**
 * Parameters for configuring compression. Used by Worker_ModularKcpNetworkParameters and
 * Worker_ModularTcpNetworkParameters. Currently, there are no configurable settings; if this struct
 * is not NULL compression is enabled.
 */
typedef struct Worker_CompressionParameters {
	/** A placeholder field. This is ignored. */
	char place_holder;
} Worker_CompressionParameters;

/* Parameters for configuring a TCP connection. Used by Worker_NetworkParameters.
 * DEPRECATED: This TCP stack has been deprecated and will be removed in a future version of
 * SpatialOS. To use TCP, use Worker_ModularTcpNetworkParameters instead.
 */
typedef struct Worker_TcpNetworkParameters {
	/**
	 * The number of multiplexed TCP connections to use. Updates for entities are sharded across
	 * connections: the higher the multiplex level, the fewer entities might be impacted by a delayed
	 * update. Messages across connections cannot be packed into the same TCP packet, which may result
	 * in higher bandwidth usage. Increasing the number of multiplexed streams may also increase CPU
	 * usage.
	 */
	uint8_t multiplex_level;
	/** Size in bytes of the TCP send buffer. */
	uint32_t send_buffer_size;
	/** Size in bytes of the TCP receive buffer. */
	uint32_t receive_buffer_size;
	/** Whether to enable TCP_NODELAY. */
	uint8_t no_delay;
	/** Type of encryption layer security to use, defined in Worker_NetworkSecurityType. */
	uint8_t security_type;
	
	Worker_Streamtcp downstream_tcp;
	Worker_Streamtcp upstream_tcp;

	Worker_HeartbeatParameters* downstream_heartbeat;
	Worker_HeartbeatParameters* upstream_heartbeat;

	Worker_FlowControlParameters*  flow_control;
	
} Worker_TcpNetworkParameters;
	/**
	 * Parameters for configuring a KCP connection. Used by Worker_NetworkParameters.
	 * DEPRECATED: This KCP stack has been deprecated and will be removed in a future version of
	 * SpatialOS. To use KCP, use Worker_ModularKcpNetworkParameters instead.
	 */
	typedef struct Worker_KcpNetworkParameters {
		/**
		 * Whether to enable fast retransmission, which causes retransmission delays to increase more
		 * slowly when retransmitting timed-out packets multiple times.
		 */
		uint8_t fast_retransmission;
		/**
		 * Whether to enable early retransmission, which causes optimistic retransmission of earlier
		 * packets when acknowledgements are received for packets which were sent later, rather than
		 * waiting until the retransmission timeout has expired.
		 */
		uint8_t early_retransmission;
		/**
		 * Whether to enable non-concessional flow control, which disables the usage of
		 * congestion windows (which are used to reduce packet loss across congested networks).
		 * Enabling non-concessional flow control can help optimize for low-latency delivery of
		 * small messages.
		 */
		uint8_t non_concessional_flow_control;
		/**
		 * Number of multiplexed KCP streams. Updates for entities are sharded across streams: the higher
		 * the multiplex level, the fewer entities might be impacted by a delayed update. Messages across
		 * streams cannot be packed into the same UDP packet which may result in higher bandwidth usage.
		 * Increasing the number of multiplexed streams may also increase CPU usage.
		 */
		uint32_t multiplex_level;
		/**
		 * Interval, in milliseconds, between which the KCP transport layer sends and receives
		 * packets waiting in its send and receive buffers respectively.
		 */
		uint32_t update_interval_millis;
		/**
		 * Hard limit on the minimum retransmission timeout. A packet will be resent if an
		 * acknowledgment has not been received from the peer within a time period known as the
		 * retransmission timeout. The retransmission timeout is calculated based on estimated round
		 * trip times to the remote peer, but it will never be set to a value lower than the minimum
		 * retransmission timeout. If you set this parameter to a value which is much higher than the
		 * average round trip time to a peer, it will likely result in packets not being resent
		 * as early as they could be, increasing latency for retransmitted packets. However, if you set
		 * this parameter to a value which is lower than the average round trip time (or ping), packets
		 * will be retransmitted even if they are not lost, which will cause unnecessary bandwidth
		 * overhead until round trip times are calculated. For more information on retransmission
		 * timeouts and their calculation, see https://tools.ietf.org/html/rfc6298. Note,
		 * however, that the RFC pertains to TCP, and therefore it focuses on avoiding unnecessary
		 * retransmissions rather than optimizing for latency.
		 * Set to zero to use default, which is lower when
		 * Worker_KcpNetworkParameters::fast_retransmission is enabled.
		 */
		uint32_t min_rto_millis;
		/**
		 * KCP flow control window size for sending, in number of KCP packets. This window is applied
		 * to sending across all streams i.e. sending a message will block if it would cause the total
		 * number of un-acked outgoing packets to exceed the send window size.
		 */
		uint32_t send_window_size;
		/**
		 * KCP flow control window for receiving, in number of KCP packets. The upper bound on the
		 * memory used by receive buffers is proportional to the multiplex level multiplied by the
		 * receive window size.
		 */
		uint32_t recv_window_size;
		/** Whether to enable the erasure codec. */
		uint8_t enable_erasure_codec;
		/** Erasure codec parameters. */
		Worker_ErasureCodecParameters erasure_codec;
		/** Heartbeat parameters. */
		Worker_HeartbeatParameters heartbeat;
		/** Type of encryption layer security to use, defined in Worker_NetworkSecurityType. */
		uint8_t security_type;
		
		Worker_StreamKcp upstream_kcp;
		Worker_StreamKcp downstream_kcp;
		
		Worker_HeartbeatParameters* upstream_heartbeat;
		Worker_HeartbeatParameters* downstream_heartbeat;
		
		Worker_CompressionParameters* upstream_compression;
		Worker_CompressionParameters* downstream_compression;

		Worker_FlowControlParameters*  flow_control;
		
	} Worker_KcpNetworkParameters;

	/**
	 * Parameters for configuring the KCP transport layer within the modular KCP network stack. Used by
	 * Worker_ModularKcpNetworkParameters.
	 */
	typedef struct Worker_KcpTransportParameters {
		/**
		 * The maximum interval, in milliseconds, between which the KCP transport layer flushes packets
		 * waiting in its cross-stream send buffer to the network. The transport layer may send earlier
		 * if there are enough packets to fill the MTU or if there has been a call to flush the network.
		 */
		uint32_t flush_interval_millis;
		/**
		 * Whether to enable fast retransmission, which causes retransmission delays to increase more
		 * slowly when retransmitting timed-out packets multiple times.
		 */
		uint8_t fast_retransmission;
		/**
		 * Whether to enable early retransmission, which causes optimistic retransmission of earlier
		 * packets when acknowledgements are received for packets which were sent later, rather than
		 * waiting until the retransmission timeout has expired.
		 */
		uint8_t early_retransmission;
		/**
		 * Whether to disable congestion control which disables the usage of congestion windows (which
		 * are used to reduce packet loss across congested networks). Disabling congestion control can
		 * optimize for low-latency delivery of small messages.
		 */
		uint8_t disable_congestion_control;
		/**
		 * Hard limit on the minimum retransmission timeout. A packet will be resent if an
		 * acknowledgment has not been received from the peer within a time period known as the
		 * retransmission timeout. The retransmission timeout is calculated based on estimated round
		 * trip times to the remote peer, but it will never be set to a value lower than the minimum
		 * retransmission timeout. If you set this parameter to a value which is much higher than the
		 * average round trip time to a peer, it will likely result in packets not being resent
		 * as early as they could be, increasing latency for retransmitted packets. However, if you set
		 * this parameter to a value which is lower than the average round trip time (or ping), packets
		 * will be retransmitted even if they are not lost, which will cause unnecessary bandwidth
		 * overhead until round trip times are calculated. For more information on retransmission
		 * timeouts and their calculation, see https://tools.ietf.org/html/rfc6298. Note,
		 * however, that the RFC pertains to TCP, and therefore it focuses on avoiding unnecessary
		 * retransmissions rather than optimizing for latency.
		 * Set to zero to use default, which is lower when
		 * Worker_KcpTransportParameters::fast_retransmission is enabled.
		 */
		uint32_t min_rto_millis;
	} Worker_KcpTransportParameters;

	/**
	 * Parameters for configuring the TCP transport layer within the modular TCP network stack. Used by
	 * Worker_ModularTcpNetworkParameters.
	 */
	typedef struct Worker_TcpTransportParameters {
		/**
		 * Maximum delay after which to flush data to the network. If non-zero, data may be delayed for up
		 * to this many milliseconds in order to pack more data into network packets and save
		 * bandwidth. If set to zero, data will always be sent immediately without delay. This will
		 * prevent manual flushes from having any effect, and may render compression ineffective.
		 */
		uint32_t flush_delay_millis;
	} Worker_TcpTransportParameters;



	/**
	 * Parameters for configuring the stack for a modular KCP connection. Used by
	 * Worker_NetworkParameters.
	 */
	typedef struct Worker_ModularKcpNetworkParameters {
		/** Type of encryption layer security to use, defined in Worker_NetworkSecurityType. */
		uint8_t security_type;
		/**
		 * Number of multiplexed KCP streams. Updates for entities are sharded across streams: the higher
		 * the multiplex level, the fewer entities might be impacted by a delayed update. Increasing the
		 * number of multiplexed streams may increase CPU usage.
		 */
		uint8_t multiplex_level;

		/** KCP parameters for messages sent from the bridge to the worker. */
		Worker_KcpTransportParameters downstream_kcp;
		/** KCP parameters for messages sent from the worker to the bridge. */
		Worker_KcpTransportParameters upstream_kcp;

		/** Erasure codec parameters for messages sent from the bridge to the worker. */
		const Worker_ErasureCodecParameters* downstream_erasure_codec;
		/** Erasure codec parameters for messages sent from the worker to the bridge. */
		const Worker_ErasureCodecParameters* upstream_erasure_codec;

		/** Heartbeat parameters for heartbeats from the bridge to the worker. */
		const Worker_HeartbeatParameters* downstream_heartbeat;
		/** Heartbeat parameters for heartbeats from the worker to the bridge. */
		const Worker_HeartbeatParameters* upstream_heartbeat;

		/** Compression parameters for messages sent from the bridge to the worker. */
		const Worker_CompressionParameters* downstream_compression;
		/** Compression parameters for messages sent from the worker to the bridge. */
		const Worker_CompressionParameters* upstream_compression;

		/** Flow control parameters. */
		const Worker_FlowControlParameters* flow_control;
	} Worker_ModularKcpNetworkParameters;

	/**
	 * Parameters for configuring the stack for a modular TCP connection. Used by
	 * Worker_NetworkParameters.
	 */
	typedef struct Worker_ModularTcpNetworkParameters {
		/** Type of encryption layer security to use, defined in Worker_NetworkSecurityType. */
		uint8_t security_type;
		/**
		 * The number of multiplexed TCP connections to use. Updates for entities are sharded across
		 * connections: the higher the multiplex level, the fewer entities might be impacted by a delayed
		 * update. Messages across connections cannot be packed into the same TCP packet, which may result
		 * in higher bandwidth usage. Increasing the number of multiplexed streams may also increase CPU
		 * usage.
		 */
		uint8_t multiplex_level;

		/** TCP parameters for messages sent from the bridge to the worker. */
		Worker_TcpTransportParameters downstream_tcp;
		/** TCP parameters for messages sent from the worker to the bridge. */
		Worker_TcpTransportParameters upstream_tcp;

		/** Heartbeat parameters for heartbeats from the bridge to the worker. */
		const Worker_HeartbeatParameters* downstream_heartbeat;
		/** Heartbeat parameters for heartbeats from the worker to the bridge. */
		const Worker_HeartbeatParameters* upstream_heartbeat;

		/** Compression parameters for messages sent from the bridge to the worker. */
		const Worker_CompressionParameters* downstream_compression;
		/** Compression parameters for messages sent from the worker to the bridge. */
		const Worker_CompressionParameters* upstream_compression;

		/** Flow control parameters. */
		const Worker_FlowControlParameters* flow_control;
	} Worker_ModularTcpNetworkParameters;

	/** Network connection type used by Worker_NetworkParameters. */
	typedef enum Worker_NetworkConnectionType {
		/** (deprecated) Use this flag to connect over TCP. */
		WORKER_NETWORK_CONNECTION_TYPE_TCP = 0,
		/** (deprecated) Use this flag to connect over RakNet. */
		WORKER_NETWORK_CONNECTION_TYPE_RAKNET = 1,
		/** (deprecated) Use this flag to connect over KCP. */
		WORKER_NETWORK_CONNECTION_TYPE_KCP = 2,
		/**
		 * Use this flag to connect over the modular KCP stack. Modular KCP connections run on a new
		 * network stack with additional optional features such as compression.
		 */
		WORKER_NETWORK_CONNECTION_TYPE_MODULAR_KCP = 3,
		/**
		 * Use this flag to connect over the modular TCP stack. Modular TCP connections run on a new
		 * network stack with additional optional features such as compression.
		 */
		WORKER_NETWORK_CONNECTION_TYPE_MODULAR_TCP = 4
	  } Worker_NetworkConnectionType;

	/** Parameters for configuring the network connection. */
	typedef struct Worker_NetworkParameters {
		/**
		 * Set this flag to non-zero to connect to SpatialOS using the externally-visible IP address. This
		 * flag must be set when connecting externally (i.e. from outside the cloud) to a cloud
		 * deployment.
		 */
		uint8_t use_external_ip;
		/**
		 * Type of network connection to use when connecting to SpatialOS, defined in
		 * Worker_NetworkConnectionType.
		 */
		uint8_t connection_type;
		/** (deprecated) Parameters used if the WORKER_NETWORK_CONNECTION_TYPE_RAKNET flag is set. */
		Worker_RakNetNetworkParameters raknet;
		/** (deprecated) Parameters used if the WORKER_NETWORK_CONNECTION_TYPE_TCP flag is set. */
		Worker_TcpNetworkParameters tcp;
		/** (deprecated) Parameters used if the WORKER_NETWORK_CONNECTION_TYPE_KCP flag is set. */
		Worker_KcpNetworkParameters kcp;
		/** Parameters used if the WORKER_NETWORK_CONNECTION_TYPE_MODULAR_KCP flag is set. */
		Worker_ModularKcpNetworkParameters modular_kcp;
		/** Parameters used if the WORKER_NETWORK_CONNECTION_TYPE_MODULAR_TCP flag is set. */
		Worker_ModularTcpNetworkParameters modular_tcp;
		/** Timeout for the connection to SpatialOS to be established. */
		uint64_t connection_timeout_millis;
		/** Default timeout for worker commands if one is not specified when command is sent. */
		uint32_t default_command_timeout_millis;
	} Worker_NetworkParameters;

	/**
	 * (Deprecated) Tuning parameters for configuring protocol logging in the SDK. Used by
	 * Worker_ConnectionParameters.
	 */
	typedef struct Worker_ProtocolLoggingParameters {
		/** Log file names are prefixed with this prefix, are numbered, and have the extension .log. */
		const char* log_prefix;
		/**
		 * Maximum number of log files to keep. Note that logs from any previous protocol logging
		 * sessions will be overwritten.
		 */
		uint32_t max_log_files;
		/** When a log file reaches this size, a new log file is created. */
		uint32_t max_log_file_size_bytes;
	} Worker_ProtocolLoggingParameters;

	/** Parameters for configuring the rotating log files used for a logsink. */
	typedef struct Worker_RotatingLogFileParameters {
		/** Log file names are prefixed with this prefix, are numbered, and have the extension .log. */
		const char* log_prefix;
		/**
		 * Maximum number of log files to keep. Note that logs from any previous protocol logging
		 * sessions are overwritten.
		 */
		uint32_t max_log_files;
		/** When a log file reaches this size, a new log file is created. */
		uint32_t max_log_file_size_bytes;
	} Worker_RotatingLogFileParameters;

	/**
	 * Custom callback for filtering log messages. Return true if a message with the given categories
	 * and level should be logged.
	 */
	typedef uint8_t Worker_LogFilterCallback(void* user_data, uint32_t categories,
											 Worker_LogLevel level);

	/** Parameters for configuring a custom filter predicate */
	typedef struct Worker_LogFilterParameters {
		/**
		 * Combination of Worker_LogCategory flags defining the messages that should be received. If a
		 * message has at least one of these flags, it is logged to the sink.
		 */
		uint32_t categories;
		/** All messages of log level >= level are received; defined in Worker_LogLevel enumeration */
		uint8_t level;
		/**
		 * If set, all log messages are sent through this filter function and ignore the categories
		 * and level members above. Note that this callback will be called concurrently from different
		 * threads. You need to synchronize access to data that is shared with the thread this callback
		 * was set on.
		 */
		Worker_LogFilterCallback* callback;
		/** Pointer to user-defined data that is passed to each callback invocation */
		void* user_data;
	} Worker_LogFilterParameters;

	/** Represents a log message to be consumed by a user-defined callback. */
	typedef struct Worker_LogData {
		/** Date & time when this message was generated. */
		const char* timestamp;
		/** The log categories that this message was generated with. */
		uint32_t categories;
		/** The log level of this message. */
		uint8_t log_level;
		/** The message content. */
		const char* content;
	} Worker_LogData;

	/**
	 * Custom callback for receiving log messages. Will be called for all log messages that pass through
	 * the filter.
	 */
	typedef void Worker_LogCallback(void* user_data, const Worker_LogData* message);

	/** Parameters for configuring a log callback */
	typedef struct Worker_LogCallbackParameters {
		/** Pointer to callback function that receives new log messages */
		Worker_LogCallback* log_callback;
		/** Pointer to user-defined data that will be passed to each callback invocation */
		void* user_data;
	} Worker_LogCallbackParameters;

	/** Parameters for configuring a logsink in the SDK. */
	typedef struct Worker_LogsinkParameters {
		/** The type of logsink to use. Depending on this value, a subset of other members is used.
		 * Defined in Worker_LogsinkType enumeration. */
		uint8_t logsink_type;
		/** Parameters for controlling filtering of log messages. */
		Worker_LogFilterParameters filter_parameters;
		/**
		 * Parameters for a rotating log file sink. Only used if logsink_type ==
		 * WORKER_LOGSINK_TYPE_ROTATING_FILE.
		 */
		Worker_RotatingLogFileParameters rotating_logfile_parameters;
		/**
		 * Parameters for custom log callback. Only used if logsink_type == WORKER_LOGSINK_TYPE_CALLBACK.
		 * Note that this callback will be called from different threads. You need to synchronize access
		 * to data that is shared with the thread this callback was set on. However, the logger guarantees
		 * that only a single log callback is run at a time per connection which can sometimes eliminate
		 * the need for synchronization.
		 */
		Worker_LogCallbackParameters log_callback_parameters;
	} Worker_LogsinkParameters;

	/**
	 * Parameters for configuring thread affinity. Affinity masks are bit masks where
	 * having 1 in the nth least significant position means the thread will be permitted to run
	 * on the nth core. If an affinity mask is set to zero, the group of threads using that mask
	 * will have no thread affinity. Used by Worker_ConnectionParameters.
	 */
	typedef struct Worker_ThreadAffinityParameters {
		/** Affinity mask for threads related to receiving network ops. */
		uint64_t receive_threads_affinity_mask;
		/** Affinity mask for threads related to sending network ops. */
		uint64_t send_threads_affinity_mask;
		/** Affinity mask for short-lived threads. */
		uint64_t temporary_threads_affinity_mask;
	} Worker_ThreadAffinityParameters;

    struct Trace_EventTracer;
	/** Parameters for creating a Worker_Connection and connecting to SpatialOS. */
	typedef struct Worker_ConnectionParameters {
		/** Worker type (platform). */
		const char* worker_type;

		/** Network parameters. */
		Worker_NetworkParameters network;

		/**
		 * Number of messages that can be stored on the send queue. When the send queue is full, calls to
		 * Worker_Connection_Send functions can block.
		 */
		uint32_t send_queue_capacity;
		/**
		 * Number of messages that can be stored on the receive queue. When the receive queue is full,
		 * SpatialOS can apply QoS and drop messages to the worker.
		 */
		uint32_t receive_queue_capacity;
		/**
		 * Number of messages logged by the SDK that can be stored in the log message queue. When the log
		 * message queue is full, messages logged by the SDK can be dropped.
		 */
		uint32_t log_message_queue_capacity;
		/**
		 * The Connection tracks several internal metrics, such as send and receive queue statistics. This
		 * parameter controls how frequently the Connection will return a MetricsOp reporting its built-in
		 * metrics. If set to zero, this functionality is disabled.
		 */
		uint32_t built_in_metrics_report_period_millis;

		/** (Deprecated) Parameters for configuring legacy protocol logging parameters. */
		Worker_ProtocolLoggingParameters protocol_logging;
		/** (Deprecated) Whether to enable legacy protocol logging at startup. */
		uint8_t enable_protocol_logging_at_startup;
		/** Number of logsinks configured. */
		uint32_t logsink_count;
		/** Array of logsinks that receive filtered log messages from the SDK. */
		const Worker_LogsinkParameters* logsinks;
		/**
		 * Whether to enable all logsinks at startup. Note that this is automatically true if
		 * enable_protocol_logging_at_startup is set to true.
		 */
		uint8_t enable_logging_at_startup;
		/**
		 * Whether to enable dynamic components.
		 * If this field is true, add and remove component ops are emitted on authority change. These ops,
		 * like all add and remove component ops, must be treated in an idempotent way (i.e. they replace
		 * any existing value on the worker for the component).
		 */
		uint8_t enable_dynamic_components;

		/** Parameters for configuring thread affinity. */
		Worker_ThreadAffinityParameters thread_affinity;

		/** Number of component vtables. */
		uint32_t component_vtable_count;
		/** Component vtable for each component that the connection will deal with. */
		const Worker_ComponentVtable* component_vtables;
		/** Default vtable used when a component is not registered. Only used if not NULL. */
		const Worker_ComponentVtable* default_component_vtable;
		Worker_NameVersionPair* versions;
		Trace_EventTracer* event_tracer;
	} Worker_ConnectionParameters;

	/** Parameters for authenticating using a SpatialOS login token. */
	typedef struct Worker_LoginTokenCredentials {
		/** The token would typically be provided on the command-line by the SpatialOS launcher. */
		const char* token;
	} Worker_LoginTokenCredentials;

    typedef struct Worker_LoginStatus
    {
    	Worker_ConnectionStatusCode code;
    	char* detail;
    	
    }Worker_LoginStatus;

    typedef struct Worker_LoginToken
    {
	    char* login_token;
    	char* deployment_name;
    }Worker_LoginToken;

     

  

	/** Parameters for authenticating using Steam credentials. */
	typedef struct Worker_SteamCredentials {
		/**
		 * Steam ticket for the steam app ID and publisher key corresponding to the project name specified
		 * in the Worker_LocatorParameters. Typically obtained from the steam APIs.
		 */
		const char* ticket;
		/**
		 * Deployment tag to request access for. If non-empty, must match the following regex:
		 * [A-Za-z0-9][A-Za-z0-9_]*
		 */
		const char* deployment_tag;
	} Worker_SteamCredentials;

	/** Parameters for authenticating using a Player Identity Token and Login Token. */
	typedef struct Worker_PlayerIdentityCredentials {
		/**
		 * Authenticates a user to a single deployment. Obtained from a game authentication server
		 * using a PIT.
		 */
		const char* player_identity_token;
		/**
		 * Uniquely identifies a user across deployments, and is provided by a game authentication
		 * server.
		 */
		const char* login_token;
	} Worker_PlayerIdentityCredentials;

	/** Locator credentials type used by the Worker_LocatorParameters struct. */
	typedef enum Worker_LocatorCredentialsTypes {
		WORKER_LOCATOR_LOGIN_TOKEN_CREDENTIALS = 1,
		WORKER_LOCATOR_STEAM_CREDENTIALS = 2,
		WORKER_LOCATOR_PLAYER_IDENTITY_CREDENTIALS = 3
	  } Worker_LocatorCredentialsTypes;

	/** Parameters for authenticating and logging in to a SpatialOS deployment. */
	typedef struct Worker_LocatorParameters {
		/** The name of the SpatialOS project. */
		const char* project_name;
		/**
		 * Type of credentials to use when authenticating via the Locator, defined in
		 * Worker_LocatorCredentialsTypes
		 */
		uint8_t credentials_type;
		/** Parameters used if the WORKER_LOGIN_TOKEN_CREDENTIALS flag is set. */
		Worker_LoginTokenCredentials login_token;
		/** Parameters used if the WORKER_STEAM_CREDENTIALS flag is set. */
		Worker_SteamCredentials steam;
		/** The player identity token/login token pair used for authentication. */
		Worker_PlayerIdentityCredentials player_identity;
		/** Whether to use an insecure (non-TLS) connection for local development. */
		uint8_t use_insecure_connection;
		/** (Deprecated) Parameters for configuring legacy protocol logging. */
		Worker_ProtocolLoggingParameters logging;
		/** (Deprecated) Whether to enable legacy protocol logging for the Locator flow. */
		uint8_t enable_logging;
		/** Number of logsinks configured. */
		uint32_t logsink_count;
		/**
		 * Array of logsinks that receive filtered log messages from the SDK. These are enabled by
		 * default.
		 */
		const Worker_LogsinkParameters* logsinks;
	} Worker_LocatorParameters;

	/** Details of a specific deployment obtained via Worker_Locator_GetDeploymentListAsync. */
	typedef struct Worker_Deployment {
		/** Name of the deployment. */
		const char* deployment_name;
		/** The name of the assembly used by this deployment. */
		const char* assembly_name;
		/** Description of the deployment. */
		const char* description;
		/** Number of users currently connected to the deployment. */
		uint32_t users_connected;
		/** Total user capacity of the deployment. */
		uint32_t users_capacity;
	} Worker_Deployment;

	/** A deployment list obtained via Worker_Locator_GetDeploymentListAsync. */
	typedef struct Worker_DeploymentList {
		/** Number of deployments. */
		uint32_t deployment_count;
		/** Array of deployments. */
		Worker_Deployment* deployments;
		/** Will be non-NULL if an error occurred. */
		const char* error;
	} Worker_DeploymentList;

	/**
	 * A queue status update when connecting to a deployment via Worker_Locator_ConnectAsync.
	 */
	typedef struct Worker_QueueStatus {
		/** Position in the queue. Decreases as we advance to the front of the queue. */
		uint32_t position_in_queue;
		/** Will be non-NULL if an error occurred. */
		const char* error;
	} Worker_QueueStatus;

	/** Component update parameters. Used to modify the behaviour of a component update request. */
	typedef struct Worker_UpdateParameters {
		/**
		 * Controls how the update is sent back to the worker from which it was sent. Defined in the
		 * Worker_ComponentUpdateLoopback enumeration.
		 */
		uint8_t loopback;
	} Worker_UpdateParameters;

	/** Command parameters. Used to modify the behaviour of a command request. */
	typedef struct Worker_CommandParameters {
		/**
		 * Allow command requests to bypass the bridge when this worker is authoritative over the target
		 * entity-component.
		 */
		uint8_t allow_short_circuit;
	} Worker_CommandParameters;

	/** Information about status of a network request. */
	typedef struct Worker_ConnectionStatus {
		/** The status of the request. This value is a member of the enum Worker_ConnectionStatusCode. */
		uint8_t code;
		/**
		 * Detailed, human readable description of the connection status.
		 * Will be "OK" if no error occurred.
		 */
		const char* detail;
	} Worker_ConnectionStatus;

	/** The parameters used when creating a player identity token. */
	typedef struct Worker_PlayerIdentityTokenRequest {
		/** The development authentication token used for exchanging the player identity token.
		 */
		const char* development_authentication_token;
		/** The ID of the player we are generating a PIT for. */
		const char* player_id;
		/**
		 * The lifetime duration of the requested PIT. This is an optional field.
		 * If the pointer is null, a default value of 24 hours will be used.
		 */
		const uint32_t* duration_seconds;
		/** The player's display name. This is an optional field. */
		const char* display_name;
		/**
		 * Additional metadata that can be stored in the PIT. This is an optional field.
		 * You can use this to securely attach extra attributes in a format you choose (e.g. JSON
		 * payload).
		 */
		const char* metadata;
		/**
		 * Whether to use an insecure (non-TLS) connection for local development.
		 * An insecure connection must be used when connecting to a local development authentication
		 * service.
		 * A secure connection must be used when connecting to a cloud development authentication
		 * service.
		 */
		uint8_t use_insecure_connection;
	} Worker_PlayerIdentityTokenRequest;

	/** The result of creating a player identity token. */
	typedef struct Worker_PlayerIdentityTokenResponse {
		/** The returned player identity token. */
		const char* player_identity_token;
		/** The status code and a human readable description of the status of the request. */
		Worker_ConnectionStatus status;
	} Worker_PlayerIdentityTokenResponse;

	/** The parameters used when creating a login token. */
	typedef struct Worker_LoginTokensRequest {
		/** The player identity token of the player */
		const char* player_identity_token;
		/** The worker type for which the requested LTs are scoped for. */
		const char* worker_type;
		/**
		 * The lifetime duration of the requested LTs. This is an optional field.
		 * If the pointer is null, a default value of 15 minutes will be used.
		 */
		const uint32_t* duration_seconds;
		/**
		 * Whether to use an insecure (non-TLS) connection for local development.
		 * An insecure connection must be used when connecting to a local development login service.
		 * A secure connection must be used when connecting to a cloud development login service.
		 */
		uint8_t use_insecure_connection;
	} Worker_LoginTokensRequest;

	/** A single login token with additional details. */
	typedef struct Worker_LoginTokenDetails {
		/** The UUID of the deployment. */
		const char* deployment_id;
		/** The name of the deployment */
		const char* deployment_name;
		/** The number of tags that the deployment contains. */
		uint32_t tag_count;
		/** The tags that the deployment contains. */
		const char** tags;
		/** The generated login token for this deployment. */
		const char* login_token;
	} Worker_LoginTokenDetails;

	/** A login token list obtained via Worker_CreateDevelopmentLoginTokens */
	typedef struct Worker_LoginTokensResponse {
		/** Number of login tokens. */
		uint32_t login_token_count;
		/** Array of login tokens. */
		Worker_LoginTokenDetails* login_tokens;
		/** The status code and a human readable description of the status of the request. */
		Worker_ConnectionStatus status;
	} Worker_LoginTokensResponse;

	/** Locator callback typedef. */
	typedef void Worker_DeploymentListCallback(void* user_data,
											   const Worker_DeploymentList* deployment_list);
	/** Locator callback typedef. */
	typedef uint8_t Worker_QueueStatusCallback(void* user_data, const Worker_QueueStatus* queue_status);
	/** PIT-creation callback typedef. */
	typedef void Worker_PlayerIdentityTokenResponseCallback(
		void* user_data, const Worker_PlayerIdentityTokenResponse* response);
	/** login token-creation callback typedef. */
	typedef void
	Worker_LoginTokensResponseCallback(void* user_data,
											 const Worker_LoginTokensResponse* response);
	/** Worker flags callback typedef. */
	typedef void Worker_GetWorkerFlagCallback(void* user_data, const char* value);

	typedef void* Worker_AllocateFunction(size_t size, void* state);
	typedef void Worker_DeallocateFunction(void* pointer, size_t size, void* state);

	/**
	 * Returns the SPATIALGDK_API_VERSION number that was defined when the library was compiled.
	 */
	WORKERSDK_API uint32_t SPATIALGDK_APIVersion(void);

	/**
	 * Returns the SPATIALGDK_API_VERSION string that was defined when the library was compiled.
	 */
	WORKERSDK_API const char* SPATIALGDK_APIVersionStr(void);

	/**
	 * Set custom allocation functions and state for managing memory within the API.
	 * The allocation function should allocate a block of memory of the size that is given by
	 * the argument and return a pointer to the first byte.
	 * The pointer must be suitably aligned to hold an object of any fundamental alignment and
	 * will be released by a matching call to the deallocation function with the same size.
	 * If either allocation or deallocation function throws, the application will terminate.
	 * Both allocation and deallocation functions must be thread-safe.
	 *
	 * You must call Worker_SetAllocator once before any other API calls. Calling it
	 * multiple times or after another API call has been made is undefined behaviour.
	 */
	WORKERSDK_API void Worker_SetAllocator(Worker_AllocateFunction* allocate,
											  Worker_DeallocateFunction* deallocate, void* state);

	/**
	 * Returns a new Worker_ConnectionParameters with default values set.
	 */
	WORKERSDK_API Worker_ConnectionParameters Worker_DefaultConnectionParameters(void);

	/**
	 * Creates a client which can be used to connect to a SpatialOS deployment via a locator service.
	 * This is the standard flow used to connect a local worker to a cloud deployment.
	 *
	 * The hostname would typically be "locator.improbable.io".
	 *
	 * The port number is used to connect to the locator service. This should be set to zero if
	 * connecting to a locator service running on the cloud.
	 */
	WORKERSDK_API Worker_Locator* Worker_Locator_Create(const char* hostname, uint16_t port,
													 const Worker_LocatorParameters* params);
	/** Frees resources for a Worker_Locator created with Worker_Locator_Create. */
	WORKERSDK_API void Worker_Locator_Destroy(Worker_Locator* locator);

	/**
	 * (Deprecated) Queries the current list of deployments for the project given in the
	 * Worker_LocatorParameters.
	 */
	WORKERSDK_API Worker_DeploymentListFuture*
	Worker_Locator_GetDeploymentListAsync(const Worker_Locator* locator);
	/**
	 * (Deprecated) Connects to a specific deployment. The deployment name should be obtained by calling
	 * Worker_Locator_GetDeploymentListAsync. The callback should return zero to cancel queuing,
	 * or non-zero to continue queueing.
	 *
	 * Returns a Worker_ConnectionFuture that can be used to obtain a Worker_Connection
	 * by using Worker_ConnectionFuture_Get. Caller is responsible for destroying it when no
	 * longer needed by using Worker_ConnectionFuture_Destroy.
	 */
	WORKERSDK_API Worker_ConnectionFuture*
	Worker_Locator_ConnectAndQueueAsync(const Worker_Locator* locator, const char* deployment_name,
										const Worker_ConnectionParameters* params, void* data,
										Worker_QueueStatusCallback* callback);

	/**
	 * Connects to a specific deployment. The deployment is defined by the login token,
	 * which is obtained from a game authentication server, along with a player identity token.
	 *
	 * Returns a Worker_ConnectionFuture that can be used to obtain a Worker_Connection
	 * by using Worker_ConnectionFuture_Get. Caller is responsible for destroying it when no
	 * longer needed by using Worker_ConnectionFuture_Destroy.
	 */
	WORKERSDK_API Worker_ConnectionFuture*
	Worker_Locator_ConnectAsync(const Worker_Locator* locator,
								const Worker_ConnectionParameters* params);

	/**
	 * Connect to a SpatialOS deployment via a receptionist. This is the flow used to connect a managed
	 * worker running in the cloud alongside the deployment, and also to connect any local worker to a
	 * (local or remote) deployment via a locally-running receptionist.
	 *
	 * The hostname and port would typically be provided by SpatialOS on the command-line, if this is a
	 * managed worker on the cloud, or otherwise be predetermined (e.g. localhost:7777 for the default
	 * receptionist of a locally-running deployment).
	 *
	 * Returns a Worker_ConnectionFuture that can be used to obtain a Worker_Connection
	 * by using Worker_ConnectionFuture_Get. Caller is responsible for destroying it when no
	 * longer needed by using Worker_ConnectionFuture_Destroy.
	 */
	WORKERSDK_API Worker_ConnectionFuture* Worker_ConnectAsync(const char* hostname, uint16_t port,
															const char* worker_id,
															const Worker_ConnectionParameters* params);

	/** Destroys a Worker_DeploymentListFuture. Blocks until the future has completed. */
	WORKERSDK_API void Worker_DeploymentListFuture_Destroy(Worker_DeploymentListFuture* future);
	/**
	 * Gets the result of a Worker_DeploymentListFuture, waiting for up to *timeout_millis to
	 * become available (or forever if timeout_millis is NULL).
	 *
	 * It is an error to call this method again once it has succeeded (e.g. not timed out) once.
	 */
	WORKERSDK_API void Worker_DeploymentListFuture_Get(Worker_DeploymentListFuture* future,
													const uint32_t* timeout_millis, void* data,
													Worker_DeploymentListCallback* callback);

	/** Calls the Development Authentication Service to generate a PIT. */
	WORKERSDK_API Worker_PlayerIdentityTokenResponseFuture*
	Worker_CreateDevelopmentPlayerIdentityTokenAsync(
		const char* hostname, uint16_t port, Worker_PlayerIdentityTokenRequest* params);
	/**
	 * Destroys a Worker_PlayerIdentityTokenResponseFuture. Blocks until the future has
	 * completed.
	 */
	WORKERSDK_API void Worker_PlayerIdentityTokenResponseFuture_Destroy(
		Worker_PlayerIdentityTokenResponseFuture* future);
	/**
	 * Gets the result of a Worker_PlayerIdentityTokenResponseFuture, waiting for up to
	 * *timeout_millis to become available (or forever if timeout_millis is NULL).
	 *
	 * It is an error to call this method again once it has succeeded (e.g. not timed out) once.
	 */
	WORKERSDK_API void Worker_PlayerIdentityTokenResponseFuture_Get(
		Worker_PlayerIdentityTokenResponseFuture* future, const uint32_t* timeout_millis,
		void* data, Worker_PlayerIdentityTokenResponseCallback* callback);

	/** Calls the Development Login Service to generate a login token list. */
	WORKERSDK_API Worker_LoginTokensResponseFuture*
	Worker_CreateDevelopmentLoginTokensAsync(const char* hostname, uint16_t port,
												   Worker_LoginTokensRequest* params);
	/**
	 * Destroys a Worker_LoginTokensResponseFuture. Blocks until the future has
	 * completed.
	 */
	WORKERSDK_API void
	Worker_LoginTokensResponseFuture_Destroy(Worker_LoginTokensResponseFuture* future);
	/**
	 * Gets the result of a Worker_LoginTokensResponseFuture, waiting for up to
	 * *timeout_millis to become available (or forever if timeout_millis is NULL).
	 *
	 * It is an error to call this method again once it has succeeded (e.g. not timeout out) once.
	 */
	WORKERSDK_API void
	Worker_LoginTokensResponseFuture_Get(Worker_LoginTokensResponseFuture* future,
											   const uint32_t* timeout_millis, void* data,
											   Worker_LoginTokensResponseCallback* callback);

	/** Destroys a Worker_ConnectionFuture. Blocks until the future has completed. */
	WORKERSDK_API void Worker_ConnectionFuture_Destroy(Worker_ConnectionFuture* future);
	/**
	 * Gets the result of a Worker_ConnectionFuture, waiting for up to *timeout_millis to
	 * become available (or forever if timeout_millis is NULL). It returns NULL in case of a timeout.
	 *
	 * It is an error to call this method again once it has succeeded (e.g. not timed out) once.
	 */
	WORKERSDK_API Worker_Connection* Worker_ConnectionFuture_Get(Worker_ConnectionFuture* future,
															  const uint32_t* timeout_millis);

	/**
	 * Frees resources for a Worker_Connection created with Worker_ConnectAsync or
	 * Worker_Locator_ConnectAsync.
	 */
	WORKERSDK_API void Worker_Connection_Destroy(Worker_Connection* connection);
	/**
	 * Indicates to the network layer that all previous invocations of the Worker_Connection_Send*
	 * methods should be flushed to the network as soon as possible. A common usage pattern is to call
	 * this function after all state changes have been applied at the end of a frame.
	 *
	 * This method is asynchronous. In particular, calling is an indication that a flush is wanted, and
	 * can return before messages are fully serialized and put on the network.
	 * This currently only has an effect if using the Worker_ModularKcpNetworkParameters or
	 * Worker_ModularTcpNetworkParameters.
	 */
	WORKERSDK_API void Worker_Connection_Flush(Worker_Connection* connection);
	/** Sends a log message from the worker to SpatialOS. */
	WORKERSDK_API void Worker_Connection_SendLogMessage(Worker_Connection* connection,
													 const Worker_LogMessage* log_message);
	/** Sends metrics data for the worker to SpatialOS. */
	WORKERSDK_API void Worker_Connection_SendMetrics(Worker_Connection* connection,
												  const Worker_Metrics* metrics);
	/**
	 * Requests SpatialOS to reserve multiple entity IDs.
	 *
	 * Note: We do not recommend reserving entity IDs. Instead, we recommend using
	 * Worker_SendCreateEntityRequest without specifying an entity ID. You can then use the
	 * automatically assigned entity ID provided in Worker_CreateEntityResponseOp.
	 */
	WORKERSDK_API Worker_RequestId Worker_Connection_SendReserveEntityIdsRequest(
		Worker_Connection* connection, const uint32_t number_of_entity_ids,
		const uint32_t* timeout_millis);
	/**
	 * Requests SpatialOS to create an entity. If components[i].schema_type is set, ownership is
	 * transferred to the SDK, and components[i].schema_type is set to NULL. If
	 * components[i].schema_type is NULL and components[i].user_handle is set, the entity data is
	 * serialized immediately using the corresponding vtable serialize function.
	 *
	 * Returns Worker_RequestId -1 if the component is not registered and the default vtable is
	 * not found.
	 */
	WORKERSDK_API Worker_RequestId Worker_Connection_SendCreateEntityRequest(
		Worker_Connection* connection, uint32_t component_count, Worker_ComponentData* components,
		const Worker_EntityId* entity_id, const uint32_t* timeout_millis);
	/** Requests SpatialOS to delete an entity. */
	WORKERSDK_API Worker_RequestId Worker_Connection_SendDeleteEntityRequest(
		Worker_Connection* connection, Worker_EntityId entity_id, const uint32_t* timeout_millis);
	/**
	 * Queries SpatialOS for entity data.
	 *
	 * Returns Worker_RequestId -1 if the query constraint or result type are not valid.
	 */
	WORKERSDK_API Worker_RequestId Worker_Connection_SendEntityQueryRequest(
		Worker_Connection* connection, const Worker_EntityQuery* entity_query,
		const uint32_t* timeout_millis);
	/**
	 * Sends a component update for the given entity to SpatialOS.
	 *
	 * If component_update->schema_type is set, ownership is transferred to the SDK, and
	 * component_update->schema_type is set to NULL. If component_update->user_handle is set, then it
	 * will be copied with the corresponding vtable copy function, then the copy is later freed with the
	 * vtable free function.
	 *
	 * Note that if update_parameters.loopback = 1 or update_parameters = NULL, the component update
	 * operation is added to the operation list and will be returned by a subsequent call to
	 * Worker_Connection_GetOpList.
	 *
	 * Returns WORKER_RESULT_FAILURE if the component is not registered and the default vtable is
	 * not found.
	 */
	WORKERSDK_API int8_t Worker_Connection_SendComponentUpdate(
		Worker_Connection* connection, Worker_EntityId entity_id,
		Worker_ComponentUpdate* component_update, const Worker_UpdateParameters* update_parameters);
	/**
	 * Adds a new component to the given entity in SpatialOS.
	 *
	 * If component_data->schema_type is set, ownership is transferred to the SDK, and
	 * component_data->schema_type is set to NULL. If component_data->user_handle is set, then it
	 * will be copied with the corresponding vtable copy function, then the copy is later freed with the
	 * vtable free function.
	 *
	 * Note that if update_parameters.loopback = 1 or update_parameters = NULL, the add component
	 * operation is added to the operation list and will be returned by a subsequent call to
	 * Worker_Connection_GetOpList.
	 *
	 * Returns WORKER_RESULT_FAILURE if the component is not registered and the default vtable is
	 * not found.
	 */
	WORKERSDK_API int8_t Worker_Connection_SendAddComponent(
		Worker_Connection* connection, Worker_EntityId entity_id, Worker_ComponentData* component_data,
		const Worker_UpdateParameters* update_parameters);
	/**
	 * Removes a component from a given entity in SpatialOS.
	 *
	 * If update_parameters.loopback = 1 or update_parameters = NULL, the remove component operation
	 * is added to the operation list and will be returned by a subsequent call to
	 * Worker_Connection_GetOpList.
	 *
	 * In order to use this function, Worker_ConnectionParameters::enable_dynamic_components must be set
	 * to 1 (true).
	 *
	 * This function does not check whether the worker currently has authority over the component, you
	 * must make sure the worker has authority in order to remove the component.
	 */
	WORKERSDK_API void
	Worker_Connection_SendRemoveComponent(Worker_Connection* connection, Worker_EntityId entity_id,
										  Worker_ComponentId component_id,
										  const Worker_UpdateParameters* update_parameters);
	/**
	 * Sends a command request targeting the given entity and component to SpatialOS. If timeout_millis
	 * is null, the default will be used.
	 *
	 * If request->schema_type is set, ownership is transferred to the SDK, and request->schema_type is
	 * set to NULL. If request->user_handle is set, then it will be copied with the corresponding vtable
	 * copy function, then the copy is later freed with the vtable free function.
	 *
	 * If command parameters argument is NULL, then command short circuiting will be disabled.
	 *
	 * Returns Worker_RequestId -1 if the component is not registered and the default vtable is
	 * not found.
	 */
	WORKERSDK_API Worker_RequestId Worker_Connection_SendCommandRequest(
		Worker_Connection* connection, Worker_EntityId entity_id, Worker_CommandRequest* request,
		const uint32_t* timeout_millis, const Worker_CommandParameters* command_parameters);
	/**
	 * Sends a command response for the given request ID to SpatialOS.
	 *
	 * If response->schema_type is set, ownership is transferred to the SDK, and response->schema_type
	 * is set to NULL. If response->user_handle is set, then it will be copied with the corresponding
	 * vtable copy function, then the copy is later freed with the vtable free function.
	 *
	 * Returns WORKER_RESULT_FAILURE if the component is not registered and the default vtable is
	 * not found.
	 */
	WORKERSDK_API int8_t Worker_Connection_SendCommandResponse(Worker_Connection* connection,
															Worker_RequestId request_id,
															Worker_CommandResponse* response);
	/** Sends an explicit failure for the given command request ID to SpatialOS. */
	WORKERSDK_API void Worker_Connection_SendCommandFailure(Worker_Connection* connection,
														 Worker_RequestId request_id,
														 const char* message);
	/**
	 * (Deprecated) Sends a diff-based component interest update for the given entity to SpatialOS. By
	 * default, the worker receives data for all entities according to the default component interest
	 * specified in its bridge settings. This function allows interest override by (entity ID, component
	 * ID) pair to force the data to either always be sent or never be sent. Note that this does not
	 * apply if the worker is _authoritative_ over a particular (entity ID, component ID) pair, in which
	 * case the data is always sent.
	 */
	WORKERSDK_API void
	Worker_Connection_SendComponentInterest(Worker_Connection* connection, Worker_EntityId entity_id,
											const Worker_InterestOverride* interest_override,
											uint32_t interest_override_count);
	/**
	 * Sends an acknowledgement of the receipt of an AuthorityLossImminent authority change for a
	 * component. Sending the acknowledgement signifies that this worker is ready to lose authority
	 * over the component.
	 */
	WORKERSDK_API void Worker_Connection_SendAuthorityLossImminentAcknowledgement(
		Worker_Connection* connection, Worker_EntityId entity_id, Worker_ComponentId component_id);
	/**
	 * (Deprecated) Enables or disables legacy protocol logging. Logging uses the parameters specified
	 * when the connection was created. Enabling it when already enabled, or disabling it when already
	 * disabled, does nothing.
	 *
	 * Note that logs from any previous protocol logging sessions will be overwritten.
	 *
	 * Either use this or the array of logsinks in Worker_ConnectionParameters. If you call this to
	 * disable protocol logging while other logsinks are enabled, those will be disabled as well.
	 */
	WORKERSDK_API void Worker_Connection_SetProtocolLoggingEnabled(Worker_Connection* connection,
																uint8_t enabled);
	/**
	 * Reenables all logging. If logging was already enabled or no logsinks had been configured during
	 * connecting, does nothing. This also re-enables the deprecated protocol logging, if it was enabled
	 * at least once using Worker_Connection_SetProtocolLoggingEnabled before.
	 */
	WORKERSDK_API void Worker_Connection_EnableLogging(Worker_Connection* connection);
	/**
	 * Disables all logging. If logging was already disabled or no logsinks had been configured during
	 * connecting, does nothing. This also disables the deprecated protocol logging, if it was enabled
	 * at least once using Worker_Connection_SetProtocolLoggingEnabled before.
	 */
	WORKERSDK_API void Worker_Connection_DisableLogging(Worker_Connection* connection);
	/**
	 * Returns true if the connection has been successfully created and communication is ongoing.
	 * DEPRECATED: Equivalent to Worker_Connection_GetConnectionStatusCode(connection) ==
	 *             WORKER_CONNECTION_STATUS_CODE_SUCCESS.
	 */
	WORKERSDK_API uint8_t Worker_Connection_IsConnected(const Worker_Connection* connection);
	/**
	 * Returns a value from the Worker_ConnectionStatusCode enum. Returns
	 * WORKER_CONNECTION_STATUS_SUCCESS if the connection is connected and usable, otherwise a
	 * value indicating the type of error that occurred.
	 */
	WORKERSDK_API uint8_t Worker_Connection_GetConnectionStatusCode(const Worker_Connection* connection);
	/**
	 * Returns a null terminated string containing more detailed information about the connection
	 * status. The returned pointer points to data that is owned by the SDK and will remain valid for
	 * the lifetime of the connection.
	 */
	WORKERSDK_API const char*
	Worker_Connection_GetConnectionStatusDetailString(const Worker_Connection* connection);
	/**
	 * Retrieves the ID of the worker as assigned by the runtime as a null terminated string. The
	 * returned pointer points to data that is owned by the SDK and will remain valid for the lifetime
	 * of the connection. If the connection has failed, then the returned string will be a valid but
	 * empty string.
	 */
	WORKERSDK_API const char* Worker_Connection_GetWorkerId(const Worker_Connection* connection);
	/**
	 * Retrieves the attributes associated with the worker at runtime. The result to data that is owned
	 * by the SDK and will remain valid for the lifetime of the connection. If the connection has
	 * failed, then the returned array of strings will be NULL.
	 */
	WORKERSDK_API const Worker_WorkerAttributes*
	Worker_Connection_GetWorkerAttributes(const Worker_Connection* connection);
	/**
	 * Queries the worker flag with the given name. If the worker flag does not exist, the value will
	 * be NULL.
	 *
	 * Worker flags are remotely configurable and may change during the runtime of the worker,
	 * including addition and deletion.
	 */
	WORKERSDK_API void Worker_Connection_GetWorkerFlag(const Worker_Connection* connection,
													const char* name, void* user_data,
													Worker_GetWorkerFlagCallback* callback);

	/**
	 * Retrieves the list of operations that have occurred since the last call to this function.
	 *
	 * If timeout_millis is non-zero, the function will block until there is at least one operation to
	 * return, or the timeout has been exceeded. If the timeout is exceeded, an empty list will be
	 * returned.
	 *
	 * If timeout_millis is zero the function is non-blocking.
	 *
	 * It is the caller's responsibility to destroy the returned Worker_OpList with the
	 * Worker_OpList_Destroy function.
	 *
	 * Note: All data contained within the op-list (such as component data or updates) is owned by
	 * Worker_OpList, and must not be passed directly to another function in the SDK, such as
	 * Worker_Connection_SendComponentUpdate, without copying the data first. Otherwise, a double free
	 * could occur.
	 */
	WORKERSDK_API Worker_OpList* Worker_Connection_GetOpList(Worker_Connection* connection,
														  uint32_t timeout_millis);
	/** Frees resources for Worker_OpList returned by Worker_Connection_GetOpList. */
	WORKERSDK_API void Worker_OpList_Destroy(Worker_OpList* op_list);

	/** Snapshot type, used by the Worker_SnapshotParameters struct. */
	typedef enum Worker_SnapshotType {
		WORKER_SNAPSHOT_TYPE_BINARY = 0,
		WORKER_SNAPSHOT_TYPE_JSON = 1
	  } Worker_SnapshotType;

	/** Parameters for interacting with a snapshot. */
	typedef struct Worker_SnapshotParameters {
		/** Snapshot type, defined in Worker_SnapshotType */
		uint8_t snapshot_type;
		/** Number of component vtables. */
		uint32_t component_vtable_count;
		/** Component vtable for each component that the connection will deal with. */
		const Worker_ComponentVtable* component_vtables;
		/** Default vtable used when a component is not registered. Only used if not NULL. */
		const Worker_ComponentVtable* default_component_vtable;
		/** Schema bundle. Used only if the snapshot type is a JSON snapshot. */
		const Schema_Bundle* schema_bundle;
		/**
		 * JSON parameters. By default, if json_parameters is not specified, then SnapshotOutputStream
		 * will default to writing JSON in compact form. Used only if the snapshot type is a JSON
		 * snapshot.
		 */
		const Schema_JsonParameters* json_parameters;
	} Worker_SnapshotParameters;

	/** Enum to keep track of the state of streams. */
	typedef enum Worker_StreamState {
		/** The last operation succeeded and the stream is in a good state. */
		WORKER_STREAM_STATE_GOOD = 0,
		/** An internal stream error occurred and the stream is not in a usble state. */
		WORKER_STREAM_STATE_BAD = 1,
		/**
		 * The data processed in the last operation was not valid and the operation failed.
		 * The stream is still in a usable state.
		 */
		WORKER_STREAM_STATE_INVALID_DATA = 2,
		/** The end of file has been reached. */
		WORKER_STREAM_STATE_EOF = 3
	  } Worker_StreamState;

	/** Struct to keep track of the state of snapshot streams. */
	typedef struct Worker_SnapshotState {
		/** Stream state. */
		uint8_t stream_state;
		/** Error message. NULL if not set. */
		const char* error_message;
	} Worker_SnapshotState;

	/**
	 * Opens a Worker_SnapshotInputStream. The caller must manage the memory of the
	 * returned Worker_SnapshotInputStream* by calling Worker_SnapshotInputStream_Destroy to
	 * write the EOF and release resources.
	 *
	 * If an error occurs, a pointer to a Worker_SnapshotInputStream is still returned.
	 * Calling Worker_SnapshotInputStream_GetState with this pointer will return
	 * an error message describing any error that occured. In the event of an error, the caller still
	 * must release the memory of the Worker_SnapshotInputStream by calling
	 * Worker_SnapshotInputStream_Destroy.
	 */
	WORKERSDK_API Worker_SnapshotInputStream*
	Worker_SnapshotInputStream_Create(const char* filename, const Worker_SnapshotParameters* params);

	/** Closes the SnapshotInputStream and releases its resources. */
	WORKERSDK_API void Worker_SnapshotInputStream_Destroy(Worker_SnapshotInputStream* input_stream);

	/**
	 * Returns zero (false) if the Worker_SnapshotInputStream has reached the EOF
	 * of the Snapshot.
	 */
	WORKERSDK_API uint8_t Worker_SnapshotInputStream_HasNext(Worker_SnapshotInputStream* input_stream);

	/**
	 * Reads next Worker_Entity* entity from input_stream.
	 *
	 * Worker_SnapshotInputStream_ReadEntity manages the memory for the returned entity internally. The
	 * next call to Worker_SnapshotInputStream_ReadEntity or Worker_SnapshotInputStream_Destroy
	 * invalidates this value; use Worker_AcquireComponentData as usual to preserve component data.
	 *
	 * If an error occurs, or the stream has reached the end of the file, then this function will return
	 * a null pointer.
	 *
	 * In the case that a null pointer is returned, you must call Worker_SnapshotInputStream_GetState
	 * to get the details of what error occurred during the operation.
	 */
	WORKERSDK_API const Worker_Entity*
	Worker_SnapshotInputStream_ReadEntity(Worker_SnapshotInputStream* input_stream);

	/**
	 * Must be called after every Worker_SnapshotInputStream operation to get the state code of the
	 * stream after the previous operation.
	 *
	 * Returns a Worker_SnapshotState which contains the stream state code and an error message: if the
	 * code is WORKER_STREAM_STATE_GOOD no error occurred.
	 */
	WORKERSDK_API Worker_SnapshotState
	Worker_SnapshotInputStream_GetState(Worker_SnapshotInputStream* input_stream);

	/**
	 * Opens Worker_SnapshotOutputStream stream. The caller must manage the memory of the
	 * returned Worker_SnapshotOutputStream* by calling
	 * Worker_SnapshotOutputStream_Destroy to write the EOF and release resources.
	 *
	 * If an error occurs, a pointer to a Worker_SnapshotOutputStream is still returned.
	 * Calling Worker_SnapshotOutputStream_GetState with this pointer will return
	 * an error message describing any error that occured. In the event of an error, the caller still
	 * must release the memory of the Worker_SnapshotOutputStream by calling
	 * Worker_SnapshotOutputStream_Destroy.
	 */
	WORKERSDK_API Worker_SnapshotOutputStream*
	Worker_SnapshotOutputStream_Create(const char* filename, const Worker_SnapshotParameters* params);

	/** Closes the snapshot output stream and releases its resources. */
	WORKERSDK_API void Worker_SnapshotOutputStream_Destroy(Worker_SnapshotOutputStream* output_stream);

	/**
	 * Writes next entity_id, entity pair from input. Must call Worker_SnapshotOutputStream_GetState
	 * after this function to check whether any error occurred during operation.
	 */
	WORKERSDK_API void Worker_SnapshotOutputStream_WriteEntity(Worker_SnapshotOutputStream* output_stream,
															const Worker_Entity* entity);

	/**
	 * Must be called after every Worker_SnapshotOutputStream operation to get
	 * the state code of the stream after the previous operation.
	 *
	 * Returns a Worker_SnapshotState which contains the stream state code and an error
	 * message: if the code is WORKER_STREAM_STATE_GOOD no error occurred.
	 */
	WORKERSDK_API Worker_SnapshotState
	Worker_SnapshotOutputStream_GetState(Worker_SnapshotOutputStream* output_stream);

	/**
	 * Returns the last warning message generated by a call to Worker_SnapshotOutputStream_WriteEntity.
	 * Will return NULL if no warning was generated.
	 */
	WORKERSDK_API const char*
	Worker_SnapshotOutputStream_GetLastWarning(Worker_SnapshotOutputStream* output_stream);

   WORKERSDK_API Worker_EntityId Worker_Connection_GetWorkerEntityId(Worker_Connection* Connection);

   WORKERSDK_API bool InitWorkLog();

   WORKERSDK_API bool LoadAllSchema(const std::string& path,const std::string& json_path,const std::string &log_path);

#endif /* WORKER_SDK_C_INCLUDE_IMPROBABLE_C_WORKER_H */

 