// EOSUnifiedFriendsManager.h — Drop-in replacement (2025-09-14)
#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <functional>
#include <cstdint>

#include <eos_sdk.h>
#include <eos_friends.h>
#include <eos_ui.h>
#include <eos_connect.h>
#include <eos_userinfo.h>
#include <eos_presence.h>

/**
 * EOSUnifiedFriendsManager — lightweight EOS Friends/Presence orchestrator.
 * - No UObject dependencies (usable from subsystem or CLI harness).
 * - Safe notification registration/removal.
 * - Name, presence, and PUID mapping enrichment after the base friends query.
 * - Provides a simple callback for UI to consume a human-readable list.
 */
class EOSUnifiedFriendsManager
{
public:
	struct FriendEntry
	{
		EOS_EpicAccountId    EpicId        = nullptr;
		EOS_ProductUserId    ProductId     = nullptr;
		std::string          EpicIdStr;         // canonical string
		std::string          DisplayName;       // from UserInfo
		EOS_EFriendsStatus   Status        = EOS_EFriendsStatus::EOS_FS_NotFriends;
		EOS_Presence_EStatus Presence      = EOS_Presence_EStatus::EOS_PS_Offline;
		bool                 HasUserInfo   = false;
		bool                 HasPresence   = false;
		bool                 MappingRequested = false;
	};

	EOSUnifiedFriendsManager();
	~EOSUnifiedFriendsManager();

	// === Lifecycle (kept as-is so UE subsystem can call this exactly like your CLI) ===
	void Initialize(EOS_HPlatform platform, EOS_EpicAccountId epicId, EOS_ProductUserId productId);
	void Shutdown(); // removes notifies; called in dtor

	// === High-level ops ===
	void QueryFriends();
	void ShowOverlay();

	// === Friend lifecycle helpers ===
	void SendInvite(EOS_EpicAccountId target);
	void AcceptInvite(EOS_EpicAccountId target);
	void RejectInvite(EOS_EpicAccountId target);

	// === Legacy/CLI UI hook (subsystem can subscribe and rebroadcast to BP) ===
	std::function<void(const std::vector<std::string>&)> OnFriendsListUpdated;

	// === Rich access for subsystem/UI ===
	const std::vector<FriendEntry>& GetFriends() const { return OrderedFriends; }

	// No-op (kept for parity with other managers)
	void Tick() {}

private:
	// --- state
	EOS_HPlatform      Platform        = nullptr;
	EOS_EpicAccountId  LocalEpicId     = nullptr;
	EOS_ProductUserId  LocalProductId  = nullptr;

	EOS_NotificationId FriendsNotifyId  = EOS_INVALID_NOTIFICATIONID;
	EOS_NotificationId PresenceNotifyId = EOS_INVALID_NOTIFICATIONID;

	std::unordered_map<std::string, FriendEntry> FriendsByEpic; // key: EpicIdStr
	std::vector<FriendEntry>                     OrderedFriends;

	bool   bInitialFriendQueryFinished = false;
	double LastMappingQuerySeconds     = 0.0;

	// --- helpers
	static std::string EpicIdToString(EOS_EpicAccountId id);
	static const char* ResultToStr(EOS_EResult r);

	void RebuildOrdered();
	void EmitUpdated();

	void QueryNamesForUnknown();
	void QueryPresenceForFriends();
	void QueryMissingPUIDMappings(bool throttle = true);

	// --- callbacks (static trampolines) ---
	static void EOS_CALL OnQueryFriendsComplete(const EOS_Friends_QueryFriendsCallbackInfo* Info);
	static void EOS_CALL OnSendInviteComplete(const EOS_Friends_SendInviteCallbackInfo* Info);
	static void EOS_CALL OnAcceptInviteComplete(const EOS_Friends_AcceptInviteCallbackInfo* Info);
	static void EOS_CALL OnRejectInviteComplete(const EOS_Friends_RejectInviteCallbackInfo* Info);
	static void EOS_CALL OnFriendsUpdate(const EOS_Friends_OnFriendsUpdateInfo* Info);

	static void EOS_CALL OnShowOverlayComplete(const EOS_UI_ShowFriendsCallbackInfo* Info);

	static void EOS_CALL OnQueryUserInfoComplete(const EOS_UserInfo_QueryUserInfoCallbackInfo* Info);
	static void EOS_CALL OnPresenceChanged(const EOS_Presence_PresenceChangedCallbackInfo* Info);
	static void EOS_CALL OnQueryPresenceComplete(const EOS_Presence_QueryPresenceCallbackInfo* Info);

	static void EOS_CALL OnQueryExternalMappingsComplete(const EOS_Connect_QueryExternalAccountMappingsCallbackInfo* Info);

	// --- internal handlers invoked from callbacks ---
	void HandleQueryFriendsComplete(const EOS_Friends_QueryFriendsCallbackInfo* Info);
	void HandleFriendsDelta(EOS_EpicAccountId targetEpic, EOS_EFriendsStatus newStatus, EOS_EpicAccountId localEpic);
	void HandleUserInfoReady(EOS_EpicAccountId targetEpic);
	void HandlePresenceReady(EOS_EpicAccountId targetEpic);

	void EnsureNotifies();
	void RemoveNotifies();

	// querying helpers
	void BeginQueryUserInfo(EOS_EpicAccountId targetEpic);
	void BeginQueryPresence(EOS_EpicAccountId targetEpic);
	void BeginQueryExternalMappings(const std::vector<EOS_EpicAccountId>& epics);
};
