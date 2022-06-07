// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "CrossServerRPCParams.h"
#include "EngineClasses/SpatialNetDriver.h"
#include "RPCExecutorInterface.h"

namespace SpatialGDK
{
class RPCExecutor : public RPCExecutorInterface
{
public:
	RPCExecutor(USpatialNetDriver* NetDriver, SpatialEventTracer* EventTracer = nullptr);

	virtual TOptional<FCrossServerRPCParams> TryRetrieveCrossServerRPCParams(const Worker_Op& Op) override;
	virtual bool ExecuteCommand(const FCrossServerRPCParams& Params) override;

private:
	USpatialNetDriver* NetDriver;
	SpatialEventTracer* EventTracer;
};

} // namespace SpatialGDK
