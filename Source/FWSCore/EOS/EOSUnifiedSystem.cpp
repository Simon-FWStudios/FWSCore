#include "EOSUnifiedSystem.h"
#include "FWSCore.h"
#include "Misc/ScopeExit.h"

EOSUnifiedSystem::EOSUnifiedSystem()
{
	// Wire auth signal so we can lazily create managers after a successful Connect login.
	AuthManager.OnLoginStateChanged = [this](bool bLoggedIn, const std::string& msg)
	{
		UE_LOG(LogEOSUnified, Log, TEXT("[System] LoginStateChanged: %s — %s"),
			bLoggedIn ? TEXT("LOGGED IN") : TEXT("LOGGED OUT"),
			*FString(msg.c_str()));

		if (bLoggedIn)
		{
			EOS_HPlatform      Platform = AuthManager.GetPlatformHandle();
			EOS_EpicAccountId  EpicId   = AuthManager.GetUserId();
			EOS_ProductUserId  PUID     = AuthManager.GetProductUserId();

			// Only create when we truly have Connect (PUID) & platform.
			if (Platform && PUID)
			{
				CreateManagers(Platform, EpicId, PUID);
			}
			else
			{
				UE_LOG(LogEOSUnified, Verbose, TEXT("[System] Deferring manager creation (Platform=%p, PUID=%p)"),
					Platform, PUID);
			}
		}
		else
		{
			// On any logout/hard-logout, tear down managers.
			DestroyManagers();
		}
	};
}

EOSUnifiedSystem::~EOSUnifiedSystem()
{
	Shutdown();
}

bool EOSUnifiedSystem::Initialize()
{
	UE_LOG(LogEOSUnified, Log, TEXT("[System] Initialize()"));
	const bool ok = AuthManager.Initialize();
	if (!ok)
	{
		UE_LOG(LogEOSUnified, Error, TEXT("[System] AuthManager.Initialize failed."));
	}
	return ok;
}

void EOSUnifiedSystem::Shutdown()
{
	UE_LOG(LogEOSUnified, Log, TEXT("[System] Shutdown()"));
	DestroyManagers();
	AuthManager.Shutdown();
}

void EOSUnifiedSystem::Tick()
{
	AuthManager.Tick();
	if (FriendsManager) FriendsManager->Tick();
	if (LobbyManager)   LobbyManager->Tick();
}

void EOSUnifiedSystem::CreateManagers(EOS_HPlatform platform, EOS_EpicAccountId epicId, EOS_ProductUserId prodId)
{
	// Idempotent: destroy any existing managers first (prevents leaks across PIE runs)
	DestroyManagers();

	UE_LOG(LogEOSUnified, Log, TEXT("[System] CreateManagers(Platform=%p)"), platform);

	// Friends
	FriendsManager = new EOSUnifiedFriendsManager();
	FriendsManager->Initialize(platform, epicId, prodId);
	FriendsManager->OnFriendsListUpdated = [](const std::vector<std::string>& list)
	{
		UE_LOG(LogEOSUnifiedFriends, Verbose, TEXT("[Friends] Updated: %d entries"), list.size());
	};

	// Lobby
	LobbyManager = new EOSUnifiedLobbyManager(&AuthManager, FriendsManager);
	LobbyManager->Initialize(platform, prodId);
}

void EOSUnifiedSystem::DestroyManagers()
{
	if (!FriendsManager && !LobbyManager) return;

	UE_LOG(LogEOSUnified, Log, TEXT("[System] DestroyManagers()"));

	if (LobbyManager)
	{
		LobbyManager->Shutdown();
		delete LobbyManager;
		LobbyManager = nullptr;
	}

	if (FriendsManager)
	{
		FriendsManager->Shutdown();
		delete FriendsManager;
		FriendsManager = nullptr;
	}
}
