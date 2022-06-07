// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "Schema/ServerEndpoint.h"

namespace SpatialGDK
{
ServerEndpoint::ServerEndpoint(Schema_ComponentData* Data)
	: ReliableRPCBuffer(ERPCType::ClientReliable)
	, UnreliableRPCBuffer(ERPCType::ClientUnreliable)
{
	ReadFromSchema(Schema_GetComponentDataFields(Data));
}

void ServerEndpoint::ApplyComponentUpdate(Schema_ComponentUpdate* Update)
{
	ReadFromSchema(Schema_GetComponentUpdateFields(Update));
}

void ServerEndpoint::ReadFromSchema(Schema_Object* SchemaObject)
{
	RPCRingBufferUtils::ReadBufferFromSchema(SchemaObject, ReliableRPCBuffer);
	RPCRingBufferUtils::ReadBufferFromSchema(SchemaObject, UnreliableRPCBuffer);
	RPCRingBufferUtils::ReadAckFromSchema(SchemaObject, ERPCType::ServerReliable, ReliableRPCAck);
	RPCRingBufferUtils::ReadAckFromSchema(SchemaObject, ERPCType::ServerUnreliable, UnreliableRPCAck);
	RPCRingBufferUtils::ReadAckFromSchema(SchemaObject, ERPCType::ServerAlwaysWrite, AlwaysWriteRPCAck);
}

} // namespace SpatialGDK
