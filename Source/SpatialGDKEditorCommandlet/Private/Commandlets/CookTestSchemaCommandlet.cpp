#include "CookTestSchemaCommandlet.h"
#include "Misc/CommandLine.h"

DECLARE_LOG_CATEGORY_EXTERN(LogCookTestSchemaCommandlet, Log, All);

DEFINE_LOG_CATEGORY(LogCookTestSchemaCommandlet);

UCookTestSchemaCommandlet::UCookTestSchemaCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	LogToConsole = true;
}
int32 UCookTestSchemaCommandlet::Main(const FString& CmdLineParams)
{
	UE_LOG(LogCookTestSchemaCommandlet, Display, TEXT("UCookTestSchemaCommandlet Cook and Generate Schema Started."));
	return 0;
}
