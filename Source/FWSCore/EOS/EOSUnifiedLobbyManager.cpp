// EOSUnifiedLobbyManager.cpp — fixed signatures + named ctx + verbose logging
#include "EOSUnifiedLobbyManager.h"

#include "Logging/LogMacros.h"
#include <atomic>
#include <memory>
#include <string>
#include <cstring>

#include <eos_lobby.h>
#include <eos_lobby_types.h>
#include <eos_connect.h>
#include <eos_ui.h>
#include <eos_ui_types.h>

#include "EOSUnifiedAuthManager.h"

// Expect this category to be defined in your module's .cpp:
// DEFINE_LOG_CATEGORY(LogEOSUnifiedLobby);
DECLARE_LOG_CATEGORY_EXTERN(LogEOSUnifiedLobby, Log, All);

#ifndef EOS_LOBBY_SEARCH_BUCKET_ID
#define EOS_LOBBY_SEARCH_BUCKET_ID "bucket_id"
#endif

static const char* kBucketKey     = EOS_LOBBY_SEARCH_BUCKET_ID; // "bucket_id"
static const char* kDefaultBucket = "default";

static inline const TCHAR* ToTChar(const char* s) { return s ? UTF8_TO_TCHAR(s) : TEXT("<null>"); }

static FString PuidToString(EOS_ProductUserId Id)
{
    if (!Id) return TEXT("<null>");
    char Buf[EOS_PRODUCTUSERID_MAX_LENGTH + 1]{};
    int32 Len = sizeof(Buf);
    EOS_ProductUserId_ToString(Id, Buf, &Len);
    return UTF8_TO_TCHAR(Buf);
}
static FString EaIdToString(EOS_EpicAccountId Id)
{
    if (!Id) return TEXT("<null>");
    char Buf[EOS_EPICACCOUNTID_MAX_LENGTH + 1]{};
    int32 Len = sizeof(Buf);
    EOS_EpicAccountId_ToString(Id, Buf, &Len);
    return UTF8_TO_TCHAR(Buf);
}

// ---------- helpers ----------

static void SetSearchStringParam(EOS_HLobbySearch search, const char* key, const char* value, EOS_EComparisonOp op)
{
    EOS_LobbySearch_SetParameterOptions p{};
    p.ApiVersion = EOS_LOBBYSEARCH_SETPARAMETER_API_LATEST;

    EOS_Lobby_AttributeData d{};
    d.ApiVersion   = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
    d.Key          = key;
    d.Value.AsUtf8 = value;
    d.ValueType    = EOS_ELobbyAttributeType::EOS_AT_STRING; // correct enum for Lobby

    p.Parameter    = &d;
    p.ComparisonOp = op;

    EOS_LobbySearch_SetParameter(search, &p);
}

// Named payload types (avoid anonymous structs in template args)
struct FEmptyExtra {};
struct FJoinExtra  { std::string LobbyId; };

// Per-call context that carries Self + “done” flag + payload
template <typename T>
struct TCallCtx
{
    EOSUnifiedLobbyManager* Self = nullptr;
    std::atomic<bool>*      Done = nullptr;
    T                       Extra{};
};

// ---------- ctor/dtor ----------

EOSUnifiedLobbyManager::EOSUnifiedLobbyManager(EOSUnifiedAuthManager* authMgr, EOSUnifiedFriendsManager* friendsMgr)
    : AuthMgr(authMgr)
    , FriendsMgr(friendsMgr)
{
}

EOSUnifiedLobbyManager::~EOSUnifiedLobbyManager()
{
    Shutdown();
}

// ---------- lifecycle ----------

void EOSUnifiedLobbyManager::Initialize(EOS_HPlatform platform, EOS_ProductUserId localProductUserId)
{
    Platform  = platform;
    LocalPUID = localProductUserId;

    UE_LOG(LogEOSUnifiedLobby, Log, TEXT("[LobbyMgr::Initialize] Platform=%p LocalPUID=%s"),
        Platform, *PuidToString(LocalPUID));

    if (AuthMgr)
    {
        UE_LOG(LogEOSUnifiedLobby, Log, TEXT("[LobbyMgr::Initialize] AuthMgr EAID=%s PUID=%s Display=%s"),
            *AuthMgr->GetEpicAccountIdString(),
            *PuidToString(AuthMgr->GetProductUserId()),
            *AuthMgr->GetCachedDisplayName());
    }

    RegisterNotifies();
}

void EOSUnifiedLobbyManager::Shutdown()
{
    UnregisterNotifies();

    if (CurrentLobbyDetails)
    {
        EOS_LobbyDetails_Release(CurrentLobbyDetails);
        CurrentLobbyDetails = nullptr;
    }
    CurrentLobbyId.clear();
    CachedSummaries.clear();

    Platform  = nullptr;
    LocalPUID = nullptr;
}

// ---------- summary ----------

FEOSLobbySummary EOSUnifiedLobbyManager::MakeSummary(EOS_HLobbyDetails details)
{
    FEOSLobbySummary S;
    if (!details) return S;

    EOS_LobbyDetails_Info* Info = nullptr;
    EOS_LobbyDetails_CopyInfoOptions C{}; C.ApiVersion = EOS_LOBBYDETAILS_COPYINFO_API_LATEST;
    if (EOS_LobbyDetails_CopyInfo(details, &C, &Info) == EOS_EResult::EOS_Success && Info)
    {
        if (Info->LobbyId) S.LobbyId = Info->LobbyId;
        S.MaxMembers       = Info->MaxMembers;
        S.bPresenceEnabled = Info->bPresenceEnabled ? true : false;
        S.bAllowInvites    = Info->bAllowInvites ? true : false;
        EOS_LobbyDetails_Info_Release(Info);
    }

    // owner
    {
        EOS_LobbyDetails_GetLobbyOwnerOptions GO{}; GO.ApiVersion = EOS_LOBBYDETAILS_GETLOBBYOWNER_API_LATEST;
        EOS_ProductUserId Owner = EOS_LobbyDetails_GetLobbyOwner(details, &GO);
        if (Owner) S.OwnerPUID = TCHAR_TO_UTF8(*PuidToString(Owner));
    }

    // members
    {
        EOS_LobbyDetails_GetMemberCountOptions M{}; M.ApiVersion = EOS_LOBBYDETAILS_GETMEMBERCOUNT_API_LATEST;
        S.MemberCount = EOS_LobbyDetails_GetMemberCount(details, &M);
    }

    // selected string attributes
    auto ReadAttr = [&](const char* Key, std::string& Out)
    {
        EOS_LobbyDetails_GetAttributeCountOptions A{}; A.ApiVersion = EOS_LOBBYDETAILS_GETATTRIBUTECOUNT_API_LATEST;
        const int32 Count = EOS_LobbyDetails_GetAttributeCount(details, &A);
        for (int32 i = 0; i < Count; ++i)
        {
            EOS_LobbyDetails_CopyAttributeByIndexOptions AO{}; AO.ApiVersion = EOS_LOBBYDETAILS_COPYATTRIBUTEBYINDEX_API_LATEST; AO.AttrIndex = i;
            EOS_Lobby_Attribute* Attr = nullptr;
            if (EOS_LobbyDetails_CopyAttributeByIndex(details, &AO, &Attr) == EOS_EResult::EOS_Success && Attr)
            {
                if (Attr->Data && Attr->Data->Key && std::strcmp(Attr->Data->Key, Key) == 0 &&
                    Attr->Data->ValueType == EOS_ELobbyAttributeType::EOS_AT_STRING &&
                    Attr->Data->Value.AsUtf8)
                {
                    Out = Attr->Data->Value.AsUtf8;
                    EOS_Lobby_Attribute_Release(Attr);
                    return;
                }
                EOS_Lobby_Attribute_Release(Attr);
            }
        }
    };

    ReadAttr("Name", S.Name);
    ReadAttr("Map",  S.Map);
    ReadAttr("Mode", S.Mode);
    return S;
}

// ---------- search ----------

struct FSearchCtx
{
    EOSUnifiedLobbyManager* Self = nullptr;
    EOS_HLobbySearch        Search = nullptr;
    std::atomic<bool>*      Finished = nullptr;
};

void EOSUnifiedLobbyManager::StartSearch(std::function<void(EOS_HLobbySearch)> configure, std::atomic<bool>* finishedFlag)
{
    if (finishedFlag) finishedFlag->store(false);

    if (!Platform || !LocalPUID)
    {
        UE_LOG(LogEOSUnifiedLobby, Warning, TEXT("[Lobby] Search aborted: Platform or LocalPUID not set"));
        if (finishedFlag) finishedFlag->store(true);
        return;
    }

    EOS_HLobby Lobby = EOS_Platform_GetLobbyInterface(Platform);
    if (!Lobby)
    {
        UE_LOG(LogEOSUnifiedLobby, Error, TEXT("[Lobby] GetLobbyInterface failed"));
        if (finishedFlag) finishedFlag->store(true);
        return;
    }

    EOS_HLobbySearch Search = nullptr;
    EOS_Lobby_CreateLobbySearchOptions Opt{}; Opt.ApiVersion = EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST; Opt.MaxResults = 50;
    const EOS_EResult RcCreate = EOS_Lobby_CreateLobbySearch(Lobby, &Opt, &Search);
    if (RcCreate != EOS_EResult::EOS_Success || !Search)
    {
        UE_LOG(LogEOSUnifiedLobby, Error, TEXT("[Lobby] CreateLobbySearch failed: %hs"), EOS_EResult_ToString(RcCreate));
        if (finishedFlag) finishedFlag->store(true);
        return;
    }

    if (configure) configure(Search);

    auto* Ctx = new FSearchCtx{ this, Search, finishedFlag };

    EOS_LobbySearch_FindOptions F{}; F.ApiVersion = EOS_LOBBYSEARCH_FIND_API_LATEST; F.LocalUserId = LocalPUID;
    UE_LOG(LogEOSUnifiedLobby, Verbose, TEXT("[Lobby] Search Find LocalUserId=%s"), *PuidToString(LocalPUID));

    EOS_LobbySearch_Find(Search, &F, Ctx, &EOSUnifiedLobbyManager::OnSearchComplete);
}

void EOSUnifiedLobbyManager::FinishAndEmitSearch(EOS_HLobbySearch searchHandle, EOS_EResult rc, std::atomic<bool>* finishedFlag)
{
    std::vector<FEOSLobbySummary> Results;

    if (rc == EOS_EResult::EOS_Success)
    {
        EOS_LobbySearch_GetSearchResultCountOptions C{}; C.ApiVersion = EOS_LOBBYSEARCH_GETSEARCHRESULTCOUNT_API_LATEST;
        const int32_t Count = EOS_LobbySearch_GetSearchResultCount(searchHandle, &C);

        for (int32_t i = 0; i < Count; ++i)
        {
            EOS_HLobbyDetails Details = nullptr;
            EOS_LobbySearch_CopySearchResultByIndexOptions CI{}; CI.ApiVersion = EOS_LOBBYSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST; CI.LobbyIndex = i;
            const EOS_EResult RcCopy = EOS_LobbySearch_CopySearchResultByIndex(searchHandle, &CI, &Details);
            if (RcCopy != EOS_EResult::EOS_Success || !Details)
                continue;

            Results.emplace_back(MakeSummary(Details));
            EOS_LobbyDetails_Release(Details);
        }
    }

    if (searchHandle)
        EOS_LobbySearch_Release(searchHandle);

    CachedSummaries = std::move(Results);
    if (OnSearchResultsUpdated)
        OnSearchResultsUpdated(CachedSummaries);

    if (finishedFlag) finishedFlag->store(true);
}

// ---------- operations ----------

void EOSUnifiedLobbyManager::CreateLobby(std::atomic<bool>* finishedFlag)
{
    if (finishedFlag) finishedFlag->store(false);

    if (!Platform || !LocalPUID)
    {
        UE_LOG(LogEOSUnifiedLobby, Warning, TEXT("[Lobby] CreateLobby aborted: Platform or LocalPUID not set"));
        if (finishedFlag) finishedFlag->store(true);
        return;
    }

    // Guard: must be Connect-logged in (prevents early/invalid tokens → 403)
    EOS_HConnect Con = EOS_Platform_GetConnectInterface(Platform);
    EOS_ELoginStatus Status = Con ? EOS_Connect_GetLoginStatus(Con, LocalPUID) : EOS_ELoginStatus::EOS_LS_NotLoggedIn;
    if (!Con || Status != EOS_ELoginStatus::EOS_LS_LoggedIn)
    {
        UE_LOG(LogEOSUnifiedLobby, Warning, TEXT("[Lobby] Not Connect-logged in (Status=%d) PUID=%s; aborting CreateLobby"),
            (int32)Status, *PuidToString(LocalPUID));
        if (finishedFlag) finishedFlag->store(true);
        return;
    }

    EOS_HLobby Lobby = EOS_Platform_GetLobbyInterface(Platform);

    EOS_Lobby_CreateLobbyOptions Opt{};
    Opt.ApiVersion        = EOS_LOBBY_CREATELOBBY_API_LATEST;
    Opt.LocalUserId       = LocalPUID;
    Opt.MaxLobbyMembers   = 4; // tweak
    Opt.PermissionLevel   = EOS_ELobbyPermissionLevel::EOS_LPL_PUBLICADVERTISED;
    Opt.bPresenceEnabled  = EOS_FALSE; // keep false until portal Presence scopes enabled
    Opt.bAllowInvites     = EOS_TRUE;
    Opt.BucketId          = kDefaultBucket;

    UE_LOG(LogEOSUnifiedLobby, Log, TEXT("[Lobby] CreateLobby LocalPUID=%s Max=%d Presence=%d Invites=%d Bucket=%hs"),
        *PuidToString(LocalPUID), (int32)Opt.MaxLobbyMembers, (int32)Opt.bPresenceEnabled, (int32)Opt.bAllowInvites, Opt.BucketId);

    auto* Ctx = new TCallCtx<FEmptyExtra>{ this, finishedFlag, {} };
    EOS_Lobby_CreateLobby(Lobby, &Opt, Ctx, &EOSUnifiedLobbyManager::OnCreateLobbyComplete);
}

void EOSUnifiedLobbyManager::LeaveLobby(std::atomic<bool>* finishedFlag)
{
    if (finishedFlag) finishedFlag->store(false);

    if (!Platform || !LocalPUID || CurrentLobbyId.empty())
    {
        UE_LOG(LogEOSUnifiedLobby, Warning, TEXT("[Lobby] LeaveLobby aborted: not in a lobby"));
        if (finishedFlag) finishedFlag->store(true);
        return;
    }

    EOS_HLobby Lobby = EOS_Platform_GetLobbyInterface(Platform);

    EOS_Lobby_LeaveLobbyOptions Opt{}; Opt.ApiVersion = EOS_LOBBY_LEAVELOBBY_API_LATEST;
    Opt.LocalUserId = LocalPUID;
    Opt.LobbyId     = CurrentLobbyId.c_str();

    UE_LOG(LogEOSUnifiedLobby, Log, TEXT("[Lobby] LeaveLobby LocalPUID=%s LobbyId=%s"),
        *PuidToString(LocalPUID), ToTChar(CurrentLobbyId.c_str()));

    auto* Ctx = new TCallCtx<FEmptyExtra>{ this, finishedFlag, {} };
    EOS_Lobby_LeaveLobby(Lobby, &Opt, Ctx, &EOSUnifiedLobbyManager::OnLeaveLobbyComplete);
    
}

void EOSUnifiedLobbyManager::DestroyLobby(std::atomic<bool>* finishedFlag)
{
    if (finishedFlag) finishedFlag->store(false);

    if (!Platform || !LocalPUID || CurrentLobbyId.empty())
    {
        UE_LOG(LogEOSUnifiedLobby, Warning, TEXT("[Lobby] DestroyLobby aborted: not owner or no lobby"));
        if (finishedFlag) finishedFlag->store(true);
        return;
    }

    EOS_HLobby Lobby = EOS_Platform_GetLobbyInterface(Platform);

    EOS_Lobby_DestroyLobbyOptions Opt{}; Opt.ApiVersion = EOS_LOBBY_DESTROYLOBBY_API_LATEST;
    Opt.LocalUserId = LocalPUID;
    Opt.LobbyId     = CurrentLobbyId.c_str();

    UE_LOG(LogEOSUnifiedLobby, Log, TEXT("[Lobby] DestroyLobby LocalPUID=%s LobbyId=%s"),
        *PuidToString(LocalPUID), ToTChar(CurrentLobbyId.c_str()));

    EOS_Lobby_DestroyLobby(Lobby, &Opt, finishedFlag, &EOSUnifiedLobbyManager::OnDestroyLobbyComplete);
}

void EOSUnifiedLobbyManager::SearchLobbies(std::atomic<bool>* finishedFlag)
{
    StartSearch(
        [&](EOS_HLobbySearch Search)
        {
            SetSearchStringParam(Search, kBucketKey, kDefaultBucket, EOS_EComparisonOp::EOS_CO_EQUAL);
        },
        finishedFlag);
}

void EOSUnifiedLobbyManager::SearchAllLobbies(std::atomic<bool>* finishedFlag)
{
    StartSearch(
        [&](EOS_HLobbySearch Search)
        {
            SetSearchStringParam(Search, kBucketKey, kDefaultBucket, EOS_EComparisonOp::EOS_CO_EQUAL);
        },
        finishedFlag);
}

void EOSUnifiedLobbyManager::SearchLobbiesByName(std::atomic<bool>* finishedFlag)
{
    StartSearch(
        [&](EOS_HLobbySearch Search)
        {
            SetSearchStringParam(Search, kBucketKey, kDefaultBucket, EOS_EComparisonOp::EOS_CO_EQUAL);
            SetSearchStringParam(Search, "Name", "DefaultLobby", EOS_EComparisonOp::EOS_CO_EQUAL);
        },
        finishedFlag);
}

void EOSUnifiedLobbyManager::SearchWithFilters(const std::vector<FSearchFilter>& filters, uint32_t maxResults, std::atomic<bool>* finishedFlag)
{
    StartSearch(
        [&](EOS_HLobbySearch Search)
        {
            EOS_LobbySearch_SetMaxResultsOptions MR{}; MR.ApiVersion = EOS_LOBBYSEARCH_SETMAXRESULTS_API_LATEST; MR.MaxResults = maxResults ? maxResults : 50;
            EOS_LobbySearch_SetMaxResults(Search, &MR);

            SetSearchStringParam(Search, kBucketKey, kDefaultBucket, EOS_EComparisonOp::EOS_CO_EQUAL);

            for (const auto& f : filters)
                SetSearchStringParam(Search, f.Key.c_str(), f.Value.c_str(), f.Op);
        },
        finishedFlag);
}

void EOSUnifiedLobbyManager::JoinLobby(const std::string& lobbyId, std::atomic<bool>* finishedFlag)
{
    if (finishedFlag) finishedFlag->store(false);

    if (!Platform || !LocalPUID || lobbyId.empty())
    {
        UE_LOG(LogEOSUnifiedLobby, Warning, TEXT("[Lobby] JoinLobby aborted: invalid params"));
        if (finishedFlag) finishedFlag->store(true);
        return;
    }

    EOS_HLobby Lobby = EOS_Platform_GetLobbyInterface(Platform);

    EOS_Lobby_JoinLobbyByIdOptions Opt{}; Opt.ApiVersion = EOS_LOBBY_JOINLOBBYBYID_API_LATEST;
    Opt.LocalUserId = LocalPUID;
    Opt.LobbyId     = lobbyId.c_str();
    Opt.bPresenceEnabled = EOS_FALSE;

    UE_LOG(LogEOSUnifiedLobby, Log, TEXT("[Lobby] JoinLobbyById LocalPUID=%s LobbyId=%s"),
        *PuidToString(LocalPUID), ToTChar(lobbyId.c_str()));

    auto* Ctx = new TCallCtx<FJoinExtra>{ this, finishedFlag, { lobbyId } };
    EOS_Lobby_JoinLobbyById(Lobby, &Opt, Ctx, &EOSUnifiedLobbyManager::OnJoinLobbyByIdComplete);
}

void EOSUnifiedLobbyManager::SendInviteTo(const std::string& targetProductUserId)
{
    if (!Platform || !LocalPUID || CurrentLobbyId.empty() || targetProductUserId.empty())
    {
        UE_LOG(LogEOSUnifiedLobby, Warning, TEXT("[Lobby] SendInviteTo aborted: missing params"));
        return;
    }

    EOS_HLobby Lobby = EOS_Platform_GetLobbyInterface(Platform);

    EOS_ProductUserId Target = EOS_ProductUserId_FromString(targetProductUserId.c_str());
    if (!Target)
    {
        UE_LOG(LogEOSUnifiedLobby, Warning, TEXT("[Lobby] SendInviteTo: invalid Target PUID '%s'"), ToTChar(targetProductUserId.c_str()));
        return;
    }

    EOS_Lobby_SendInviteOptions Opt{}; Opt.ApiVersion = EOS_LOBBY_SENDINVITE_API_LATEST;
    Opt.LocalUserId = LocalPUID;
    Opt.LobbyId     = CurrentLobbyId.c_str();
    Opt.TargetUserId= Target;

    UE_LOG(LogEOSUnifiedLobby, Log, TEXT("[Lobby] SendInvite LocalPUID=%s LobbyId=%s TargetPUID=%s"),
        *PuidToString(LocalPUID), ToTChar(CurrentLobbyId.c_str()), *PuidToString(Target));

    EOS_Lobby_SendInvite(Lobby, &Opt, nullptr, &EOSUnifiedLobbyManager::OnSendInviteComplete);
}

void EOSUnifiedLobbyManager::AcceptInvite(const std::string& inviteId, std::atomic<bool>* finishedFlag)
{
    UE_LOG(LogEOSUnifiedLobby, Warning, TEXT("[Lobby] AcceptInvite not implemented in this build (InviteId=%s)"),
        ToTChar(inviteId.c_str()));
    if (finishedFlag) finishedFlag->store(true);
}

void EOSUnifiedLobbyManager::RejectInvite(const std::string& inviteId, std::atomic<bool>* finishedFlag)
{
    UE_LOG(LogEOSUnifiedLobby, Warning, TEXT("[Lobby] RejectInvite not implemented in this build (InviteId=%s)"),
        ToTChar(inviteId.c_str()));
    if (finishedFlag) finishedFlag->store(true);
}

void EOSUnifiedLobbyManager::ShowInviteOverlay()
{
    if (!Platform || !AuthMgr) return;

    EOS_HUI UI = EOS_Platform_GetUIInterface(Platform);
    EOS_EpicAccountId EA = AuthMgr->GetUserId();
    if (!UI || !EA)
    {
        UE_LOG(LogEOSUnifiedLobby, Verbose, TEXT("[Lobby] ShowInviteOverlay: UI or EAID missing"));
        return;
    }

    EOS_UI_ShowFriendsOptions Opt{}; Opt.ApiVersion = EOS_UI_SHOWFRIENDS_API_LATEST; Opt.LocalUserId = EA;
    // Correct signature: returns void, needs ClientData + completion callback
    EOS_UI_ShowFriends(UI, &Opt, this, &EOSUnifiedLobbyManager::OnShowFriendsComplete);
}

void EOSUnifiedLobbyManager::ShowLeaveLobbyOverlay()
{
    UE_LOG(LogEOSUnifiedLobby, Verbose, TEXT("[Lobby] ShowLeaveLobbyOverlay: no-op"));
}

void EOSUnifiedLobbyManager::ModifyCurrentLobby(const char* name, const char* map, const char* mode, int newMaxMembers)
{
    if (!Platform || !LocalPUID || CurrentLobbyId.empty())
    {
        UE_LOG(LogEOSUnifiedLobby, Warning, TEXT("[Lobby] ModifyCurrentLobby aborted: invalid state"));
        return;
    }

    EOS_HLobby Lobby = EOS_Platform_GetLobbyInterface(Platform);

    EOS_Lobby_UpdateLobbyModificationOptions UO{}; UO.ApiVersion = EOS_LOBBY_UPDATELOBBYMODIFICATION_API_LATEST;
    UO.LobbyId     = CurrentLobbyId.c_str();
    UO.LocalUserId = LocalPUID;

    EOS_HLobbyModification Mod = nullptr;
    EOS_EResult Rc = EOS_Lobby_UpdateLobbyModification(Lobby, &UO, &Mod);
    if (Rc != EOS_EResult::EOS_Success || !Mod)
    {
        UE_LOG(LogEOSUnifiedLobby, Error, TEXT("[Lobby] UpdateLobbyModification failed: %hs"), EOS_EResult_ToString(Rc));
        return;
    }

    auto AddAttr = [&](const char* Key, const char* Val)
    {
        if (!Val || !*Val) return;

        EOS_Lobby_AttributeData Attr{};
        Attr.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
        Attr.Key        = Key;
        Attr.ValueType  = EOS_ELobbyAttributeType::EOS_AT_STRING;
        Attr.Value.AsUtf8 = Val;

        EOS_LobbyModification_AddAttributeOptions AO{};
        AO.ApiVersion = EOS_LOBBYMODIFICATION_ADDATTRIBUTE_API_LATEST;
        AO.Attribute  = &Attr;
        AO.Visibility = EOS_ELobbyAttributeVisibility::EOS_LAT_PUBLIC;

        EOS_EResult A = EOS_LobbyModification_AddAttribute(Mod, &AO);
        if (A != EOS_EResult::EOS_Success)
            UE_LOG(LogEOSUnifiedLobby, Warning, TEXT("[Lobby] AddAttribute '%hs' failed: %hs"), Key, EOS_EResult_ToString(A));
    };

    AddAttr("Name", name);
    AddAttr("Map",  map);
    AddAttr("Mode", mode);

    if (newMaxMembers > 0)
    {
        EOS_LobbyModification_SetMaxMembersOptions SM{}; SM.ApiVersion = EOS_LOBBYMODIFICATION_SETMAXMEMBERS_API_LATEST; SM.MaxMembers = newMaxMembers;
        EOS_LobbyModification_SetMaxMembers(Mod, &SM);
    }

    EOS_Lobby_UpdateLobbyOptions U{}; U.ApiVersion = EOS_LOBBY_UPDATELOBBY_API_LATEST; U.LobbyModificationHandle = Mod;

    // Correct signature: returns void; provide ClientData and a static completion
    EOS_Lobby_UpdateLobby(Lobby, &U, this, &EOSUnifiedLobbyManager::OnUpdateLobbyComplete);

    EOS_LobbyModification_Release(Mod);
}

void EOSUnifiedLobbyManager::GetCachedSummaries(std::vector<FEOSLobbySummary>& out) const
{
    out = CachedSummaries;
}

// ---------- notifies ----------

void EOSUnifiedLobbyManager::RegisterNotifies()
{
    if (!Platform) return;
    EOS_HLobby Lobby = EOS_Platform_GetLobbyInterface(Platform);
    if (!Lobby) return;

    if (NotifyInviteReceivedId == EOS_INVALID_NOTIFICATIONID)
    {
        EOS_Lobby_AddNotifyLobbyInviteReceivedOptions O{}; O.ApiVersion = EOS_LOBBY_ADDNOTIFYLOBBYINVITERECEIVED_API_LATEST;
        NotifyInviteReceivedId = EOS_Lobby_AddNotifyLobbyInviteReceived(Lobby, &O, this, &EOSUnifiedLobbyManager::OnLobbyInviteReceivedCallback);
    }

    if (NotifyJoinLobbyAcceptedId == EOS_INVALID_NOTIFICATIONID)
    {
        EOS_Lobby_AddNotifyJoinLobbyAcceptedOptions O{}; O.ApiVersion = EOS_LOBBY_ADDNOTIFYJOINLOBBYACCEPTED_API_LATEST;
        // Correct callback signature type:
        NotifyJoinLobbyAcceptedId = EOS_Lobby_AddNotifyJoinLobbyAccepted(Lobby, &O, this, &EOSUnifiedLobbyManager::OnJoinLobbyAccepted);
    }

    if (NotifyLobbyUpdateId == EOS_INVALID_NOTIFICATIONID)
    {
        EOS_Lobby_AddNotifyLobbyUpdateReceivedOptions O{}; O.ApiVersion = EOS_LOBBY_ADDNOTIFYLOBBYUPDATERECEIVED_API_LATEST;
        NotifyLobbyUpdateId = EOS_Lobby_AddNotifyLobbyUpdateReceived(Lobby, &O, this, &EOSUnifiedLobbyManager::OnLobbyUpdateReceived);
    }

    if (NotifyMemberUpdateId == EOS_INVALID_NOTIFICATIONID)
    {
        EOS_Lobby_AddNotifyLobbyMemberUpdateReceivedOptions O{}; O.ApiVersion = EOS_LOBBY_ADDNOTIFYLOBBYMEMBERUPDATERECEIVED_API_LATEST;
        NotifyMemberUpdateId = EOS_Lobby_AddNotifyLobbyMemberUpdateReceived(Lobby, &O, this, &EOSUnifiedLobbyManager::OnMemberUpdateReceived);
    }
}

void EOSUnifiedLobbyManager::UnregisterNotifies()
{
    if (!Platform) return;
    EOS_HLobby Lobby = EOS_Platform_GetLobbyInterface(Platform);
    if (!Lobby) return;

    if (NotifyInviteReceivedId != EOS_INVALID_NOTIFICATIONID)
    {
        EOS_Lobby_RemoveNotifyLobbyInviteReceived(Lobby, NotifyInviteReceivedId);
        NotifyInviteReceivedId = EOS_INVALID_NOTIFICATIONID;
    }
    if (NotifyJoinLobbyAcceptedId != EOS_INVALID_NOTIFICATIONID)
    {
        EOS_Lobby_RemoveNotifyJoinLobbyAccepted(Lobby, NotifyJoinLobbyAcceptedId);
        NotifyJoinLobbyAcceptedId = EOS_INVALID_NOTIFICATIONID;
    }
    if (NotifyLobbyUpdateId != EOS_INVALID_NOTIFICATIONID)
    {
        EOS_Lobby_RemoveNotifyLobbyUpdateReceived(Lobby, NotifyLobbyUpdateId);
        NotifyLobbyUpdateId = EOS_INVALID_NOTIFICATIONID;
    }
    if (NotifyMemberUpdateId != EOS_INVALID_NOTIFICATIONID)
    {
        EOS_Lobby_RemoveNotifyLobbyMemberUpdateReceived(Lobby, NotifyMemberUpdateId);
        NotifyMemberUpdateId = EOS_INVALID_NOTIFICATIONID;
    }
}

// ---------- static callbacks ----------

void EOS_CALL EOSUnifiedLobbyManager::OnShowFriendsComplete(const EOS_UI_ShowFriendsCallbackInfo* Info)
{
    UE_LOG(LogEOSUnifiedLobby, Log, TEXT("[UI] ShowFriends completed: %hs"),
        EOS_EResult_ToString(Info ? Info->ResultCode : EOS_EResult::EOS_UnexpectedError));
}

void EOS_CALL EOSUnifiedLobbyManager::OnUpdateLobbyComplete(const EOS_Lobby_UpdateLobbyCallbackInfo* Info)
{
    UE_LOG(LogEOSUnifiedLobby, Log, TEXT("[Lobby] UpdateLobby rc=%hs"),
        EOS_EResult_ToString(Info ? Info->ResultCode : EOS_EResult::EOS_UnexpectedError));
}

void EOS_CALL EOSUnifiedLobbyManager::OnCreateLobbyComplete(const EOS_Lobby_CreateLobbyCallbackInfo* Info)
{
    auto* Ctx = static_cast<TCallCtx<FEmptyExtra>*>(Info ? Info->ClientData : nullptr);
    std::unique_ptr<TCallCtx<FEmptyExtra>> Holder(Ctx);
    EOSUnifiedLobbyManager* Self = Holder ? Holder->Self : nullptr;
    if (Holder && Holder->Done) Holder->Done->store(true);

    UE_LOG(LogEOSUnifiedLobby, Log, TEXT("[Lobby] CreateLobby rc=%hs LobbyId=%s"),
        EOS_EResult_ToString(Info ? Info->ResultCode : EOS_EResult::EOS_UnexpectedError),
        ToTChar(Info ? Info->LobbyId : nullptr));

    if (!Self || !Info || Info->ResultCode != EOS_EResult::EOS_Success)
    {
        if (Info)
            UE_LOG(LogEOSUnifiedLobby, Error, TEXT("[Lobby] Create FAILED: %hs"), EOS_EResult_ToString(Info->ResultCode));
        // Bubble failure if you expose OnLobbyCreateFailed in the header (optional)
        if (Self && Self->OnLobbyCreateFailed) Self->OnLobbyCreateFailed(Info ? Info->ResultCode : EOS_EResult::EOS_UnexpectedError, "CreateLobby failed");
        return;
    }

    Self->CurrentLobbyId = Info->LobbyId ? Info->LobbyId : "";

    if (Self->CurrentLobbyDetails)
    {
        EOS_LobbyDetails_Release(Self->CurrentLobbyDetails);
        Self->CurrentLobbyDetails = nullptr;
    }
    
    // Copy details so we can emit a summary to the upper layer immediately
    EOS_HLobby Lobby = EOS_Platform_GetLobbyInterface(Self->Platform);
    EOS_Lobby_CopyLobbyDetailsHandleOptions CO{};
    CO.ApiVersion = EOS_LOBBY_COPYLOBBYDETAILSHANDLE_API_LATEST;
    CO.LobbyId     = Self->CurrentLobbyId.c_str();
    CO.LocalUserId = Self->LocalPUID;

    EOS_HLobbyDetails Details = nullptr;
    if (EOS_Lobby_CopyLobbyDetailsHandle(Lobby, &CO, &Details) == EOS_EResult::EOS_Success && Details)
    {
        if (Self->CurrentLobbyDetails)
            EOS_LobbyDetails_Release(Self->CurrentLobbyDetails);
        Self->CurrentLobbyDetails = Details;

        FEOSLobbySummary S = Self->MakeSummary(Details);
        UE_LOG(LogEOSUnifiedLobby, Log, TEXT("[Lobby] Create OK. CurrentLobbyId=%s Owner=%s Members=%u/%u Name=%s Map=%s Mode=%s"),
            ToTChar(S.LobbyId.c_str()), ToTChar(S.OwnerPUID.c_str()), S.MemberCount, S.MaxMembers,
            ToTChar(S.Name.c_str()), ToTChar(S.Map.c_str()), ToTChar(S.Mode.c_str()));

        if (Self->OnLobbyCreated) Self->OnLobbyCreated(Self->CurrentLobbyId);
        
        if (Self->OnJoinedLobby)
        {
            if ( S.LobbyId.empty() ) S.LobbyId = Self->CurrentLobbyId;
            if ( S.OwnerPUID.empty() ) S.OwnerPUID = Self->LocalPUID ? TCHAR_TO_UTF8(*PuidToString(Self->LocalPUID)) : "<null>";
            if ( S.MemberCount == 0 ) S.MemberCount = 1;
            Self->OnJoinedLobby(S);
        }
    }
}

void EOS_CALL EOSUnifiedLobbyManager::OnDestroyLobbyComplete(const EOS_Lobby_DestroyLobbyCallbackInfo* Info)
{
    auto* Ctx = static_cast<std::atomic<bool>*>(Info ? Info->ClientData : nullptr);
    if (Ctx) Ctx->store(true);
    UE_LOG(LogEOSUnifiedLobby, Log, TEXT("[Lobby] DestroyLobby rc=%hs"),
        EOS_EResult_ToString(Info ? Info->ResultCode : EOS_EResult::EOS_UnexpectedError));
}

void EOS_CALL EOSUnifiedLobbyManager::OnLeaveLobbyComplete(const EOS_Lobby_LeaveLobbyCallbackInfo* Info)
{
    auto* Ctx = static_cast<TCallCtx<FEmptyExtra>*>(Info ? Info->ClientData : nullptr);
    std::unique_ptr<TCallCtx<FEmptyExtra>> Holder(Ctx);
    EOSUnifiedLobbyManager* Self = Holder ? Holder->Self : nullptr;
    if (Holder && Holder->Done) Holder->Done->store(true);
    
    UE_LOG(LogEOSUnifiedLobby, Log, TEXT("[Lobby] LeaveLobby rc=%hs"),
        EOS_EResult_ToString(Info ? Info->ResultCode : EOS_EResult::EOS_UnexpectedError));

    if (Info && Info->ResultCode == EOS_EResult::EOS_Success)
    {
        if (Self->CurrentLobbyDetails) { EOS_LobbyDetails_Release(Self->CurrentLobbyDetails); Self->CurrentLobbyDetails = nullptr; }
        const std::string LeftId = std::exchange(Self->CurrentLobbyId, std::string{});
        if (Self->OnLeftLobby) Self->OnLeftLobby(LeftId);
    }
}

void EOS_CALL EOSUnifiedLobbyManager::OnJoinLobbyByIdComplete(const EOS_Lobby_JoinLobbyByIdCallbackInfo* Info)
{
    auto* Ctx = static_cast<TCallCtx<FJoinExtra>*>(Info ? Info->ClientData : nullptr);
    std::unique_ptr<TCallCtx<FJoinExtra>> Holder(Ctx);
    EOSUnifiedLobbyManager* Self = Holder ? Holder->Self : nullptr;
    if (Holder && Holder->Done) Holder->Done->store(true);

    UE_LOG(LogEOSUnifiedLobby, Log, TEXT("[Lobby] JoinLobbyById rc=%hs LobbyId=%s"),
        EOS_EResult_ToString(Info ? Info->ResultCode : EOS_EResult::EOS_UnexpectedError),
        ToTChar(Info ? Info->LobbyId : nullptr));

    if (!Self || !Info || Info->ResultCode != EOS_EResult::EOS_Success)
        return;

    Self->CurrentLobbyId = Holder->Extra.LobbyId;

    EOS_HLobby Lobby = EOS_Platform_GetLobbyInterface(Self->Platform);
    EOS_Lobby_CopyLobbyDetailsHandleOptions CO{}; CO.ApiVersion = EOS_LOBBY_COPYLOBBYDETAILSHANDLE_API_LATEST;
    CO.LobbyId     = Self->CurrentLobbyId.c_str();
    CO.LocalUserId = Self->LocalPUID;

    EOS_HLobbyDetails Details = nullptr;
    if (EOS_Lobby_CopyLobbyDetailsHandle(Lobby, &CO, &Details) == EOS_EResult::EOS_Success && Details)
    {
        if (Self->CurrentLobbyDetails)
            EOS_LobbyDetails_Release(Self->CurrentLobbyDetails);
        Self->CurrentLobbyDetails = Details;

        FEOSLobbySummary S = Self->MakeSummary(Details);
        if (Self->OnJoinedLobby) Self->OnJoinedLobby(S);
    }
}

void EOS_CALL EOSUnifiedLobbyManager::OnJoinLobbyComplete(const EOS_Lobby_JoinLobbyCallbackInfo* Info)
{
    UE_LOG(LogEOSUnifiedLobby, Log, TEXT("[Lobby] JoinLobby rc=%hs"),
        EOS_EResult_ToString(Info ? Info->ResultCode : EOS_EResult::EOS_UnexpectedError));
}

void EOS_CALL EOSUnifiedLobbyManager::OnJoinLobbyAccepted(const EOS_Lobby_JoinLobbyAcceptedCallbackInfo* Info)
{
    // This notify comes from UI flow; you would typically call EOS_Lobby_CopyLobbyDetailsHandleByUiEventId here,
    // then EOS_Lobby_JoinLobby with that details. For now, we just log to satisfy binding & visibility.
    UE_LOG(LogEOSUnifiedLobby, Log, TEXT("[Lobby] JoinLobbyAccepted UiEventId=%llu LocalUserId=%s"),
        static_cast<unsigned long long>(Info ? Info->UiEventId : 0ULL),
        *PuidToString(Info ? Info->LocalUserId : nullptr));
}

void EOS_CALL EOSUnifiedLobbyManager::OnSendInviteComplete(const EOS_Lobby_SendInviteCallbackInfo* Info)
{
    UE_LOG(LogEOSUnifiedLobby, Log, TEXT("[Lobby] SendInvite rc=%hs"),
        EOS_EResult_ToString(Info ? Info->ResultCode : EOS_EResult::EOS_UnexpectedError));
}

void EOS_CALL EOSUnifiedLobbyManager::OnSearchComplete(const EOS_LobbySearch_FindCallbackInfo* Info)
{
    auto* Ctx = static_cast<FSearchCtx*>(Info ? Info->ClientData : nullptr);
    std::unique_ptr<FSearchCtx> Holder(Ctx);

    if (!Info || !Holder || !Holder->Self)
        return;

    Holder->Self->FinishAndEmitSearch(Holder->Search, Info->ResultCode, Holder->Finished);
}

void EOS_CALL EOSUnifiedLobbyManager::OnLobbyInviteReceivedCallback(const EOS_Lobby_LobbyInviteReceivedCallbackInfo* Info)
{
    auto* Self = static_cast<EOSUnifiedLobbyManager*>(Info ? Info->ClientData : nullptr);
    if (!Self || !Info) return;

    UE_LOG(LogEOSUnifiedLobby, Log, TEXT("[Lobby] InviteReceived InviteId=%s FromPUID=%s ToLocalPUID=%s"),
        ToTChar(Info->InviteId), *PuidToString(Info->TargetUserId), *PuidToString(Info->LocalUserId));

    if (Self->OnLobbyInviteReceivedEvent)
        Self->OnLobbyInviteReceivedEvent(Info->InviteId ? Info->InviteId : "");
}

void EOS_CALL EOSUnifiedLobbyManager::OnLobbyUpdateReceived(const EOS_Lobby_LobbyUpdateReceivedCallbackInfo* Info)
{
    // No ResultCode in this payload; log what we have
    UE_LOG(LogEOSUnifiedLobby, Verbose, TEXT("[Lobby] LobbyUpdateReceived LobbyId=%s"),
        ToTChar(Info ? Info->LobbyId : nullptr));
}

void EOS_CALL EOSUnifiedLobbyManager::OnMemberUpdateReceived(const EOS_Lobby_LobbyMemberUpdateReceivedCallbackInfo* Info)
{
    // No ResultCode in this payload; log what we have
    UE_LOG(LogEOSUnifiedLobby, Verbose, TEXT("[Lobby] MemberUpdateReceived LobbyId=%s AffectedUser=%s"),
        ToTChar(Info ? Info->LobbyId : nullptr),
        *PuidToString(Info ? Info->TargetUserId : nullptr));
}
