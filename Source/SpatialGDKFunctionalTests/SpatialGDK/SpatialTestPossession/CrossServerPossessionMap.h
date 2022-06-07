// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "TestMaps/GeneratedTestMap.h"
#include "CrossServerPossessionMap.generated.h"

/**
 * Generated test maps for Cross Server Possession tests.
 * Each test required its own map because the tests don't function
 * correctly when run after another test in the same map (UNR-5121).
 */

UCLASS()
class SPATIALGDKFUNCTIONALTESTS_API UCrossServerPossessionMap : public UGeneratedTestMap
{
	GENERATED_BODY()

public:
	UCrossServerPossessionMap();

protected:
	virtual void CreateCustomContentForMap() override;
};

UCLASS()
class SPATIALGDKFUNCTIONALTESTS_API UCrossServerMultiPossessionMap : public UGeneratedTestMap
{
	GENERATED_BODY()

public:
	UCrossServerMultiPossessionMap();

protected:
	virtual void CreateCustomContentForMap() override;
};
