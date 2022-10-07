// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "Interop/SpatialClassInfoManager.h"
#include "Schema/Interest.h"
#include "Utils/GDKPropertyMacros.h"
#include "Utils/RepDataUtils.h"

#include <WorkerSDK/improbable/c_schema.h>
#include <WorkerSDK/improbable/c_worker.h>

DECLARE_LOG_CATEGORY_EXTERN(LogComponentFactory, Log, All);

class USpatialNetDriver;
class USpatialPackageMap;
class USpatialClassInfoManager;
class USpatialLatencyTracer;
class USpatialPackageMapClient;

class UNetDriver;

enum EReplicatedPropertyGroup : uint32;

namespace SpatialGDK
{
class SPATIALGDK_API ComponentFactory
{
public:
	ComponentFactory(bool bInterestDirty, USpatialNetDriver* InNetDriver);

	TArray<FWorkerComponentData> CreateComponentDatas(UObject* Object, const FClassInfo& Info, const FRepChangeState& RepChangeState,
													  uint32& OutBytesWritten);
	TArray<FWorkerComponentUpdate> CreateComponentUpdates(UObject* Object, const FClassInfo& Info, Worker_EntityId EntityId,
														  const FRepChangeState* RepChangeState, uint32& OutBytesWritten);

	bool WasInitialOnlyDataWritten() const { return bInitialOnlyDataWritten; }

	static FWorkerComponentData CreateEmptyComponentData(Worker_ComponentId ComponentId);

private:
	FWorkerComponentData CreateComponentData(Worker_ComponentId ComponentId, UObject* Object, const FRepChangeState& Changes,
											 ESchemaComponentType PropertyGroup, uint32& OutBytesWritten);
	FWorkerComponentUpdate CreateComponentUpdate(Worker_ComponentId ComponentId, UObject* Object, const FRepChangeState& Changes,
												 ESchemaComponentType PropertyGroup, uint32& OutBytesWritten);

	uint32 FillSchemaObject(Schema_Object* ComponentObject, UObject* Object, const FRepChangeState& Changes,
							ESchemaComponentType PropertyGroup, bool bIsInitialData, TArray<Schema_FieldId>* ClearedIds = nullptr);

	void AddProperty(Schema_Object* Object, Schema_FieldId FieldId, GDK_PROPERTY(Property) * Property, const uint8* Data,
					 TArray<Schema_FieldId>* ClearedIds,bool is_repeated = false,int index = -1);

	USpatialNetDriver* NetDriver;
	USpatialPackageMapClient* PackageMap;
	USpatialClassInfoManager* ClassInfoManager;

	bool bInterestHasChanged;
	bool bInitialOnlyDataWritten;
	bool bInitialOnlyReplicationEnabled;
};

} // namespace SpatialGDK
