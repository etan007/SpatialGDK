// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "Interop/Connection/SpatialOSWorkerInterface.h"

#include "Interop/ClaimPartitionHandler.h"
#include "Interop/CreateEntityHandler.h"

#include "SpatialCommonTypes.h"
#include "SpatialConstants.h"
#include "SpatialView/EntityView.h"
#include "SpatialView/OpList/ExtractedOpList.h"
#include "SpatialView/OpList/OpList.h"
#include "SpatialView/ViewCoordinator.h"

#include "SpatialWorkerConnection.generated.h"

class USpatialNetDriver;
class USpatialWorkerConnection;

namespace SpatialGDK
{
class ServerWorkerEntityCreator
{
public:
	ServerWorkerEntityCreator(USpatialNetDriver& InNetDriver, USpatialWorkerConnection& InConnection);
	void CreateWorkerEntity();
	void ProcessOps(const TArray<Worker_Op>& Ops);

private:
	void OnEntityCreated(const Worker_CreateEntityResponseOp& Op);
	enum class WorkerSystemEntityCreatorState
	{
		CreatingWorkerSystemEntity,
		ClaimingWorkerPartition,
	};
	WorkerSystemEntityCreatorState State;

	USpatialNetDriver& NetDriver;
	USpatialWorkerConnection& Connection;

	CreateEntityHandler CreateEntityHandler;
	ClaimPartitionHandler ClaimPartitionHandler;
};
} // namespace SpatialGDK

DECLARE_LOG_CATEGORY_EXTERN(LogSpatialWorkerConnection, Log, All);

UCLASS()
class SPATIALGDK_API USpatialWorkerConnection : public UObject, public SpatialOSWorkerInterface
{
	GENERATED_BODY()

public:
	void SetConnection(Worker_Connection* WorkerConnectionIn, TSharedPtr<SpatialGDK::SpatialEventTracer> EventTracer,
					   SpatialGDK::FComponentSetData ComponentSetData);
	void DestroyConnection();

	// UObject interface.
	virtual void FinishDestroy() override;

	// Worker Connection Interface
	virtual const TArray<SpatialGDK::EntityDelta>& GetEntityDeltas() override;
	virtual const TArray<Worker_Op>& GetWorkerMessages() override;

	virtual Worker_RequestId SendReserveEntityIdsRequest(uint32_t NumOfEntities, const SpatialGDK::FRetryData& RetryData) override;
	virtual Worker_RequestId SendCreateEntityRequest(TArray<FWorkerComponentData> Components, const Worker_EntityId* EntityId,
													 const SpatialGDK::FRetryData& RetryData,
													 const FSpatialGDKSpanId& SpanId = {}) override;
	virtual Worker_RequestId SendDeleteEntityRequest(Worker_EntityId EntityId, const SpatialGDK::FRetryData& RetryData,
													 const FSpatialGDKSpanId& SpanId = {}) override;
	virtual void SendAddComponent(Worker_EntityId EntityId, FWorkerComponentData* ComponentData,
								  const FSpatialGDKSpanId& SpanId = {}) override;
	virtual void SendRemoveComponent(Worker_EntityId EntityId, Worker_ComponentId ComponentId,
									 const FSpatialGDKSpanId& SpanId = {}) override;
	virtual void SendComponentUpdate(Worker_EntityId EntityId, FWorkerComponentUpdate* ComponentUpdate,
									 const FSpatialGDKSpanId& SpanId = {}) override;
	virtual Worker_RequestId SendCommandRequest(Worker_EntityId EntityId, Worker_CommandRequest* Request,
												const SpatialGDK::FRetryData& RetryData, const FSpatialGDKSpanId& SpanId) override;
	virtual void SendCommandResponse(Worker_RequestId RequestId, Worker_CommandResponse* Response,
									 const FSpatialGDKSpanId& SpanId = {}) override;
	virtual void SendCommandFailure(Worker_RequestId RequestId, const FString& Message, const FSpatialGDKSpanId& SpanId = {}) override;
	virtual void SendLogMessage(uint8_t Level, const FName& LoggerName, const TCHAR* Message) override;
	virtual Worker_RequestId SendEntityQueryRequest(const Worker_EntityQuery* EntityQuery,
													const SpatialGDK::FRetryData& RetryData) override;
	virtual void SendMetrics(SpatialGDK::SpatialMetrics Metrics) override;

	void CreateServerWorkerEntity();

	void Advance(float DeltaTimeS);
	bool HasDisconnected() const;
	Worker_ConnectionStatusCode GetConnectionStatus() const;
	FString GetDisconnectReason() const;

	const SpatialGDK::EntityView& GetView() const;
	SpatialGDK::ViewCoordinator& GetCoordinator() const;
	// TODO: UNR-5481 - Fix this hack for fixing spatial debugger crash after client travel
	bool HasValidCoordinator() const { return Coordinator.IsValid(); }

	PhysicalWorkerName GetWorkerId() const;
	Worker_EntityId GetWorkerSystemEntityId() const;

	SpatialGDK::CallbackId RegisterComponentAddedCallback(Worker_ComponentId ComponentId, SpatialGDK::FComponentValueCallback Callback);
	SpatialGDK::CallbackId RegisterComponentRemovedCallback(Worker_ComponentId ComponentId, SpatialGDK::FComponentValueCallback Callback);
	SpatialGDK::CallbackId RegisterComponentValueCallback(Worker_ComponentId ComponentId, SpatialGDK::FComponentValueCallback Callback);
	SpatialGDK::CallbackId RegisterAuthorityGainedCallback(Worker_ComponentId ComponentId, SpatialGDK::FEntityCallback Callback);
	SpatialGDK::CallbackId RegisterAuthorityLostCallback(Worker_ComponentId ComponentId, SpatialGDK::FEntityCallback Callback);
	SpatialGDK::CallbackId RegisterAuthorityLostTempCallback(Worker_ComponentId ComponentId, SpatialGDK::FEntityCallback Callback);
	void RemoveCallback(SpatialGDK::CallbackId Id);

	void Flush();

	void SetStartupComplete();

	SpatialGDK::ISpatialOSWorker* GetSpatialWorkerInterface() const;
	SpatialGDK::SpatialEventTracer* GetEventTracer() const { return EventTracer; }

private:
	TOptional<SpatialGDK::ServerWorkerEntityCreator> WorkerEntityCreator;

	static bool IsStartupComponent(Worker_ComponentId Id);
	static void ExtractStartupOps(SpatialGDK::OpList& OpList, SpatialGDK::ExtractedOpListData& ExtractedOpList);
	bool StartupComplete = false;
	SpatialGDK::SpatialEventTracer* EventTracer;
	TUniquePtr<SpatialGDK::ViewCoordinator> Coordinator;
};
