// EOSUnifiedLobbyManager.h — UE5.6/EOS SDK aligned (no AcceptInvite* types; correct notify/join types)
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include <eos_common.h>
#include <eos_sdk.h>
#include <eos_lobby.h>
#include <eos_lobby_types.h>  // for *_CallbackInfo structs and option structs
#include <eos_ui.h>
#include <eos_ui_types.h>     // for EOS_UI_AcknowledgeEventIdOptions

class EOSUnifiedAuthManager;
class EOSUnifiedFriendsManager;

// UI/debug summary of a lobby
struct FEOSLobbySummary
{
	std::string LobbyId;
	std::string OwnerPUID;
	std::string Name;
	std::string Map;
	std::string Mode;
	uint32_t    MaxMembers  = 0;
	uint32_t    MemberCount = 0;
	bool        bPresenceEnabled = false;
	bool        bAllowInvites    = true;
};

class EOSUnifiedLobbyManager
{
public:
	EOSUnifiedLobbyManager(EOSUnifiedAuthManager* authMgr, EOSUnifiedFriendsManager* friendsMgr);
	~EOSUnifiedLobbyManager();

	// ---- Lifecycle ----
	void Initialize(EOS_HPlatform platform, EOS_ProductUserId localProductUserId);
	bool IsLoggedIn() const { return Platform != nullptr && LocalPUID != nullptr; }
	void Shutdown();

	// ---- Operations ----
	void CreateLobby(std::atomic<bool>* finishedFlag = nullptr);
	void LeaveLobby(std::atomic<bool>* finishedFlag = nullptr);
	void DestroyLobby(std::atomic<bool>* finishedFlag = nullptr);

	// Searches:
	void SearchLobbies(std::atomic<bool>* finishedFlag = nullptr);        // presence-enabled
	void SearchAllLobbies(std::atomic<bool>* finishedFlag = nullptr);     // no filters
	void SearchLobbiesByName(std::atomic<bool>* finishedFlag = nullptr);  // name == "DefaultLobby"

	struct FSearchFilter { std::string Key; std::string Value; EOS_EComparisonOp Op = EOS_EComparisonOp::EOS_CO_EQUAL; };
	void SearchWithFilters(const std::vector<FSearchFilter>& filters, uint32_t maxResults, std::atomic<bool>* finishedFlag = nullptr);

	void JoinLobby(const std::string& lobbyId, std::atomic<bool>* finishedFlag = nullptr);

	// Invites
	void SendInviteTo(const std::string& targetProductUserId);

	// Accept: copy details by InviteId, then JoinLobby(with details)
	void AcceptInvite(const std::string& inviteId, std::atomic<bool>* finishedFlag = nullptr);

	// Reject: only if the symbol exists in this SDK
	void RejectInvite(const std::string& inviteId, std::atomic<bool>* finishedFlag = nullptr);

	// UI helpers
	void ShowInviteOverlay();      // opens Friends overlay (noop if EAID is unavailable)
	void ShowLeaveLobbyOverlay();  // noop (no dedicated EOS UI)

	// Mutations (owner)
	void ModifyCurrentLobby(const char* name, const char* map, const char* mode, int newMaxMembers = 0);

	// ---- Snapshot ----
	void GetCachedSummaries(std::vector<FEOSLobbySummary>& out) const;

	// ---- Events (to Subsystem/UI) ----
	std::function<void(const std::vector<FEOSLobbySummary>&)> OnSearchResultsUpdated;
	std::function<void(const FEOSLobbySummary&)>              OnJoinedLobby;
	std::function<void(const std::string&)>                   OnLeftLobby;
	std::function<void(const std::string&)>                   OnLobbyInviteReceivedEvent; // renamed to avoid clash
	std::function<void(const std::string&)>                   OnLobbyCreated;        // LobbyId
	std::function<void(EOS_EResult, const std::string&)>      OnLobbyCreateFailed;   // Result + message

	void Tick() {}

private:
	// State
	EOS_HPlatform     Platform   = nullptr;
	EOS_ProductUserId LocalPUID  = nullptr;

	EOS_HLobbyDetails CurrentLobbyDetails = nullptr;
	std::string       CurrentLobbyId;

	std::vector<FEOSLobbySummary> CachedSummaries;

	// Notifies
	EOS_NotificationId NotifyLobbyUpdateId       = EOS_INVALID_NOTIFICATIONID;
	EOS_NotificationId NotifyMemberUpdateId      = EOS_INVALID_NOTIFICATIONID;
	EOS_NotificationId NotifyInviteReceivedId    = EOS_INVALID_NOTIFICATIONID;
	EOS_NotificationId NotifyJoinLobbyAcceptedId = EOS_INVALID_NOTIFICATIONID;

	// Back-pointers (not owned)
	EOSUnifiedAuthManager*     AuthMgr    = nullptr;
	EOSUnifiedFriendsManager*  FriendsMgr = nullptr;

	// Helpers
	static FEOSLobbySummary MakeSummary(EOS_HLobbyDetails details);
	void StartSearch(std::function<void(EOS_HLobbySearch)> configure, std::atomic<bool>* finishedFlag);
	void FinishAndEmitSearch(EOS_HLobbySearch searchHandle, EOS_EResult rc, std::atomic<bool>* finishedFlag);

	void RegisterNotifies();
	void UnregisterNotifies();
	static void EOS_CALL OnShowFriendsComplete(const EOS_UI_ShowFriendsCallbackInfo* Info);
	static void EOS_CALL OnUpdateLobbyComplete(const EOS_Lobby_UpdateLobbyCallbackInfo* Info);

	// ----- Static callbacks (note: types that exist in this SDK) -----
	static void EOS_CALL OnCreateLobbyComplete(const EOS_Lobby_CreateLobbyCallbackInfo* Info);
	static void EOS_CALL OnDestroyLobbyComplete(const EOS_Lobby_DestroyLobbyCallbackInfo* Info);
	static void EOS_CALL OnLeaveLobbyComplete(const EOS_Lobby_LeaveLobbyCallbackInfo* Info);

	static void EOS_CALL OnJoinLobbyByIdComplete(const EOS_Lobby_JoinLobbyByIdCallbackInfo* Info); // JoinLobbyById
	static void EOS_CALL OnJoinLobbyComplete(const EOS_Lobby_JoinLobbyCallbackInfo* Info);         // JoinLobby (details)
	static void EOS_CALL OnJoinLobbyAccepted(const EOS_Lobby_JoinLobbyAcceptedCallbackInfo* Info);

	static void EOS_CALL OnSendInviteComplete(const EOS_Lobby_SendInviteCallbackInfo* Info);

	static void EOS_CALL OnSearchComplete(const EOS_LobbySearch_FindCallbackInfo* Info);

	static void EOS_CALL OnLobbyInviteReceivedCallback(const EOS_Lobby_LobbyInviteReceivedCallbackInfo* Info);
	static void EOS_CALL OnLobbyUpdateReceived(const EOS_Lobby_LobbyUpdateReceivedCallbackInfo* Info);
	static void EOS_CALL OnMemberUpdateReceived(const EOS_Lobby_LobbyMemberUpdateReceivedCallbackInfo* Info);
	
};
