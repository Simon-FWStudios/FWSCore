#include "EOSUnifiedSubsystem.h"
#include "FWSCore.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Async/Async.h"

#include <eos_sdk.h>
#include <eos_common.h>

DEFINE_LOG_CATEGORY_STATIC(LogEOSUnifiedSubsystemCpp, Log, All);

// ---------------- local helpers ----------------

static FString EAID_ToString(EOS_EpicAccountId Id)
{
	if (!Id) return TEXT("");
	char Buf[EOS_EPICACCOUNTID_MAX_LENGTH + 1] = {};
	int32_t Len = (int32_t)sizeof(Buf);
	return (EOS_EpicAccountId_ToString(Id, Buf, &Len) == EOS_EResult::EOS_Success) ? UTF8_TO_TCHAR(Buf) : TEXT("");
}

static FString PUID_ToString(EOS_ProductUserId Id)
{
	if (!Id) return TEXT("");
	char Buf[EOS_PRODUCTUSERID_MAX_LENGTH + 1] = {};
	int32_t Len = (int32_t)sizeof(Buf);
	return (EOS_ProductUserId_ToString(Id, Buf, &Len) == EOS_EResult::EOS_Success) ? UTF8_TO_TCHAR(Buf) : TEXT("");
}

// ---------------- UGameInstanceSubsystem ----------------

void UEOSUnifiedSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UE_LOG(LogEOSUnifiedSubsystemCpp, Log, TEXT("[EOS] Subsystem Initialize"));

	// Boot the core system (creates/adopts platform via AuthManager)
	if (!System.Initialize())
	{
		UE_LOG(LogEOSUnifiedSubsystemCpp, Error, TEXT("[EOS] System.Initialize failed"));
	}

	// Bridge Auth signal to BP and (re)bind manager callbacks when ready
	System.GetAuthManager().OnLoginStateChanged = [this](bool bLoggedIn, const std::string& Msg)
	{
		AsyncTask(ENamedThreads::GameThread, [this, bLoggedIn, MsgStr = FString(Msg.c_str())]()
		{
			OnAuthStateChanged.Broadcast(bLoggedIn, MsgStr);

			// When login succeeds, ensure we have managers and hooks; on logout, clear caches.
			if (bLoggedIn)
			{
				RecreateManagersIfPossible();
				BindFriendsCallbacks();
				BindLobbyCallbacks();

				// Hook display name updates (optional)
				System.GetAuthManager().OnAuthDisplayNameCached =
					[this](const FString& Name)
					{
						AsyncTask(ENamedThreads::GameThread, [this, Name]()
						{
							OnDisplayNameUpdated.Broadcast(Name);
						});
					};
			}
			else
			{
				CachedFriendsBP.Reset();
				CachedLobbySummariesBP.Reset();
				OnFriendsUpdated.Broadcast(CachedFriendsBP);
				OnLobbySummariesUpdated.Broadcast(CachedLobbySummariesBP);
			}
		});
	};

	// Start the core ticker
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([this](float DeltaSeconds)
		{
			this->TickEOS(DeltaSeconds);
			return true;
		}),
		0.0f);
	UE_LOG(LogEOSUnified, Log, TEXT("[UEOSUnifiedSubsystem] Ticker registered (per-frame EOS tick)."));
}

void UEOSUnifiedSubsystem::Deinitialize()
{
	UE_LOG(LogEOSUnifiedSubsystemCpp, Log, TEXT("[EOS] Subsystem Deinitialize"));

	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
		UE_LOG(LogEOSUnified, Log, TEXT("[UEOSUnifiedSubsystem] Ticker removed."));
	}

	System.Shutdown();

	Super::Deinitialize();
}

// ---------------- ticker ----------------

bool UEOSUnifiedSubsystem::Tick(float DeltaTime)
{
	TickEOS(DeltaTime);
	return true; // keep ticking
}

void UEOSUnifiedSubsystem::TickEOS(float /*DeltaSeconds*/)
{
	System.Tick();
}

// ---------------- BP: Auth ----------------

void UEOSUnifiedSubsystem::Login()
{
	System.GetAuthManager().Login();
}

void UEOSUnifiedSubsystem::LoginViaPortal()
{
	System.GetAuthManager().LoginAccountPortal();
}

void UEOSUnifiedSubsystem::Logout()
{
	System.GetAuthManager().Logout();
}

void UEOSUnifiedSubsystem::HardLogout()
{
	System.GetAuthManager().HardLogout(/*done*/nullptr);
}

// ---------------- BP: Convenience ----------------

bool UEOSUnifiedSubsystem::IsLoggedIn() const
{
	return System.GetEpicAccountId() != nullptr;
}

FString UEOSUnifiedSubsystem::GetLocalEpicAccountIdString() const
{
	return EAID_ToString(System.GetEpicAccountId());
}

FString UEOSUnifiedSubsystem::GetProductUserIdString() const
{
	return PUID_ToString(System.GetProductUserId());
}

// ---------------- Friends ----------------

void UEOSUnifiedSubsystem::QueryFriends()
{
	if (auto* FM = System.GetFriendsManager())
	{
		FM->QueryFriends();
	}
}

TArray<FEOSFriendView> UEOSUnifiedSubsystem::GetCachedFriends() const
{
	return CachedFriendsBP;
}

void UEOSUnifiedSubsystem::SendFriendInvite(const FString& EpicAccountIdStr)
{
	if (auto* FM = System.GetFriendsManager())
	{
		EOS_EpicAccountId Target = EOS_EpicAccountId_FromString(TCHAR_TO_UTF8(*EpicAccountIdStr));
		if (Target) FM->SendInvite(Target);
	}
}

void UEOSUnifiedSubsystem::AcceptInvite(const FString& EpicAccountIdStr)
{
	if (auto* FM = System.GetFriendsManager())
	{
		EOS_EpicAccountId Target = EOS_EpicAccountId_FromString(TCHAR_TO_UTF8(*EpicAccountIdStr));
		if (Target) FM->AcceptInvite(Target);
	}
}

void UEOSUnifiedSubsystem::RejectInvite(const FString& EpicAccountIdStr)
{
	if (auto* FM = System.GetFriendsManager())
	{
		EOS_EpicAccountId Target = EOS_EpicAccountId_FromString(TCHAR_TO_UTF8(*EpicAccountIdStr));
		if (Target) FM->RejectInvite(Target);
	}
}

// ---------------- Lobby ----------------

void UEOSUnifiedSubsystem::CreateLobby()
{
	if (auto* LM = System.GetLobbyManager())
	{
		LM->CreateLobby(/*finishedFlag*/nullptr);
	}
}

void UEOSUnifiedSubsystem::SearchLobbies_ByBucket()
{
	// Example bucketed search (presence-enabled); aligns with basic sample behavior.
	if (auto* LM = System.GetLobbyManager())
	{
		LM->SearchLobbies(/*finishedFlag*/nullptr);
	}
}

void UEOSUnifiedSubsystem::SearchLobbies_All()
{
	if (auto* LM = System.GetLobbyManager())
	{
		LM->SearchAllLobbies(/*finishedFlag*/nullptr);
	}
}

void UEOSUnifiedSubsystem::JoinLobby(const FString& LobbyId)
{
	if (auto* LM = System.GetLobbyManager())
	{
		LM->JoinLobby(TCHAR_TO_UTF8(*LobbyId), /*finishedFlag*/nullptr);
	}
}

void UEOSUnifiedSubsystem::LeaveLobby()
{
	if (auto* LM = System.GetLobbyManager())
	{
		LM->LeaveLobby(/*finishedFlag*/nullptr);
	}
}

void UEOSUnifiedSubsystem::InviteToLobby()
{
	// Simple helper to open Friends overlay for invites (if supported by platform/overlay)
	if (auto* LM = System.GetLobbyManager())
	{
		LM->ShowInviteOverlay();
	}
}

void UEOSUnifiedSubsystem::AcceptLobbyInvite(const FString& InviteId)
{
	if (auto* LM = System.GetLobbyManager())
	{
		LM->AcceptInvite(TCHAR_TO_UTF8(*InviteId), /*finishedFlag*/nullptr);
	}
}

void UEOSUnifiedSubsystem::RejectLobbyInvite(const FString& InviteId)
{
	if (auto* LM = System.GetLobbyManager())
	{
		LM->RejectInvite(TCHAR_TO_UTF8(*InviteId), /*finishedFlag*/nullptr);
	}
}

void UEOSUnifiedSubsystem::GetCachedLobbySummaries(TArray<FEOSLobbySummaryBP>& OutSummaries) const
{
	OutSummaries = CachedLobbySummariesBP;
}

// ---------------- Overlay ----------------

void UEOSUnifiedSubsystem::ShowOverlay()
{
	// Route to Friends overlay (most common entry point)
	if (auto* FM = System.GetFriendsManager())
	{
		FM->ShowOverlay();
	}
}

// ---------------- internal wiring ----------------

void UEOSUnifiedSubsystem::RecreateManagersIfPossible()
{
	auto& Auth = System.GetAuthManager();
	EOS_HPlatform       Plat = Auth.GetPlatformHandle();
	EOS_EpicAccountId   Epic = Auth.GetUserId();
	EOS_ProductUserId   Puid = Auth.GetProductUserId();

	const FString EpicStr = EOSUnified::EpicIdToString(Epic);
	const FString PuidStr = EOSUnified::ProductUserIdToString_Safe(Puid);

	UE_LOG(LogEOSUnified, Log, TEXT("[UEOSUnifiedSubsystem] RecreateManagersIfPossible(Plat=%p, EAID=%s, PUID=%s)"),
		Plat, *EpicStr, *PuidStr);

	if (Plat && (Epic || Puid))
	{
		System.CreateManagers(Plat, Epic, Puid); // creates Friends + Lobby if PUID valid
		BindFriendsCallbacks();
		BindLobbyCallbacks();

		RefreshFriendsCacheFromManager();
		OnFriendsUpdated.Broadcast(CachedFriendsBP);

		// Immediately rebuild lobbies cache (in case we were already in a lobby)
		RebuildLobbySummariesCache();
	}
}

void UEOSUnifiedSubsystem::RefreshFriendsCacheFromManager()
{
	CachedFriendsBP.Reset();

	auto* FM = System.GetFriendsManager();
	if (!FM) return;

	const auto& Friends = FM->GetFriends();
	CachedFriendsBP.Reserve((int32)Friends.size());

	for (const auto& F : Friends)
	{
		FEOSFriendView V;
		V.DisplayName   = UTF8_TO_TCHAR(F.DisplayName.c_str());
		V.EpicAccountId = UTF8_TO_TCHAR(F.EpicIdStr.c_str());
		V.ProductUserId = PUID_ToString(F.ProductId);
		V.Status        = (int32)F.Status;
		V.StatusText    = EOSUnified::FriendStatusToString(V.Status);
		V.Presence      = (int32)F.Presence;
		V.PresenceText  = EOSUnified::PresenceStatusToString(V.Presence);

		CachedFriendsBP.Add(MoveTemp(V));
	}
}

void UEOSUnifiedSubsystem::GetLobbies(TArray<FEOSLobbySummaryBP>& OutSummaries) const
{
	OutSummaries.Reset();

	auto* LM = System.GetLobbyManager();
	if (!LM) return;

	std::vector<FEOSLobbySummary> Native;
	LM->GetCachedSummaries(Native);

	OutSummaries.Reserve((int32)Native.size());
	for (const auto& S : Native)
	{
		FEOSLobbySummaryBP BP;
		BP.LobbyId         = UTF8_TO_TCHAR(S.LobbyId.c_str());
		BP.Name            = UTF8_TO_TCHAR(S.Name.c_str());
		BP.Map             = UTF8_TO_TCHAR(S.Map.c_str());
		BP.Mode            = UTF8_TO_TCHAR(S.Mode.c_str());
		BP.MaxMembers      = (int32)S.MaxMembers;
		BP.MemberCount     = (int32)S.MemberCount;
		BP.bPresenceEnabled= S.bPresenceEnabled;
		BP.bAllowInvites   = S.bAllowInvites;

		OutSummaries.Add(MoveTemp(BP));
	}
}

void UEOSUnifiedSubsystem::RebuildLobbySummariesCache()
{
	GetLobbies(CachedLobbySummariesBP);
	OnLobbySummariesUpdated.Broadcast(CachedLobbySummariesBP);
}

void UEOSUnifiedSubsystem::BindFriendsCallbacks()
{
	auto* Friends = System.GetFriendsManager();
	if (!Friends) return;

	Friends->OnFriendsListUpdated = [this](const std::vector<std::string>& /*Labels*/)
	{
		AsyncTask(ENamedThreads::GameThread, [this]()
		{
			RefreshFriendsCacheFromManager();
			UE_LOG(LogEOSUnified, Log, TEXT("[UEOSUnifiedSubsystem] Friends updated; count=%d"), CachedFriendsBP.Num());
			OnFriendsUpdated.Broadcast(CachedFriendsBP);
		});
	};
}

void UEOSUnifiedSubsystem::BindLobbyCallbacks()
{
auto* Lobby = System.GetLobbyManager();
    if (!Lobby) return;

    // Search results -> rebuild BP cache + broadcast
    Lobby->OnSearchResultsUpdated = [this](const std::vector<FEOSLobbySummary>&)
    {
        AsyncTask(ENamedThreads::GameThread, [this]()
        {
            RebuildLobbySummariesCache();
            UE_LOG(LogEOSUnified, Log, TEXT("[Subsystem] Lobby search results updated; count=%d"), CachedLobbySummariesBP.Num());
            OnLobbySummariesUpdated.Broadcast(CachedLobbySummariesBP);
        });
    };

    // Joined lobby -> rebuild and inform UI (route to 'OnLobbyJoined' BP event if you want)
    Lobby->OnJoinedLobby = [this](const FEOSLobbySummary& /*Summary*/)
    {
        AsyncTask(ENamedThreads::GameThread, [this]()
        {
            RebuildLobbySummariesCache();
            UE_LOG(LogEOSUnified, Log, TEXT("[Subsystem] OnJoinedLobby -> cache rebuilt; count=%d"), CachedLobbySummariesBP.Num());
            // Optional: OnLobbyJoined.Broadcast( FString(UTF8_TO_TCHAR(Summary.LobbyId.c_str())) );
            OnLobbySummariesUpdated.Broadcast(CachedLobbySummariesBP);
        });
    };

    // Left/destroyed -> rebuild and inform UI
    Lobby->OnLeftLobby = [this](const std::string& LobbyId)
    {
        AsyncTask(ENamedThreads::GameThread, [this, Id = FString(UTF8_TO_TCHAR(LobbyId.c_str()))]()
        {
            RebuildLobbySummariesCache();
            UE_LOG(LogEOSUnified, Log, TEXT("[Subsystem] OnLeftLobby(%s) -> cache rebuilt; count=%d"), *Id, CachedLobbySummariesBP.Num());
            // Optional: OnLobbyLeftOrDestroyed.Broadcast(Id);
            OnLobbySummariesUpdated.Broadcast(CachedLobbySummariesBP);
        });
    };

    // Invites -> forward to UI
    Lobby->OnLobbyInviteReceivedEvent = [this](const std::string& InviteId)
    {
        AsyncTask(ENamedThreads::GameThread, [this, I = FString(UTF8_TO_TCHAR(InviteId.c_str()))]()
        {
            UE_LOG(LogEOSUnified, Log, TEXT("[Subsystem] OnLobbyInviteReceivedEvent: %s"), *I);
            OnLobbyInviteReceived.Broadcast(I, GetProductUserIdString()); // or sender if you track it
        });
    };
}

void UEOSUnifiedSubsystem::SearchLobbies_ByName(const FString& NameFilter)
{
	if (auto* LM = System.GetLobbyManager())
	{
		// Use the manager’s generalized filter path to avoid hardcoded “TestLobby”
		std::atomic<bool> Done{false};
		EOSUnifiedLobbyManager::FSearchFilter f;
		f.Key = "Name"; f.Value = TCHAR_TO_UTF8(*NameFilter);
		f.Op  = EOS_EComparisonOp::EOS_CO_CONTAINS;

		LM->SearchWithFilters({ f }, /*maxResults*/50, &Done);
		UE_LOG(LogEOSUnified, Log, TEXT("[UEOSUnifiedSubsystem] SearchLobbies_ByName('%s') dispatched."), *NameFilter);
	}
}

FString UEOSUnifiedSubsystem::GetCachedDisplayName() const
{
	return System.GetAuthManager().GetCachedDisplayName();
}

int32 UEOSUnifiedSubsystem::GetLocalUserNum() const
{
	return System.GetAuthManager().GetLocalUserNum();
}
FString UEOSUnifiedSubsystem::GetEpicAccountIdString() const
{
	return System.GetAuthManager().GetEpicAccountIdString() ;
}
FString UEOSUnifiedSubsystem::GetDeploymentOrSandboxId() const
{
	return  System.GetAuthManager().GetDeploymentOrSandboxId();
}