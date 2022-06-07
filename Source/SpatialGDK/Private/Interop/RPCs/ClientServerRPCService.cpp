// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "Interop/RPCs/ClientServerRPCService.h"

#include "EngineClasses/SpatialPackageMapClient.h"
#include "Schema/ClientEndpoint.h"
#include "Schema/ServerEndpoint.h"
#include "SpatialConstants.h"
#include "SpatialView/EntityComponentTypes.h"

DEFINE_LOG_CATEGORY(LogClientServerRPCService);

namespace SpatialGDK
{
ClientServerRPCService::ClientServerRPCService(const ActorCanExtractRPCDelegate InCanExtractRPCDelegate,
											   const ExtractRPCDelegate InExtractRPCCallback, const FSubView& InSubView,
											   FRPCStore& InRPCStore)
	: CanExtractRPCDelegate(InCanExtractRPCDelegate)
	, ExtractRPCCallback(InExtractRPCCallback)
	, SubView(&InSubView)
	, RPCStore(&InRPCStore)
{
}

void ClientServerRPCService::AdvanceView()
{
	const FSubViewDelta& SubViewDelta = SubView->GetViewDelta();
	for (const EntityDelta& Delta : SubViewDelta.EntityDeltas)
	{
		switch (Delta.Type)
		{
		case EntityDelta::UPDATE:
		{
			for (const ComponentChange& Change : Delta.ComponentUpdates)
			{
				if (IsClientOrServerEndpoint(Change.ComponentId))
				{
					ApplyComponentUpdate(Delta.EntityId, Change.ComponentId, Change.Update);
				}
			}
			break;
		}
		case EntityDelta::ADD:
			PopulateDataStore(Delta.EntityId);
			SetEntityData(Delta.EntityId);
			break;
		case EntityDelta::REMOVE:
			ClientServerDataStore.Remove(Delta.EntityId);
			break;
		case EntityDelta::TEMPORARILY_REMOVED:
			ClientServerDataStore.Remove(Delta.EntityId);
			PopulateDataStore(Delta.EntityId);
			SetEntityData(Delta.EntityId);
			break;
		default:
			break;
		}
	}
}

void ClientServerRPCService::ProcessChanges()
{
	const FSubViewDelta& SubViewDelta = SubView->GetViewDelta();
	for (const EntityDelta& Delta : SubViewDelta.EntityDeltas)
	{
		switch (Delta.Type)
		{
		case EntityDelta::UPDATE:
		{
			for (const ComponentChange& Change : Delta.ComponentUpdates)
			{
				ComponentUpdate(Delta.EntityId, Change.ComponentId, Change.Update);
			}
			break;
		}
		case EntityDelta::ADD:
			EntityAdded(Delta.EntityId);
			break;
		case EntityDelta::TEMPORARILY_REMOVED:
			EntityAdded(Delta.EntityId);
			break;
		default:
			break;
		}
	}
}

bool ClientServerRPCService::ContainsOverflowedRPC(const EntityRPCType& EntityRPC) const
{
	return OverflowedRPCs.Contains(EntityRPC);
}

TMap<EntityRPCType, TArray<PendingRPCPayload>>& ClientServerRPCService::GetOverflowedRPCs()
{
	return OverflowedRPCs;
}

void ClientServerRPCService::AddOverflowedRPC(const EntityRPCType EntityType, PendingRPCPayload&& Payload)
{
	OverflowedRPCs.FindOrAdd(EntityType).Add(MoveTemp(Payload));
}

void ClientServerRPCService::IncrementAckedRPCID(const Worker_EntityId EntityId, const ERPCType Type)
{
	const EntityRPCType EntityTypePair = EntityRPCType(EntityId, Type);
	uint64* LastAckedRPCId = LastAckedRPCIds.Find(EntityTypePair);
	if (LastAckedRPCId == nullptr)
	{
		UE_LOG(LogClientServerRPCService, Warning,
			   TEXT("ClientServerRPCService::IncrementAckedRPCID: Could not find last acked RPC id. Entity: %lld, RPC type: %s"), EntityId,
			   *SpatialConstants::RPCTypeToString(Type));
		return;
	}

	++(*LastAckedRPCId);

	const EntityComponentId EntityComponentPair = { EntityId, RPCRingBufferUtils::GetAckComponentId(Type) };
	Schema_Object* EndpointObject = Schema_GetComponentUpdateFields(RPCStore->GetOrCreateComponentUpdate(EntityComponentPair));

	RPCRingBufferUtils::WriteAckToSchema(EndpointObject, Type, *LastAckedRPCId);
}

uint64 ClientServerRPCService::GetAckFromView(const Worker_EntityId EntityId, const ERPCType Type)
{
	switch (Type)
	{
	case ERPCType::ServerAlwaysWrite:
		return ClientServerDataStore[EntityId].Server.AlwaysWriteRPCAck;
	default:
		checkNoEntry();
		return 0;
	}
}

void ClientServerRPCService::SetEntityData(Worker_EntityId EntityId)
{
	for (const Worker_ComponentId ComponentId : SubView->GetView()[EntityId].Authority)
	{
		OnEndpointAuthorityGained(EntityId, ComponentId);
	}
}

void ClientServerRPCService::EntityAdded(const Worker_EntityId EntityId)
{
	for (const Worker_ComponentId ComponentId : SubView->GetView()[EntityId].Authority)
	{
		ExtractRPCsForEntity(EntityId, ComponentId == SpatialConstants::CLIENT_AUTH_COMPONENT_SET_ID
										   ? SpatialConstants::SERVER_ENDPOINT_COMPONENT_ID
										   : SpatialConstants::CLIENT_ENDPOINT_COMPONENT_ID);
	}
}

void ClientServerRPCService::ComponentUpdate(const Worker_EntityId EntityId, const Worker_ComponentId ComponentId,
											 Schema_ComponentUpdate* Update)
{
	if (!IsClientOrServerEndpoint(ComponentId))
	{
		return;
	}
	HandleRPC(EntityId, ComponentId);
}

void ClientServerRPCService::PopulateDataStore(const Worker_EntityId EntityId)
{
	const EntityViewElement& Entity = SubView->GetView()[EntityId];
	const ClientEndpoint Client = ClientEndpoint(
		Entity.Components.FindByPredicate(ComponentIdEquality{ SpatialConstants::CLIENT_ENDPOINT_COMPONENT_ID })->GetUnderlying());
	const ServerEndpoint Server = ServerEndpoint(
		Entity.Components.FindByPredicate(ComponentIdEquality{ SpatialConstants::SERVER_ENDPOINT_COMPONENT_ID })->GetUnderlying());
	ClientServerDataStore.Emplace(EntityId, ClientServerEndpoints{ Client, Server });
}

void ClientServerRPCService::ApplyComponentUpdate(const Worker_EntityId EntityId, const Worker_ComponentId ComponentId,
												  Schema_ComponentUpdate* Update)
{
	switch (ComponentId)
	{
	case SpatialConstants::CLIENT_ENDPOINT_COMPONENT_ID:
		ClientServerDataStore[EntityId].Client.ApplyComponentUpdate(Update);
		break;
	case SpatialConstants::SERVER_ENDPOINT_COMPONENT_ID:
		ClientServerDataStore[EntityId].Server.ApplyComponentUpdate(Update);
		break;
	default:
		break;
	}
}

void ClientServerRPCService::OnEndpointAuthorityGained(const Worker_EntityId EntityId, const Worker_ComponentId ComponentId)
{
	switch (ComponentId)
	{
	case SpatialConstants::CLIENT_AUTH_COMPONENT_SET_ID:
	{
		const ClientEndpoint& Endpoint = ClientServerDataStore[EntityId].Client;
		RPCStore->LastSentRPCIds.Add(EntityRPCType(EntityId, ERPCType::ServerAlwaysWrite), Endpoint.AlwaysWriteRPCBuffer.LastSentRPCId);
		break;
	}
	case SpatialConstants::SERVER_AUTH_COMPONENT_SET_ID:
	{
		const ServerEndpoint& Endpoint = ClientServerDataStore[EntityId].Server;
		LastSeenRPCIds.Add(EntityRPCType(EntityId, ERPCType::ServerAlwaysWrite), Endpoint.AlwaysWriteRPCAck);
		LastAckedRPCIds.Add(EntityRPCType(EntityId, ERPCType::ServerAlwaysWrite), Endpoint.AlwaysWriteRPCAck);
		break;
	}
	default:
		// Removed checkNoEntry as part of UNR-5462 to unblock 0.13.0.
		break;
	}
}

void ClientServerRPCService::OnEndpointAuthorityLost(const Worker_EntityId EntityId, const Worker_ComponentId ComponentId)
{
	switch (ComponentId)
	{
	case SpatialConstants::CLIENT_AUTH_COMPONENT_SET_ID:
	{
		RPCStore->LastSentRPCIds.Remove(EntityRPCType(EntityId, ERPCType::ServerAlwaysWrite));
		ClearOverflowedRPCs(EntityId);
		break;
	}
	case SpatialConstants::SERVER_AUTH_COMPONENT_SET_ID:
	{
		LastAckedRPCIds.Remove(EntityRPCType(EntityId, ERPCType::ServerAlwaysWrite));
		ClearOverflowedRPCs(EntityId);
		break;
	}
	default:
		checkNoEntry();
		break;
	}
}

void ClientServerRPCService::ClearOverflowedRPCs(const Worker_EntityId EntityId)
{
	for (uint8 RPCType = static_cast<uint8>(ERPCType::ClientReliable); RPCType <= static_cast<uint8>(ERPCType::NetMulticast); RPCType++)
	{
		OverflowedRPCs.Remove(EntityRPCType(EntityId, static_cast<ERPCType>(RPCType)));
	}
}

void ClientServerRPCService::HandleRPC(const Worker_EntityId EntityId, const Worker_ComponentId ComponentId)
{
	// When migrating an Actor to another worker, we preemptively change the role to SimulatedProxy when updating authority intent.
	// This can happen while this worker still has ServerEndpoint authority, and attempting to process a server RPC causes the engine
	// to print errors if the role isn't Authority. Instead, we exit here, and the RPC will be processed by the server that receives
	// authority.
	const bool bIsServerRpc = ComponentId == SpatialConstants::CLIENT_ENDPOINT_COMPONENT_ID;
	if (bIsServerRpc && SubView->HasAuthority(EntityId, SpatialConstants::SERVER_AUTH_COMPONENT_SET_ID))
	{
		if (!CanExtractRPCDelegate.Execute(EntityId))
		{
			return;
		}
	}
	ExtractRPCsForEntity(EntityId, ComponentId);
}

void ClientServerRPCService::ExtractRPCsForEntity(const Worker_EntityId EntityId, const Worker_ComponentId ComponentId)
{
	switch (ComponentId)
	{
	case SpatialConstants::CLIENT_ENDPOINT_COMPONENT_ID:
		ExtractRPCsForType(EntityId, ERPCType::ServerAlwaysWrite);
		break;
	case SpatialConstants::SERVER_ENDPOINT_COMPONENT_ID:
		break;
	default:
		checkNoEntry();
		break;
	}
}

void ClientServerRPCService::ExtractRPCsForType(const Worker_EntityId EntityId, const ERPCType Type)
{
	const EntityRPCType EntityTypePair = EntityRPCType(EntityId, Type);

	if (!LastSeenRPCIds.Contains(EntityTypePair))
	{
		UE_LOG(LogClientServerRPCService, Warning,
			   TEXT("Tried to extract RPCs but no entry in Last Seen Map! This can happen after server travel. Entity: %lld, type: %s"),
			   EntityId, *SpatialConstants::RPCTypeToString(Type));
		return;
	}
	const uint64 LastSeenRPCId = LastSeenRPCIds[EntityTypePair];

	const RPCRingBuffer& Buffer = GetBufferFromView(EntityId, Type);

	uint64 LastProcessedRPCId = LastSeenRPCId;
	if (Buffer.LastSentRPCId >= LastSeenRPCId)
	{
		uint64 FirstRPCIdToRead = LastSeenRPCId + 1;

		const uint32 BufferSize = RPCRingBufferUtils::GetRingBufferSize(Type);
		if (Buffer.LastSentRPCId > LastSeenRPCId + BufferSize)
		{
			if (!RPCRingBufferUtils::ShouldIgnoreCapacity(Type))
			{
				UE_LOG(LogClientServerRPCService, Warning,
					   TEXT("ClientServerRPCService::ExtractRPCsForType: RPCs were overwritten without being processed! Entity: %lld, RPC "
							"type: %s, "
							"last seen RPC ID: %d, last sent ID: %d, buffer size: %d"),
					   EntityId, *SpatialConstants::RPCTypeToString(Type), LastSeenRPCId, Buffer.LastSentRPCId, BufferSize);
			}
			FirstRPCIdToRead = Buffer.LastSentRPCId - BufferSize + 1;
		}

		for (uint64 RPCId = FirstRPCIdToRead; RPCId <= Buffer.LastSentRPCId; RPCId++)
		{
			const TOptional<RPCPayload>& Element = Buffer.GetRingBufferElement(RPCId);
			if (Element.IsSet())
			{
				ExtractRPCCallback.Execute(FUnrealObjectRef(EntityId, Element.GetValue().Offset), RPCSender(), Element.GetValue(), RPCId);
				LastProcessedRPCId = RPCId;
			}
			else
			{
				UE_LOG(
					LogClientServerRPCService, Warning,
					TEXT("ClientServerRPCService::ExtractRPCsForType: Ring buffer element empty. Entity: %lld, RPC type: %s, empty element "
						 "RPC id: %d"),
					EntityId, *SpatialConstants::RPCTypeToString(Type), RPCId);
			}
		}
	}
	else
	{
		UE_LOG(
			LogClientServerRPCService, Warning,
			TEXT("ClientServerRPCService::ExtractRPCsForType: Last sent RPC has smaller ID than last seen RPC. Entity: %lld, RPC type: %s, "
				 "last sent ID: %d, last seen ID: %d"),
			EntityId, *SpatialConstants::RPCTypeToString(Type), Buffer.LastSentRPCId, LastSeenRPCId);
	}

	if (LastProcessedRPCId > LastSeenRPCId)
	{
		LastSeenRPCIds[EntityTypePair] = LastProcessedRPCId;
	}
}

const RPCRingBuffer& ClientServerRPCService::GetBufferFromView(const Worker_EntityId EntityId, const ERPCType Type)
{
	switch (Type)
	{
	case ERPCType::ServerAlwaysWrite:
		return ClientServerDataStore[EntityId].Client.AlwaysWriteRPCBuffer;
	default:
		checkNoEntry();
		static const RPCRingBuffer DummyBuffer(ERPCType::Invalid);
		return DummyBuffer;
	}
}

bool ClientServerRPCService::IsClientOrServerEndpoint(const Worker_ComponentId ComponentId)
{
	return ComponentId == SpatialConstants::CLIENT_ENDPOINT_COMPONENT_ID || ComponentId == SpatialConstants::SERVER_ENDPOINT_COMPONENT_ID;
}
} // namespace SpatialGDK
