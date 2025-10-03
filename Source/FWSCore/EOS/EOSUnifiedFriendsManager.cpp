// EOSUnifiedFriendsManager.cpp — Drop-in replacement (2025-09-14)

#include "EOSUnifiedFriendsManager.h"
#include "FWSCore.h"
#include "FWSCore.h"
#include <algorithm>
#include <chrono>
#include <eos_common.h>
#include "SampleConstants.h"

// simple monotonic seconds helper
static double NowSeconds()
{
	using clock = std::chrono::steady_clock;
	static const auto t0 = clock::now();
	return std::chrono::duration<double>(clock::now() - t0).count();
}

static constexpr double kMinSecondsBetweenMappingQueries = 60.0;

EOSUnifiedFriendsManager::EOSUnifiedFriendsManager() {}
EOSUnifiedFriendsManager::~EOSUnifiedFriendsManager() { Shutdown(); }

void EOSUnifiedFriendsManager::Initialize(EOS_HPlatform platform, EOS_EpicAccountId epicId, EOS_ProductUserId productId)
{
	Platform       = platform;
	LocalEpicId    = epicId;
	LocalProductId = productId;

	FriendsByEpic.clear();
	OrderedFriends.clear();
	bInitialFriendQueryFinished = false;
	LastMappingQuerySeconds = 0.0;

	EnsureNotifies();
}

void EOSUnifiedFriendsManager::Shutdown()
{
	RemoveNotifies();
	FriendsByEpic.clear();
	OrderedFriends.clear();
	bInitialFriendQueryFinished = false;
}

void EOSUnifiedFriendsManager::QueryFriends()
{
	if (!Platform || !LocalEpicId)
	{
		UE_LOG(LogEOSUnifiedFriends, Warning, TEXT("[Friends] QueryFriends aborted: platform/local epic not set"));
		return;
	}

	EOS_HFriends friends = EOS_Platform_GetFriendsInterface(Platform);
	if (!friends)
	{
		UE_LOG(LogEOSUnifiedFriends, Error, TEXT("[Friends] GetFriendsInterface failed"));
		return;
	}

	EOS_Friends_QueryFriendsOptions opt{};
	opt.ApiVersion  = EOS_FRIENDS_QUERYFRIENDS_API_LATEST;
	opt.LocalUserId = LocalEpicId;

	char buf[EOS_EPICACCOUNTID_MAX_LENGTH + 1] = {};
	int32_t l = (int32_t)sizeof(buf);
	EOS_EpicAccountId_ToString(LocalEpicId, buf, &l);

	UE_LOG(LogEOSUnifiedFriends, Log, TEXT("[Friends] QueryFriends(LocalEpicId=%s)"), UTF8_TO_TCHAR(buf));

	EOS_Friends_QueryFriends(friends, &opt, this, &EOSUnifiedFriendsManager::OnQueryFriendsComplete);
}

void EOSUnifiedFriendsManager::ShowOverlay()
{
	if (!Platform || !LocalEpicId)
	{
		UE_LOG(LogEOSUnifiedFriends, Warning, TEXT("[Friends] ShowOverlay aborted: platform/local epic not set"));
		return;
	}

	EOS_HUI ui = EOS_Platform_GetUIInterface(Platform);
	if (!ui)
	{
		UE_LOG(LogEOSUnifiedFriends, Warning, TEXT("[Friends] No UI interface"));
		return;
	}

	EOS_UI_ShowFriendsOptions opt{};
	opt.ApiVersion  = EOS_UI_SHOWFRIENDS_API_LATEST;
	opt.LocalUserId = LocalEpicId;

	EOS_UI_ShowFriends(ui, &opt, this, &EOSUnifiedFriendsManager::OnShowOverlayComplete);
}

void EOSUnifiedFriendsManager::SendInvite(EOS_EpicAccountId target)
{
	if (!Platform || !LocalEpicId || !target) return;

	EOS_HFriends friends = EOS_Platform_GetFriendsInterface(Platform);
	if (!friends) return;

	EOS_Friends_SendInviteOptions opt{};
	opt.ApiVersion   = EOS_FRIENDS_SENDINVITE_API_LATEST;
	opt.LocalUserId  = LocalEpicId;
	opt.TargetUserId = target;

	EOS_Friends_SendInvite(friends, &opt, this, &EOSUnifiedFriendsManager::OnSendInviteComplete);
}

void EOSUnifiedFriendsManager::AcceptInvite(EOS_EpicAccountId target)
{
	if (!Platform || !LocalEpicId || !target) return;

	EOS_HFriends friends = EOS_Platform_GetFriendsInterface(Platform);
	if (!friends) return;

	EOS_Friends_AcceptInviteOptions opt{};
	opt.ApiVersion   = EOS_FRIENDS_ACCEPTINVITE_API_LATEST;
	opt.LocalUserId  = LocalEpicId;
	opt.TargetUserId = target;

	EOS_Friends_AcceptInvite(friends, &opt, this, &EOSUnifiedFriendsManager::OnAcceptInviteComplete);
}

void EOSUnifiedFriendsManager::RejectInvite(EOS_EpicAccountId target)
{
	if (!Platform || !LocalEpicId || !target) return;

	EOS_HFriends friends = EOS_Platform_GetFriendsInterface(Platform);
	if (!friends) return;

	EOS_Friends_RejectInviteOptions opt{};
	opt.ApiVersion   = EOS_FRIENDS_REJECTINVITE_API_LATEST;
	opt.LocalUserId  = LocalEpicId;
	opt.TargetUserId = target;

	EOS_Friends_RejectInvite(friends, &opt, this, &EOSUnifiedFriendsManager::OnRejectInviteComplete);
}

// ---- helpers ----
std::string EOSUnifiedFriendsManager::EpicIdToString(EOS_EpicAccountId id)
{
	if (!id) return {};
	char buf[EOS_EPICACCOUNTID_MAX_LENGTH + 1]{};
	int32_t inout = (int32_t)sizeof(buf);
	EOS_EResult r = EOS_EpicAccountId_ToString(id, buf, &inout);
	if (r != EOS_EResult::EOS_Success) return {};
	return std::string(buf);
}

const char* EOSUnifiedFriendsManager::ResultToStr(EOS_EResult r)
{
	return EOS_EResult_ToString(r);
}

void EOSUnifiedFriendsManager::RebuildOrdered()
{
	OrderedFriends.clear();
	OrderedFriends.reserve(FriendsByEpic.size());
	for (auto& kv : FriendsByEpic) OrderedFriends.push_back(kv.second);

	std::stable_sort(OrderedFriends.begin(), OrderedFriends.end(),
		[](const FriendEntry& a, const FriendEntry& b)
		{
			if (a.Status != b.Status) return (int)a.Status > (int)b.Status; // friends first
			return a.DisplayName < b.DisplayName;
		});
}

void EOSUnifiedFriendsManager::EmitUpdated()
{
	RebuildOrdered();

	if (OnFriendsListUpdated)
	{
		std::vector<std::string> out;
		out.reserve(OrderedFriends.size());
		for (auto& f : OrderedFriends)
		{
			std::string label = f.DisplayName.empty() ? f.EpicIdStr : f.DisplayName;
			label += " (" + f.EpicIdStr + ")";
			out.push_back(std::move(label));
		}
		OnFriendsListUpdated(out);
	}
}

void EOSUnifiedFriendsManager::QueryNamesForUnknown()
{
	for (auto& kv : FriendsByEpic)
	{
		auto& f = kv.second;
		if (!f.HasUserInfo && f.EpicId)
		{
			BeginQueryUserInfo(f.EpicId);
		}
	}
}

void EOSUnifiedFriendsManager::QueryPresenceForFriends()
{
	for (auto& kv : FriendsByEpic)
	{
		auto& f = kv.second;
		if (f.Status == EOS_EFriendsStatus::EOS_FS_Friends)
		{
			BeginQueryPresence(f.EpicId);
		}
	}
}

void EOSUnifiedFriendsManager::QueryMissingPUIDMappings(bool throttle)
{
	if (!LocalProductId) return;

	const double now = NowSeconds();
	if (throttle && (now - LastMappingQuerySeconds) < kMinSecondsBetweenMappingQueries)
	{
		return;
	}

	std::vector<EOS_EpicAccountId> epics;
	epics.reserve(FriendsByEpic.size());
	for (auto& kv : FriendsByEpic)
	{
		auto& f = kv.second;
		if (!f.ProductId && !f.MappingRequested && f.EpicId)
		{
			epics.push_back(f.EpicId);
			f.MappingRequested = true;
		}
	}

	if (!epics.empty())
	{
		BeginQueryExternalMappings(epics);
		LastMappingQuerySeconds = now;
	}
}

// ---- static callbacks ----
void EOS_CALL EOSUnifiedFriendsManager::OnQueryFriendsComplete(const EOS_Friends_QueryFriendsCallbackInfo* Info)
{
	if (!Info) return;
	auto* self = static_cast<EOSUnifiedFriendsManager*>(Info->ClientData);
	if (self) self->HandleQueryFriendsComplete(Info);
}

void EOS_CALL EOSUnifiedFriendsManager::OnSendInviteComplete(const EOS_Friends_SendInviteCallbackInfo* Info)
{
	if (!Info) return;
	auto* self = static_cast<EOSUnifiedFriendsManager*>(Info->ClientData);
	if (!self) return;

	if (Info->ResultCode == EOS_EResult::EOS_Success)
	{
		self->QueryFriends();
	}
	else
	{
		UE_LOG(LogEOSUnifiedFriends, Warning, TEXT("[Friends] SendInvite failed: %s"),
			UTF8_TO_TCHAR(ResultToStr(Info->ResultCode)));
	}
}

void EOS_CALL EOSUnifiedFriendsManager::OnAcceptInviteComplete(const EOS_Friends_AcceptInviteCallbackInfo* Info)
{
	if (!Info) return;
	auto* self = static_cast<EOSUnifiedFriendsManager*>(Info->ClientData);
	if (!self) return;

	if (Info->ResultCode == EOS_EResult::EOS_Success)
	{
		self->QueryFriends();
	}
	else
	{
		UE_LOG(LogEOSUnifiedFriends, Warning, TEXT("[Friends] AcceptInvite failed: %s"),
			UTF8_TO_TCHAR(ResultToStr(Info->ResultCode)));
	}
}

void EOS_CALL EOSUnifiedFriendsManager::OnRejectInviteComplete(const EOS_Friends_RejectInviteCallbackInfo* Info)
{
	if (!Info) return;
	auto* self = static_cast<EOSUnifiedFriendsManager*>(Info->ClientData);
	if (!self) return;

	if (Info->ResultCode == EOS_EResult::EOS_Success)
	{
		self->QueryFriends();
	}
	else
	{
		UE_LOG(LogEOSUnifiedFriends, Warning, TEXT("[Friends] RejectInvite failed: %s"),
			UTF8_TO_TCHAR(ResultToStr(Info->ResultCode)));
	}
}

void EOS_CALL EOSUnifiedFriendsManager::OnFriendsUpdate(const EOS_Friends_OnFriendsUpdateInfo* Info)
{
	if (!Info) return;
	auto* self = static_cast<EOSUnifiedFriendsManager*>(Info->ClientData);
	if (!self) return;

	self->HandleFriendsDelta(Info->TargetUserId, Info->CurrentStatus, Info->LocalUserId);
}

void EOS_CALL EOSUnifiedFriendsManager::OnShowOverlayComplete(const EOS_UI_ShowFriendsCallbackInfo* Info)
{
	if (!Info) return;
	auto* self = static_cast<EOSUnifiedFriendsManager*>(Info->ClientData);
	if (!self) return;

	if (Info->ResultCode == EOS_EResult::EOS_Success)
	{
		self->QueryFriends();
	}
}

void EOS_CALL EOSUnifiedFriendsManager::OnQueryUserInfoComplete(const EOS_UserInfo_QueryUserInfoCallbackInfo* Info)
{
	if (!Info) return;
	auto* self = static_cast<EOSUnifiedFriendsManager*>(Info->ClientData);
	if (!self) return;

	if (Info->ResultCode != EOS_EResult::EOS_Success)
	{
		UE_LOG(LogEOSUnifiedFriends, Warning, TEXT("[Friends] QueryUserInfo failed: %s"),
			UTF8_TO_TCHAR(ResultToStr(Info->ResultCode)));
		return;
	}

	EOS_HUserInfo ui = EOS_Platform_GetUserInfoInterface(self->Platform);
	EOS_UserInfo* user = nullptr;

	EOS_UserInfo_CopyUserInfoOptions c{};
	c.ApiVersion   = EOS_USERINFO_COPYUSERINFO_API_LATEST;
	c.LocalUserId  = Info->LocalUserId;
	c.TargetUserId = Info->TargetUserId;

	if (EOS_UserInfo_CopyUserInfo(ui, &c, &user) == EOS_EResult::EOS_Success && user)
	{
		std::string epic = EpicIdToString(Info->TargetUserId);
		auto it = self->FriendsByEpic.find(epic);
		if (it != self->FriendsByEpic.end())
		{
			it->second.DisplayName = user->DisplayName ? user->DisplayName : "";
			it->second.HasUserInfo = true;
			self->EmitUpdated();
		}
		EOS_UserInfo_Release(user);
	}
}

void EOS_CALL EOSUnifiedFriendsManager::OnPresenceChanged(const EOS_Presence_PresenceChangedCallbackInfo* Info)
{
	if (!Info) return;
	auto* self = static_cast<EOSUnifiedFriendsManager*>(Info->ClientData);
	if (!self) return;

	self->BeginQueryPresence(Info->PresenceUserId);
}

void EOS_CALL EOSUnifiedFriendsManager::OnQueryPresenceComplete(const EOS_Presence_QueryPresenceCallbackInfo* Info)
{
	if (!Info) return;
	auto* self = static_cast<EOSUnifiedFriendsManager*>(Info->ClientData);
	if (!self) return;

	if (Info->ResultCode != EOS_EResult::EOS_Success)
	{
		return;
	}

	EOS_HPresence presence = EOS_Platform_GetPresenceInterface(self->Platform);
	EOS_Presence_Info* out = nullptr;

	EOS_Presence_CopyPresenceOptions cop{};
	cop.ApiVersion   = EOS_PRESENCE_COPYPRESENCE_API_LATEST;
	cop.LocalUserId  = Info->LocalUserId;
	cop.TargetUserId = Info->TargetUserId;

	if (EOS_Presence_CopyPresence(presence, &cop, &out) == EOS_EResult::EOS_Success && out)
	{
		std::string epic = EpicIdToString(Info->TargetUserId);
		auto it = self->FriendsByEpic.find(epic);
		if (it != self->FriendsByEpic.end())
		{
			it->second.Presence    = out->Status;
			it->second.HasPresence = true;
			self->EmitUpdated();
		}
		EOS_Presence_Info_Release(out);
	}
}

void EOS_CALL EOSUnifiedFriendsManager::OnQueryExternalMappingsComplete(const EOS_Connect_QueryExternalAccountMappingsCallbackInfo* Info)
{
	if (!Info) return;
	auto* self = static_cast<EOSUnifiedFriendsManager*>(Info->ClientData);
	if (!self) return;

	if (Info->ResultCode != EOS_EResult::EOS_Success)
	{
		UE_LOG(LogEOSUnifiedFriends, Warning, TEXT("[Friends] QueryExternalAccountMappings failed: %s"),
			UTF8_TO_TCHAR(ResultToStr(Info->ResultCode)));
		return;
	}

	EOS_HConnect conn = EOS_Platform_GetConnectInterface(self->Platform);
	if (!conn) return;

	for (auto& kv : self->FriendsByEpic)
	{
		auto& f = kv.second;
		if (!f.ProductId && f.MappingRequested)
		{
			EOS_Connect_GetExternalAccountMappingsOptions get{};
			get.ApiVersion     = EOS_CONNECT_GETEXTERNALACCOUNTMAPPINGS_API_LATEST;
			get.LocalUserId    = self->LocalProductId;
			get.AccountIdType  = EOS_EExternalAccountType::EOS_EAT_EPIC;
			get.TargetExternalUserId = f.EpicIdStr.c_str();

			EOS_ProductUserId mapped = EOS_Connect_GetExternalAccountMapping(conn, &get);
			if (mapped)
			{
				f.ProductId = mapped;
				f.MappingRequested = false;
			}
		}
	}
	self->EmitUpdated();
}

// ---- internal handlers ----
void EOSUnifiedFriendsManager::HandleQueryFriendsComplete(const EOS_Friends_QueryFriendsCallbackInfo* Info)
{
	UE_LOG(LogEOSUnifiedFriends, Log, TEXT("[Friends] QueryFriendsComplete rc=%s"),
		UTF8_TO_TCHAR(EOS_EResult_ToString(Info->ResultCode)));
	UE_LOG(LogEOSUnifiedFriends, Log, TEXT("[Friends] Enumerating friends..."));

	if (Info->ResultCode != EOS_EResult::EOS_Success)
	{
		UE_LOG(LogEOSUnifiedFriends, Error, TEXT("[Friends] QueryFriends failed: %s"),
			UTF8_TO_TCHAR(ResultToStr(Info->ResultCode)));
		return;
	}

	EOS_HFriends friends = EOS_Platform_GetFriendsInterface(Platform);

	EOS_Friends_GetFriendsCountOptions c{};
	c.ApiVersion  = EOS_FRIENDS_GETFRIENDSCOUNT_API_LATEST;
	c.LocalUserId = Info->LocalUserId;
	int32_t count = EOS_Friends_GetFriendsCount(friends, &c);

	UE_LOG(LogEOSUnifiedFriends, Log, TEXT("[Friends] Count=%d"), count);

	FriendsByEpic.clear();
	OrderedFriends.clear();

	EOS_Friends_GetFriendAtIndexOptions idx{};
	idx.ApiVersion  = EOS_FRIENDS_GETFRIENDATINDEX_API_LATEST;
	idx.LocalUserId = Info->LocalUserId;

	for (int32_t i = 0; i < count; ++i)
	{
		idx.Index = i;
		EOS_EpicAccountId friendEpic = EOS_Friends_GetFriendAtIndex(friends, &idx);
		if (!EOS_EpicAccountId_IsValid(friendEpic)) continue;

		EOS_Friends_GetStatusOptions so{};
		so.ApiVersion   = EOS_FRIENDS_GETSTATUS_API_LATEST;
		so.LocalUserId  = Info->LocalUserId;
		so.TargetUserId = friendEpic;
		EOS_EFriendsStatus st = EOS_Friends_GetStatus(friends, &so);

		FriendEntry entry;
		entry.EpicId    = friendEpic;
		entry.EpicIdStr = EpicIdToString(friendEpic);
		entry.Status    = st;

		FriendsByEpic[entry.EpicIdStr] = entry;
	}

	bInitialFriendQueryFinished = true;

	// enrichment passes
	QueryNamesForUnknown();
	QueryPresenceForFriends();
	QueryMissingPUIDMappings(true);

	EmitUpdated();
}

void EOSUnifiedFriendsManager::HandleFriendsDelta(EOS_EpicAccountId targetEpic, EOS_EFriendsStatus newStatus, EOS_EpicAccountId /*localEpic*/)
{
	std::string key = EpicIdToString(targetEpic);
	auto it = FriendsByEpic.find(key);

	if (newStatus == EOS_EFriendsStatus::EOS_FS_NotFriends)
	{
		if (it != FriendsByEpic.end())
		{
			FriendsByEpic.erase(it);
			EmitUpdated();
		}
		return;
	}

	if (it == FriendsByEpic.end())
	{
		if (bInitialFriendQueryFinished)
		{
			// lazily refresh full list so we have consistent status + names
			QueryFriends();
		}
		return;
	}
	else
	{
		if (it->second.Status != newStatus)
		{
			it->second.Status = newStatus;
			EmitUpdated();
		}
	}
}

void EOSUnifiedFriendsManager::HandleUserInfoReady(EOS_EpicAccountId /*targetEpic*/) {}
void EOSUnifiedFriendsManager::HandlePresenceReady(EOS_EpicAccountId /*targetEpic*/) {}

void EOSUnifiedFriendsManager::EnsureNotifies()
{
	if (!Platform) return;

	// Friends updates
	EOS_HFriends friends = EOS_Platform_GetFriendsInterface(Platform);
	if (friends && FriendsNotifyId == EOS_INVALID_NOTIFICATIONID)
	{
		EOS_Friends_AddNotifyFriendsUpdateOptions o{};
		o.ApiVersion = EOS_FRIENDS_ADDNOTIFYFRIENDSUPDATE_API_LATEST;
		FriendsNotifyId = EOS_Friends_AddNotifyFriendsUpdate(friends, &o, this, &EOSUnifiedFriendsManager::OnFriendsUpdate);
		if (FriendsNotifyId != EOS_INVALID_NOTIFICATIONID)
		{
			UE_LOG(LogEOSUnifiedFriends, Verbose, TEXT("[Friends] Registered FriendsUpdate notify (%lld)"),
				static_cast<long long>(FriendsNotifyId));
		}
	}

	// Presence updates
	EOS_HPresence presence = EOS_Platform_GetPresenceInterface(Platform);
	if (presence && PresenceNotifyId == EOS_INVALID_NOTIFICATIONID)
	{
		EOS_Presence_AddNotifyOnPresenceChangedOptions p{};
		p.ApiVersion = EOS_PRESENCE_ADDNOTIFYONPRESENCECHANGED_API_LATEST;
		PresenceNotifyId = EOS_Presence_AddNotifyOnPresenceChanged(presence, &p, this, &EOSUnifiedFriendsManager::OnPresenceChanged);
		if (PresenceNotifyId != EOS_INVALID_NOTIFICATIONID)
		{
			UE_LOG(LogEOSUnifiedFriends, Verbose, TEXT("[Friends] Registered PresenceChanged notify (%lld)"),
				static_cast<long long>(PresenceNotifyId));
		}
	}
}

void EOSUnifiedFriendsManager::RemoveNotifies()
{
	if (!Platform) return;

	EOS_HFriends friends = EOS_Platform_GetFriendsInterface(Platform);
	if (friends && FriendsNotifyId != EOS_INVALID_NOTIFICATIONID)
	{
		EOS_Friends_RemoveNotifyFriendsUpdate(friends, FriendsNotifyId);
		UE_LOG(LogEOSUnifiedFriends, Verbose, TEXT("[Friends] Removed FriendsUpdate notify (%lld)"),
			static_cast<long long>(FriendsNotifyId));
		FriendsNotifyId = EOS_INVALID_NOTIFICATIONID;
	}

	EOS_HPresence presence = EOS_Platform_GetPresenceInterface(Platform);
	if (presence && PresenceNotifyId != EOS_INVALID_NOTIFICATIONID)
	{
		EOS_Presence_RemoveNotifyOnPresenceChanged(presence, PresenceNotifyId);
		UE_LOG(LogEOSUnifiedFriends, Verbose, TEXT("[Friends] Removed PresenceChanged notify (%lld)"),
			static_cast<long long>(PresenceNotifyId));
		PresenceNotifyId = EOS_INVALID_NOTIFICATIONID;
	}
}

void EOSUnifiedFriendsManager::BeginQueryUserInfo(EOS_EpicAccountId targetEpic)
{
	if (!Platform || !LocalEpicId || !targetEpic) return;

	EOS_HUserInfo ui = EOS_Platform_GetUserInfoInterface(Platform);
	if (!ui) return;

	EOS_UserInfo_QueryUserInfoOptions q{};
	q.ApiVersion   = EOS_USERINFO_QUERYUSERINFO_API_LATEST;
	q.LocalUserId  = LocalEpicId;
	q.TargetUserId = targetEpic;

	EOS_UserInfo_QueryUserInfo(ui, &q, this, &EOSUnifiedFriendsManager::OnQueryUserInfoComplete);
}

void EOSUnifiedFriendsManager::BeginQueryPresence(EOS_EpicAccountId targetEpic)
{
	if (!Platform || !LocalEpicId || !targetEpic) return;

	EOS_HPresence presence = EOS_Platform_GetPresenceInterface(Platform);
	if (!presence) return;

	EOS_Presence_QueryPresenceOptions q{};
	q.ApiVersion   = EOS_PRESENCE_QUERYPRESENCE_API_LATEST;
	q.LocalUserId  = LocalEpicId;
	q.TargetUserId = targetEpic;

	EOS_Presence_QueryPresence(presence, &q, this, &EOSUnifiedFriendsManager::OnQueryPresenceComplete);
}

void EOSUnifiedFriendsManager::BeginQueryExternalMappings(const std::vector<EOS_EpicAccountId>& epics)
{
	if (!Platform || !LocalProductId || epics.empty()) return;

	EOS_HConnect conn = EOS_Platform_GetConnectInterface(Platform);
	if (!conn) return;

	std::vector<std::string> epicStrs;
	std::vector<const char*> ptrs;

	epicStrs.reserve(epics.size());
	ptrs.reserve(epics.size());

	for (auto e : epics)
	{
		epicStrs.push_back(EpicIdToString(e));
	}
	for (auto& s : epicStrs) ptrs.push_back(s.c_str());

	EOS_Connect_QueryExternalAccountMappingsOptions q{};
	q.ApiVersion                = EOS_CONNECT_QUERYEXTERNALACCOUNTMAPPINGS_API_LATEST;
	q.LocalUserId               = LocalProductId;
	q.AccountIdType             = EOS_EExternalAccountType::EOS_EAT_EPIC;
	q.ExternalAccountIds        = ptrs.data();
	q.ExternalAccountIdCount    = (uint32_t)ptrs.size();

	EOS_Connect_QueryExternalAccountMappings(conn, &q, this, &EOSUnifiedFriendsManager::OnQueryExternalMappingsComplete);
}

