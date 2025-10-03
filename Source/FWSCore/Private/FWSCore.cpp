// Copyright Epic Games, Inc. All Rights Reserved.

#include "FWSCore.h"

#define LOCTEXT_NAMESPACE "FFWSCoreModule"

DEFINE_LOG_CATEGORY(LogEOSUnified);
DEFINE_LOG_CATEGORY(LogEOSUnifiedAuth);
DEFINE_LOG_CATEGORY(LogEOSUnifiedFriends);
DEFINE_LOG_CATEGORY(LogEOSUnifiedLobby);
DEFINE_LOG_CATEGORY(LogUnified);
DEFINE_LOG_CATEGORY(LogSaveSystem);
DEFINE_LOG_CATEGORY(LogUIManager);
DEFINE_LOG_CATEGORY(LogFWSCore);

void FFWSCoreModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
}

void FFWSCoreModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FFWSCoreModule, FWSCore)