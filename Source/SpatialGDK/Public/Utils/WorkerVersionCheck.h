// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "WorkerSDK/improbable/c_worker.h"

#define WORKER_SDK_VERSION "15.1.0" // We currently download the 15.1.1 SDK, but its still marked as 15.1.0.

constexpr bool StringsEqual(char const* A, char const* B)
{
	return *A == *B && (*A == '\0' || StringsEqual(A + 1, B + 1));
}

// Check if the current version of the Worker SDK is compatible with the current version of UnrealGDK
// WORKER_SDK_VERSION is incremented here when breaking changes are made that make previous versions of the SDK
// incompatible
static_assert(StringsEqual("15.1.0", WORKER_SDK_VERSION),
			  "Worker SDK version is incompatible with the UnrealGDK version. Check both the Worker SDK and GDK are up to date");
