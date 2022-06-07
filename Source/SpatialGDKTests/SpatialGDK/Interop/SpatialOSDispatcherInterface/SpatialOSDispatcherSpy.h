// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "Interop/SpatialOSDispatcherInterface.h"

// The SpatialOSDispatcherSpy is intended as a very minimal implementation which will acknowledge
// and record any calls. It can then be used to unit test other classes and validate that given
// particular inputs, they make calls to SpatialOS which are as expected.
//
// Currently, only a few methods below have implementations. Feel free to extend this mock as needed
// for testing purposes.

class SpatialOSDispatcherSpy : public SpatialOSDispatcherInterface
{
public:
	SpatialOSDispatcherSpy();
	virtual ~SpatialOSDispatcherSpy() {}

	// Dispatcher Calls
	virtual void OnCriticalSection(bool InCriticalSection) override;
	virtual void OnAddEntity(const Worker_AddEntityOp& Op) override;
	virtual void OnAddComponent(const Worker_AddComponentOp& Op) override;
	virtual void OnRemoveEntity(const Worker_RemoveEntityOp& Op) override;
	virtual void OnRemoveComponent(const Worker_RemoveComponentOp& Op) override;
	virtual void FlushRemoveComponentOps() override;
	virtual void DropQueuedRemoveComponentOpsForEntity(Worker_EntityId EntityId) override;
	virtual void OnAuthorityChange(const Worker_ComponentSetAuthorityChangeOp& Op) override;

	virtual void OnComponentUpdate(const Worker_ComponentUpdateOp& Op) override;

	// This gets bound to a delegate in SpatialRPCService and is called for each RPC extracted when calling
	// SpatialRPCService::ExtractRPCsForEntity.
	virtual bool OnExtractIncomingRPC(Worker_EntityId EntityId, ERPCType RPCType, const SpatialGDK::RPCPayload& Payload) override;

	virtual void AddPendingReliableRPC(Worker_RequestId RequestId, TSharedRef<struct FReliableRPCForRetry> ReliableRPC) override;
};
