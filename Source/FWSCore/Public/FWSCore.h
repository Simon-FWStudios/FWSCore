// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogEOSUnifiedAuth, Verbose, All);
DECLARE_LOG_CATEGORY_EXTERN(LogEOSUnified, Verbose, All);
DECLARE_LOG_CATEGORY_EXTERN(LogEOSUnifiedFriends, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogEOSUnifiedLobby, Verbose, All);
DECLARE_LOG_CATEGORY_EXTERN(LogUnified, Verbose, All);
DECLARE_LOG_CATEGORY_EXTERN(LogSaveSystem, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogUIManager, Verbose, All);

DECLARE_LOG_CATEGORY_EXTERN(LogFWSCore, Log, All);

class FFWSCoreModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
