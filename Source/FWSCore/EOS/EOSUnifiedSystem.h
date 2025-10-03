// EOSUnifiedSystem.h — Drop-in replacement (2025-09-14)
#pragma once

#include "EOSUnifiedAuthManager.h"
#include "EOSUnifiedFriendsManager.h"
#include "EOSUnifiedLobbyManager.h"

/**
 * Plain C++ owner of EOS managers + auth.
 * - Initializes Auth only.
 * - Managers are created AFTER login via CreateManagers().
 * - Subsystem calls Tick() every frame.
 */
class EOSUnifiedSystem
{
public:
	EOSUnifiedSystem();
	~EOSUnifiedSystem();

	// ---- Lifecycle ----
	bool Initialize();   // creates/adopts platform via AuthManager
	void Shutdown();     // releases created platform and destroys managers
	void Tick();         // pumps EOS callbacks

	// ---- Auth passthroughs ----
	void Login()                  { AuthManager.Login(); }
	void LoginViaPortal()         { AuthManager.LoginAccountPortal(); }
	void Logout()                 { AuthManager.Logout(); }
	void HardLogout()             { AuthManager.HardLogout(/*done*/nullptr); }

	// ---- Accessors ----
	EOSUnifiedAuthManager&       GetAuthManager()       { return AuthManager; }
	const EOSUnifiedAuthManager& GetAuthManager() const { return AuthManager; }

	EOSUnifiedFriendsManager*       GetFriendsManager()       { return FriendsManager; }
	const EOSUnifiedFriendsManager* GetFriendsManager() const { return FriendsManager; }

	EOSUnifiedLobbyManager*         GetLobbyManager()         { return LobbyManager; }
	const EOSUnifiedLobbyManager*   GetLobbyManager()   const { return LobbyManager; }

	// Convenience aliases (compat)
	EOS_EpicAccountId   GetEpicAccountId()   const { return AuthManager.GetUserId(); }
	EOS_ProductUserId   GetProductUserId()   const { return AuthManager.GetProductUserId(); }
	EOS_HPlatform       GetPlatformHandle()  const { return AuthManager.GetPlatformHandle(); }

	/** Re/create Friends & Lobby managers when platform + identities are ready. */
	void CreateManagers(EOS_HPlatform platform, EOS_EpicAccountId epicId, EOS_ProductUserId prodId);
	void DestroyManagers();

private:
	EOSUnifiedAuthManager      AuthManager;
	EOSUnifiedFriendsManager*  FriendsManager = nullptr;
	EOSUnifiedLobbyManager*    LobbyManager   = nullptr;
};
