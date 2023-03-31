#pragma once

#include "CoreMinimal.h"
#include "Commandlets/CookCommandlet.h"
#include "CookTestSchemaCommandlet.generated.h"



UCLASS(config = Editor)
class UCookTestSchemaCommandlet : public UCookCommandlet
{
	
	GENERATED_UCLASS_BODY()


public:
	virtual int32 Main(const FString& CmdLineParams) override;
};
