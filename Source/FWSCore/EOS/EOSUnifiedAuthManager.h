// EOSUnifiedAuthManager.h — Drop-in replacement (2025-09-14)
#pragma once

#include <atomic>
#include <functional>
#include <string>

#include "eos_sdk.h"
#include <eos_auth.h>
#include <eos_connect.h>
#include <eos_userinfo.h>

#include "SampleConstants.h"

// Forward declarations (to avoid circular includes)
class EOSUnifiedFriendsManager;
class EOSUnifiedLobbyManager;

/**
 * Auth + Connect orchestrator (engine-agnostic header).
 * - Handles EOS SDK/platform lifetime adoption or self-creation.
 * - Supports PersistentAuth, RefreshToken, and Account Portal flows.
 * - Exposes a lightweight Tick() for EOS_Platform_Tick.
 * - Thread-safe signal for login state changes.
 *
 * NOTE: Implementation intentionally keeps Unreal-specific logging/paths in the .cpp.
 */
class EOSUnifiedAuthManager
{
public:
	EOSUnifiedAuthManager();
	~EOSUnifiedAuthManager();

	// ----- Lifecycle -----
	// Loads token, creates/attaches to platform, but does NOT start login automatically.
	bool Initialize();
	// Unhooks/clears handles; releases platform only if we created it.
	void Shutdown();
	// Call each frame (or on a ticker) to pump callbacks.
	void Tick();

	// ----- Login / Logout -----
	// Attempts RefreshToken -> falls back to PersistentAuth if none.
	void Login();
	// Opens Epic Account Portal interactive login.
	void LoginAccountPortal();
	// Local/logout without server-side persistent token revoke.
	void Logout();
	// Fully revoke server-side token and clear local state.
	void HardLogout(std::atomic<bool>* done = nullptr);

	// ----- Signals -----
	// (Game thread safe if you bounce it there in your subsystem)
	std::function<void(bool /*bLoggedIn*/, const std::string& /*message*/)> OnLoginStateChanged;

	// ----- Accessors -----
	EOS_HPlatform     GetPlatformHandle() const { return PlatformHandle; }
	EOS_EpicAccountId GetUserId()        const { return UserId; }
	EOS_ProductUserId GetProductUserId() const { return ProductUserId; }
	FString GetCachedDisplayName() const { return CachedDisplayName; }

	std::function<void(const FString&)> OnAuthDisplayNameCached;

	void SetPlatformHandle(EOS_HPlatform InPlatform) { PlatformHandle = InPlatform; }
	bool IsAccountPortalActive() const { return bIsAccountPortalActive.load(); }

	// Optional: wire managers so auth can notify them (not required for basic auth)
	void SetManagerPointers(EOSUnifiedFriendsManager* InFriends, EOSUnifiedLobbyManager* InLobby)
	{
		FriendsManager = InFriends; LobbyManager = InLobby;
	}

	// Diagnostics convenience
	void LogCurrentUserDisplayName();

	// Exposed for explicit control (Initialize() calls this if needed)
	bool CreatePlatform();
	
	/** True if we have a valid logged-in local user. */
	bool IsLoggedIn() const;

	/** Local user index (0..n). Returns 0 if unknown. */
	int32 GetLocalUserNum() const;

	/** Epic Account ID as a string (stable per user). Empty if not logged in. */

	FString GetEpicAccountIdString() const;

	/** Deployment or Sandbox ID (for namespacing saves). Empty if unknown. */

	FString GetDeploymentOrSandboxId() const;

	// ===== (Optional) internal setter you call after login / user-info fetch =====
	void SetCachedDisplayName(const FString& InName);
	void SetCachedDeploymentOrSandbox(const FString& InNamespace);

private:


protected:
	void QueryLocalUserDisplayName();     // kicks off EOS_UserInfo query
	void ClearCachedDisplayName() { CachedDisplayName.Empty(); }


private:
	// ----- Internal helpers / callbacks -----
	void HandleLoginCallback(const EOS_Auth_LoginCallbackInfo* Data);
	static void EOS_CALL LoginCompleteCallback(const EOS_Auth_LoginCallbackInfo* Data);

	void StartConnectLogin(const char* accessToken);

	void SaveRefreshToken();
	void LoadRefreshToken();
	void DeleteRefreshToken();

	void OnConnectLogoutComplete(const EOS_Connect_LogoutCallbackInfo* Data,
	                             std::atomic<bool>* done);
	void OnAuthLogoutComplete(const EOS_Auth_LogoutCallbackInfo* Data,
	                          std::atomic<bool>* done);
	void OnDeletePersistentAuthComplete(const EOS_Auth_DeletePersistentAuthCallbackInfo* Data,
	                                    std::atomic<bool>* done);

private:
	// ----- State -----
	std::string       RefreshToken;
	std::string       OwnedCacheDirAnsi;      // holds lifetime of CacheDirectory UTF-8 string

	EOS_EpicAccountId  UserId         = nullptr;
	EOS_ProductUserId  ProductUserId  = nullptr;
	FString			   CachedDisplayName;;
	EOS_HPlatform      PlatformHandle = nullptr;

	bool bCreatedPlatform = false;  // we created and must release
	bool bUsingEngineEOS  = false;  // adopted engine-owned platform (don't release)

	std::atomic<bool>  bIsAccountPortalActive{false};
	bool               bAuthLoginComplete    = false;
	bool               bConnectLoginComplete = false;

	// Portal flow state (kept for compatibility with existing code)
	EOS_EpicAccountId  PendingPortalEpicAccountId = nullptr;

	// Optional manager pointers
	EOSUnifiedFriendsManager* FriendsManager = nullptr;
	EOSUnifiedLobbyManager*   LobbyManager   = nullptr;

	bool bIsLoggedIn = false;
	int32 CachedLocalUserNum = 0;
	FString CachedDeploymentOrSandbox;  // "rd4cwxh3..." or "Prod"

};
