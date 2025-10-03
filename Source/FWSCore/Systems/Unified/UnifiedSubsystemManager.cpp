#include "UnifiedSubsystemManager.h"

#include "FWSCore.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "FWSCore/EOS/EOSUnifiedSubsystem.h"
#include "FWSCore/Player/PlayerProfileComponent.h"
#include "FWSCore/Systems/Save/SaveSystemSubsystem.h"

// ===== Subsystem lifetime =====

void UUnifiedSubsystemManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (UGameInstance* GI = GetGameInstance())
	{
		EOS  = GI->GetSubsystem<UEOSUnifiedSubsystem>();
		Save = GI->GetSubsystem<USaveSystemSubsystem>();
	}

	// --- Bind EOS ---
	if (EOS)
	{
		EOS->OnAuthStateChanged.RemoveAll(this);
		EOS->OnAuthStateChanged.AddDynamic(this, &UUnifiedSubsystemManager::HandleAuthChanged);

		EOS->OnFriendsUpdated.RemoveAll(this);
		EOS->OnFriendsUpdated.AddDynamic(this, &UUnifiedSubsystemManager::HandleEOSFriendsUpdated);

		EOS->OnLobbySummariesUpdated.RemoveAll(this);
		EOS->OnLobbySummariesUpdated.AddDynamic(this, &UUnifiedSubsystemManager::HandleEOSLobbySummariesUpdated);

		bLoggedIn = EOS->IsLoggedIn();
		RefreshCachedAuthStrings();
		OnAuthChanged.Broadcast(bLoggedIn, FString());

		if (EOS && bAutoLoginOnStartup && !IsRunningDedicatedServer() && !bLoggedIn)
		{
			UE_LOG(LogUnified, Log, TEXT("[Unified] Auto-login on startup..."));
			TryAutoLogin();
		}
	}

	// --- Bind Save ---
	if (Save)
	{
		Save->InitializeSystem(false);

		Save->OnSaveStarted.RemoveAll(this);
		Save->OnSaveFinished.RemoveAll(this);
		Save->OnProfileChanged.RemoveAll(this);

		Save->OnSaveStarted.AddDynamic(this, &UUnifiedSubsystemManager::HandleSaveStarted);
		Save->OnSaveFinished.AddDynamic(this, &UUnifiedSubsystemManager::HandleSaveFinished);
		Save->OnProfileChanged.AddLambda([this](const FString NewSlot)
		{
			CachedSlotName = NewSlot;
			OnProfileChanged.Broadcast(NewSlot);
		});

		CachedSlotName = Save->GetCurrentSlotName();
	}

	// Bridge PlayerProfileComponent (local controller 0)
	WireProfileComponent();

	// Optional: auto resolve/load on startup if already logged in
	if (bLoggedIn && Save)
	{
		Save->ResolveLoadForAllLocalPlayers(/*bApplyAfterLoad*/true);
	}
}

void UUnifiedSubsystemManager::Deinitialize()
{
	if (EOS)
	{
		EOS->OnAuthStateChanged.RemoveAll(this);
		EOS->OnFriendsUpdated.RemoveAll(this);
		EOS->OnLobbySummariesUpdated.RemoveAll(this);
		EOS = nullptr;
	}

	if (Save)
	{
		Save->OnSaveStarted.RemoveAll(this);
		Save->OnSaveFinished.RemoveAll(this);
		Save->OnProfileChanged.RemoveAll(this);
		Save = nullptr;
	}

	if (ForwardedProfile)
	{
		ForwardedProfile->OnSettingsLoaded.RemoveAll(this);
		ForwardedProfile->OnSettingsApplied.RemoveAll(this);
		ForwardedProfile->OnProfileKeyChanged.RemoveAll(this);
		ForwardedProfile = nullptr;
	}

	Super::Deinitialize();
}

// ===== Auth API =====
void UUnifiedSubsystemManager::TryAutoLogin()
{
	if (!EOS)
	{
		UE_LOG(LogUnified, Warning, TEXT("[Unified] TryAutoLogin: EOS subsystem not ready"));
		return;
	}
	if (bLoggedIn)
	{
		UE_LOG(LogUnified, Verbose, TEXT("[Unified] TryAutoLogin: already logged in"));
		return;
	}

	// EOSUnifiedAuthManager::Login() tries RefreshToken first, then PersistentAuth.
	// This is a silent path (no portal UI).
	UE_LOG(LogUnified, Log, TEXT("[Unified] TryAutoLogin: invoking EOS->Login()"));
	EOS->Login();
}

void UUnifiedSubsystemManager::Login()       { if (EOS) EOS->Login(); }
void UUnifiedSubsystemManager::LoginPortal() { if (EOS) EOS->LoginViaPortal(); }
void UUnifiedSubsystemManager::Logout()      { if (EOS) EOS->Logout(); }
void UUnifiedSubsystemManager::ShowOverlay() { if (EOS) EOS->ShowOverlay(); }

void UUnifiedSubsystemManager::HandleAuthChanged(bool bNowLoggedIn, const FString& Message)
{
	bLoggedIn = bNowLoggedIn;
	RefreshCachedAuthStrings();
	OnAuthChanged.Broadcast(bLoggedIn, Message);

	// On login, kick profile resolve/load for local players
	if (bLoggedIn && Save)
	{
		Save->ResolveLoadForAllLocalPlayers(/*bApplyAfterLoad*/true);
	}

	// Controllers/components might have changed across maps/login → rewire
	WireProfileComponent();
}

void UUnifiedSubsystemManager::RefreshCachedAuthStrings()
{
	CachedEpicId.Empty();
	CachedProductUserId.Empty();

	if (EOS)
	{
		// Public getters in your EOS subsystem
		CachedEpicId       = EOS->GetLocalEpicAccountIdString();
		CachedProductUserId= EOS->GetProductUserIdString();
	}

	// Display name from profile settings (fallback that works offline/guest)
	CachedDisplayName.Empty();
	if (ForwardedProfile)
	{
		CachedDisplayName = ForwardedProfile->CurrentSettings.PreferredDisplayName;
	}
}

// ===== Save / Profiles =====

TArray<FString> UUnifiedSubsystemManager::GetAvailableProfiles() const
{
	return Save ? Save->GetAvailableProfiles() : TArray<FString>{};
}

void UUnifiedSubsystemManager::SwitchProfile(const FString& NewProfileName)
{
	if (!Save) return;
	Save->SwitchProfile(NewProfileName);
}

void UUnifiedSubsystemManager::AddNewProfile(const FString& NewProfileName)
{
	if (Save) Save->AddNewProfile(NewProfileName);
}

void UUnifiedSubsystemManager::DeleteProfile(const FString& ProfileName)
{
	if (Save) Save->DeleteProfile(ProfileName);
}

void UUnifiedSubsystemManager::RequestSave(bool bAsync)
{
	if (Save) Save->RequestSave(bAsync);
}

void UUnifiedSubsystemManager::RequestLoad(bool bAsync)
{
	if (Save) Save->RequestLoad(bAsync);
}

void UUnifiedSubsystemManager::HandleSaveStarted(const FString SlotName)
{
	CachedSlotName = SlotName;
	OnSaveStarted.Broadcast(SlotName);
}

void UUnifiedSubsystemManager::HandleSaveFinished(const FString SlotName, bool bSuccess)
{
	CachedSlotName = SlotName;
	OnSaveFinished.Broadcast(SlotName, bSuccess);
}

// ===== Settings bridge (PlayerProfileComponent) =====

UPlayerProfileComponent* UUnifiedSubsystemManager::GetOrCreateLocalPlayerProfile(int32 ControllerId) const
{
	if (!Save) return nullptr;

	UWorld* World = GetWorld();
	if (!World) return nullptr;

	APlayerController* PC = UGameplayStatics::GetPlayerController(World, ControllerId);
	if (!PC) return nullptr;

	return Save->EnsureProfileComponent(PC);
}

void UUnifiedSubsystemManager::WireProfileComponent()
{
	// Unbind old
	if (ForwardedProfile)
	{
		ForwardedProfile->OnSettingsLoaded.RemoveAll(this);
		ForwardedProfile->OnSettingsApplied.RemoveAll(this);
		ForwardedProfile->OnProfileKeyChanged.RemoveAll(this);
		ForwardedProfile = nullptr;
	}

	// Bind new (local player 0)
	ForwardedProfile = GetOrCreateLocalPlayerProfile(/*ControllerId*/0);
	if (!ForwardedProfile) return;

	ForwardedProfile->OnSettingsLoaded.AddDynamic(this, &UUnifiedSubsystemManager::HandleSettingsLoaded_Internal);
	ForwardedProfile->OnSettingsApplied.AddDynamic(this, &UUnifiedSubsystemManager::HandleSettingsApplied_Internal);
	ForwardedProfile->OnProfileKeyChanged.AddDynamic(this, &UUnifiedSubsystemManager::HandleProfileKeyChanged_Internal);

	// Refresh display name cache from profile settings
	CachedDisplayName = ForwardedProfile->CurrentSettings.PreferredDisplayName;
}

void UUnifiedSubsystemManager::HandleSettingsLoaded_Internal(const FPlayerSettings& Settings)
{
	CachedDisplayName = Settings.PreferredDisplayName;
	OnSettingsLoaded.Broadcast(Settings);
}

void UUnifiedSubsystemManager::HandleSettingsApplied_Internal(const FPlayerSettings& Settings)
{
	CachedDisplayName = Settings.PreferredDisplayName;
	OnSettingsApplied.Broadcast(Settings);
}

void UUnifiedSubsystemManager::HandleProfileKeyChanged_Internal(const FSaveProfileKey& Key)
{
	OnProfileKeyChanged.Broadcast(Key);
}

// ===== Friends (EOS pass-through) =====

void UUnifiedSubsystemManager::QueryFriends()
{
	if (EOS) EOS->QueryFriends();
}

void UUnifiedSubsystemManager::GetCachedFriendsBP(TArray<FEOSFriendView>& Out) const
{
	Out.Reset();
	if (!EOS) return;
	Out = EOS->GetCachedFriends();
}

void UUnifiedSubsystemManager::HandleEOSFriendsUpdated(const TArray<FEOSFriendView>& Friends)
{
	OnFriendsUpdated.Broadcast(Friends);
}

// ===== Lobby (EOS pass-through) =====

void UUnifiedSubsystemManager::CreateLobby()
{
	if (!EOS) { OnLobbyCreated.Broadcast(false, TEXT("EOS not ready")); return; }
	EOS->CreateLobby();
	OnLobbyCreated.Broadcast(true, TEXT("")); // keep the host flow snappy
}
void UUnifiedSubsystemManager::SearchLobbies_ByBucket()      { if (EOS) EOS->SearchLobbies_ByBucket(); }
void UUnifiedSubsystemManager::SearchLobbies_All()           { if (EOS) EOS->SearchLobbies_All(); }
void UUnifiedSubsystemManager::SearchLobbies_ByName(const FString& Name)        { if (EOS) EOS->SearchLobbies_ByName(Name); }
void UUnifiedSubsystemManager::JoinLobby(const FString& LobbyId)
{
	if (EOS) EOS->JoinLobby(LobbyId);
	// Fire a lightweight signal so UI can reflect "joined" state quickly.
	OnLobbyJoined.Broadcast();
}
void UUnifiedSubsystemManager::LeaveLobby()
{
	if (EOS) EOS->LeaveLobby();
	OnLobbyLeftOrDestroyed.Broadcast();
}
void UUnifiedSubsystemManager::AcceptLobbyInvite(const FString& InviteId){ if (EOS) EOS->AcceptLobbyInvite(InviteId); }
void UUnifiedSubsystemManager::RejectLobbyInvite(const FString& InviteId){ if (EOS) EOS->RejectLobbyInvite(InviteId); }

void UUnifiedSubsystemManager::GetCachedLobbySummariesBP(TArray<FEOSLobbySummaryBP>& Out) const
{
	Out.Reset();
	if (!EOS) return;
	EOS->GetCachedLobbySummaries(Out);
}

void UUnifiedSubsystemManager::HandleEOSLobbySummariesUpdated(const TArray<FEOSLobbySummaryBP>& Lobbies)
{
	// Existing EOS-shaped event (keep if you expose it)
	OnLobbySummariesUpdated.Broadcast(Lobbies);

	// Wrapped unified event
	TArray<FUnifiedLobbySummary> Wrapped;
	Wrapped.Reserve(Lobbies.Num());
	for (const FEOSLobbySummaryBP& S : Lobbies)
	{
		FUnifiedLobbySummary U;
		U.LobbyId         = S.LobbyId;
		U.Name            = S.Name;
		U.Map             = S.Map;
		U.Mode            = S.Mode;
		U.MaxMembers      = S.MaxMembers;
		U.MemberCount     = S.MemberCount;
		U.bPresenceEnabled= S.bPresenceEnabled;
		U.bAllowInvites   = S.bAllowInvites;
		Wrapped.Add(MoveTemp(U));
	}
	OnLobbySummariesUpdated_U.Broadcast(Wrapped);
	OnLobbyUpdated.Broadcast();
}

void UUnifiedSubsystemManager::GetCachedLobbySummaries_U(TArray<FUnifiedLobbySummary>& Out) const
{
	Out.Reset();
	if (!EOS) return;

	TArray<FEOSLobbySummaryBP> Tmp;
	EOS->GetCachedLobbySummaries(Tmp);

	Out.Reserve(Tmp.Num());
	for (const FEOSLobbySummaryBP& S : Tmp)
	{
		FUnifiedLobbySummary U;
		U.LobbyId         = S.LobbyId;
		U.Name            = S.Name;
		U.Map             = S.Map;
		U.Mode            = S.Mode;
		U.MaxMembers      = S.MaxMembers;
		U.MemberCount     = S.MemberCount;
		U.bPresenceEnabled= S.bPresenceEnabled;
		U.bAllowInvites   = S.bAllowInvites;
		Out.Add(MoveTemp(U));
	}
}

bool UUnifiedSubsystemManager::UpdateApplySaveSettings(const FPlayerSettings& NewSettings, bool bApply /*=true*/, bool bSave /*=true*/)
{
	LastSettings = NewSettings;
	HasSettings  = true;

	UE_LOG(LogUnified, Log, TEXT("[Unified] UpdateApplySaveSettings: Apply=%s Save=%s"),
		bApply ? TEXT("true") : TEXT("false"),
		bSave  ? TEXT("true") : TEXT("false"));

	// Tell listeners we have a fresh set (and optionally that it’s applied)
	OnSettingsLoaded.Broadcast(NewSettings);
	if (bApply)
	{
		OnSettingsApplied.Broadcast(NewSettings);
	}

	// Persist if requested (async by default)
	if (bSave && Save)
	{
		Save->RequestSave(/*bAsync*/true);
	}
	return true;
}

bool UUnifiedSubsystemManager::GetCurrentSettings(FPlayerSettings& OutSettings) const
{
	if (!HasSettings)
	{
		UE_LOG(LogUnified, Verbose, TEXT("[Unified] GetCurrentSettings: no cached settings yet."));
		return false;
	}

	OutSettings = LastSettings;
	return true;
}

void UUnifiedSubsystemManager::CreateLobbyWithParams(const FString& Name, const FString& Map, const FString& Mode,
													 int32 MaxMembers, bool bPresence, bool bAllowInvites)
{
	if (!EOS)
	{
		UE_LOG(LogUnified, Warning, TEXT("[Unified] CreateLobbyWithParams: EOS not ready"));
		return;
	}
	// If your EOS layer supports attributes, set them; otherwise fall back:
	// EOS->CreateLobby(...); EOS->SetLobbyAttribute("name", Name) ... etc.
	CreateLobby(); // fallback to your existing simple create
}

void UUnifiedSubsystemManager::InviteToLobby()
{
	if (EOS) EOS->InviteToLobby();   // opens EOS overlay invite UI (your EOS wrapper already exposes this)
}