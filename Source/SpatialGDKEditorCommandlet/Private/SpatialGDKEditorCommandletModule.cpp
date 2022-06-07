// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "SpatialGDKEditorCommandletModule.h"

// clang-format off
#include "SpatialConstants.h"
#include "SpatialConstants.cxx"
// clang-format on

#define LOCTEXT_NAMESPACE "FSpatialGDKEditorCommandletModule"

DEFINE_LOG_CATEGORY(LogSpatialGDKEditorCommandlet);

IMPLEMENT_MODULE(FSpatialGDKEditorCommandletModule, SpatialGDKEditorCommandlet);

void FSpatialGDKEditorCommandletModule::StartupModule() {}
void FSpatialGDKEditorCommandletModule::ShutdownModule() {}

#undef LOCTEXT_NAMESPACE
