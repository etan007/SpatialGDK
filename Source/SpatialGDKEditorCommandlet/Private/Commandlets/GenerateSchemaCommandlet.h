// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "Commandlets/Commandlet.h"

#include "GenerateSchemaCommandlet.generated.h"

UCLASS()
class UGenerateSchemaCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UGenerateSchemaCommandlet();

public:
	virtual int32 Main(const FString& Params) override;

protected:
	virtual bool HandleOptions(const TArray<FString>& Switches);
};
