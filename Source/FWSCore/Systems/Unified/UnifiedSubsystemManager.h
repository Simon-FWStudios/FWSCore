// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// UnifiedSubsystemManager.h
#include "CoreMinimal.h"
#include "FWSCore/Shared/FWSTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UnifiedSubsystemManager.generated.h"

class UEOSUnifiedSubsystem;
class USaveSystemSubsystem;
class UPlayerProfileComponent;

/** Auth changed (mirrors EOS subsystem) */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnUnifiedAuthChanged, bool, bLoggedIn, const FString&, Message);

/** Save/profile events */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnUnifiedProfileChangedBP, FString, NewSlot);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnUnifiedSaveStarted, FString, SlotName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnUnifiedSaveFinished, FString, SlotName, bool, bSuccess);

/** Player settings/profile component events */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnUnifiedSettingsLoaded,  FPlayerSettings, Settings);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnUnifiedSettingsApplied, FPlayerSettings, Settings);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnUnifiedProfileKeyChangedBP, FSaveProfileKey, Key);

/** Friends / Lobby mirrors */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnUnifiedFriendsUpdated, const TArray<FEOSFriendView>&, Friends);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnUnifiedLobbySummariesUpdated, const TArray<FEOSLobbySummaryBP>&, Lobbies);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnUnifiedLobbySummariesUpdated_U, const TArray<FUnifiedLobbySummary>&, Lobbies);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnUnifiedLobbyCreated, bool, bSuccess, const FString&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnUnifiedLobbyJoined);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnUnifiedLobbyUpdated);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnUnifiedLobbyLeftOrDestroyed);

/**
 * UUnifiedSubsystemManager
 * Single BP-facing facade for EOS (auth/friends/lobbies) + Save/Profile.
 */
UCLASS(Config=Game, DefaultConfig)
//Config/DefaultGame.ini
//[/Script/UnifiedSubsystemManager.UnifiedSubsystemManager]
//bAutoLoginOnStartup=True
class FWSCORE_API UUnifiedSubsystemManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// ===== Subsystem lifetime =====
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ===== Auth (EOS) =====
	UFUNCTION(BlueprintPure,  Category="Unified|Auth") bool   IsLoggedIn() const { return bLoggedIn; }
	UFUNCTION(BlueprintCallable, Category="Unified|Auth") void Login();
	UFUNCTION(BlueprintCallable, Category="Unified|Auth") void LoginPortal();
	UFUNCTION(BlueprintCallable, Category="Unified|Auth") void Logout();
	UFUNCTION(BlueprintCallable, Category="Unified|Auth") void ShowOverlay();

	/** From EOS subsystem */
	UFUNCTION(BlueprintPure,  Category="Unified|Auth") FString GetEpicAccountId()  const { return CachedEpicId; }
	UFUNCTION(BlueprintPure,  Category="Unified|Auth") FString GetProductUserId()  const { return CachedProductUserId; }

	/** Display name: from PlayerProfile settings (works online/offline, split-screen) */
	UFUNCTION(BlueprintPure,  Category="Unified|Auth") FString GetDisplayName() const { return CachedDisplayName; }

	// ===== Save / Profiles (slots) =====
	UFUNCTION(BlueprintPure,  Category="Unified|Save") FString GetCurrentSlotName() const { return CachedSlotName; }
	UFUNCTION(BlueprintCallable, Category="Unified|Save") TArray<FString> GetAvailableProfiles() const;
	UFUNCTION(BlueprintCallable, Category="Unified|Save") void SwitchProfile(const FString& NewProfileName);
	UFUNCTION(BlueprintCallable, Category="Unified|Save") void AddNewProfile(const FString& NewProfileName);
	UFUNCTION(BlueprintCallable, Category="Unified|Save") void DeleteProfile(const FString& ProfileName);
	UFUNCTION(BlueprintCallable, Category="Unified|Save") void RequestSave(bool bAsync = true);
	UFUNCTION(BlueprintCallable, Category="Unified|Save") void RequestLoad(bool bAsync = true);

	// ===== Settings (bridged to PlayerProfileComponent) =====
	UFUNCTION(BlueprintCallable, Category="Unified|Settings")
	bool UpdateApplySaveSettings(const FPlayerSettings& NewSettings, bool bApply = true, bool bSave = true);

	UFUNCTION(BlueprintPure, Category="Unified|Settings")
	bool GetCurrentSettings(FPlayerSettings& OutSettings) const;

	// ===== Friends (EOS pass-through) =====
	UFUNCTION(BlueprintCallable, Category="Unified|Friends") void QueryFriends();
	UFUNCTION(BlueprintCallable, Category="Unified|Friends") void GetCachedFriendsBP(UPARAM(ref) TArray<FEOSFriendView>& Out) const;

	// ===== Lobby (EOS pass-through) =====
	UFUNCTION(BlueprintCallable, Category="Unified|Lobby") void CreateLobby();
	UFUNCTION(BlueprintCallable, Category="Unified|Lobby") void SearchLobbies_ByBucket();
	UFUNCTION(BlueprintCallable, Category="Unified|Lobby") void SearchLobbies_All();
	UFUNCTION(BlueprintCallable, Category="Unified|Lobby") void SearchLobbies_ByName(const FString& Name);   // note: no parameter in your header
	UFUNCTION(BlueprintCallable, Category="Unified|Lobby") void JoinLobby(const FString& LobbyId);
	UFUNCTION(BlueprintCallable, Category="Unified|Lobby") void LeaveLobby();
	UFUNCTION(BlueprintCallable, Category="Unified|Lobby") void AcceptLobbyInvite(const FString& InviteId);
	UFUNCTION(BlueprintCallable, Category="Unified|Lobby") void RejectLobbyInvite(const FString& InviteId);
	UFUNCTION(BlueprintCallable, Category="Unified|Lobby") void GetCachedLobbySummariesBP(UPARAM(ref) TArray<FEOSLobbySummaryBP>& Out) const;
	UFUNCTION(BlueprintCallable, Category="Unified|Lobby")
	void GetCachedLobbySummaries_U(UPARAM(ref) TArray<FUnifiedLobbySummary>& Out) const;
	UFUNCTION(BlueprintCallable, Category="Unified|Lobby")
	void CreateLobbyWithParams(const FString& Name, const FString& Map, const FString& Mode,
							   int32 MaxMembers, bool bPresence, bool bAllowInvites);
	UPROPERTY(BlueprintAssignable) FOnUnifiedLobbyCreated OnLobbyCreated;
	void NotifyLobbyCreatedSuccess() { OnLobbyCreated.Broadcast(true, TEXT("")); }
	void NotifyLobbyCreatedError(const FString& Err) { OnLobbyCreated.Broadcast(false, Err); }
	UFUNCTION(BlueprintCallable, Category="Unified|Lobby")
	void InviteToLobby();
	
	// ===== Accessors =====
	UFUNCTION(BlueprintPure, Category="Unified|Access") UEOSUnifiedSubsystem* GetEOS() const { return EOS; }
	UFUNCTION(BlueprintPure, Category="Unified|Access") USaveSystemSubsystem* GetSave() const { return Save; }
	UFUNCTION(BlueprintCallable, Category="Unified|Access") UPlayerProfileComponent* GetOrCreateLocalPlayerProfile(int32 ControllerId = 0) const;

	// ===== Events (sticky) =====
	UPROPERTY(BlueprintAssignable, Category="Unified|Events") FOnUnifiedAuthChanged            OnAuthChanged;
	UPROPERTY(BlueprintAssignable, Category="Unified|Events") FOnUnifiedProfileChangedBP      OnProfileChanged;
	UPROPERTY(BlueprintAssignable, Category="Unified|Events") FOnUnifiedSaveStarted           OnSaveStarted;
	UPROPERTY(BlueprintAssignable, Category="Unified|Events") FOnUnifiedSaveFinished          OnSaveFinished;
	UPROPERTY(BlueprintAssignable, Category="Unified|Events") FOnUnifiedSettingsLoaded        OnSettingsLoaded;
	UPROPERTY(BlueprintAssignable, Category="Unified|Events") FOnUnifiedSettingsApplied       OnSettingsApplied;
	UPROPERTY(BlueprintAssignable, Category="Unified|Events") FOnUnifiedProfileKeyChangedBP   OnProfileKeyChanged;
	UPROPERTY(BlueprintAssignable, Category="Unified|Events") FOnUnifiedFriendsUpdated        OnFriendsUpdated;
	UPROPERTY(BlueprintAssignable, Category="Unified|Events") FOnUnifiedLobbySummariesUpdated    OnLobbySummariesUpdated;     // existing
	UPROPERTY(BlueprintAssignable, Category="Unified|Events") FOnUnifiedLobbySummariesUpdated_U  OnLobbySummariesUpdated_U;   // new
	UPROPERTY(BlueprintAssignable, Category="Unified|Events")
	FOnUnifiedLobbyJoined OnLobbyJoined;

	UPROPERTY(BlueprintAssignable, Category="Unified|Events")
	FOnUnifiedLobbyUpdated OnLobbyUpdated;

	UPROPERTY(BlueprintAssignable, Category="Unified|Events")
	FOnUnifiedLobbyLeftOrDestroyed OnLobbyLeftOrDestroyed;
	
	UFUNCTION(BlueprintCallable, Category="Unified|Auth") void TryAutoLogin();

	// Configurable behaviour (DefaultGame.ini → [UnifiedSubsystemManager])
	UPROPERTY(Config, EditDefaultsOnly, Category="Unified|Auth") bool bAutoLoginOnStartup = true;
private:
	// ---- Internal handlers ----
	UFUNCTION() void HandleAuthChanged(bool bNowLoggedIn, const FString& Message);
	UFUNCTION() void HandleSaveStarted(const FString SlotName);
	UFUNCTION() void HandleSaveFinished(const FString SlotName, bool bSuccess);
	UFUNCTION() void HandleSettingsLoaded_Internal(const FPlayerSettings& Settings);
	UFUNCTION() void HandleSettingsApplied_Internal(const FPlayerSettings& Settings);
	UFUNCTION() void HandleProfileKeyChanged_Internal(const FSaveProfileKey& Key);
	UFUNCTION() void HandleEOSFriendsUpdated(const TArray<FEOSFriendView>& Friends);
	UFUNCTION() void HandleEOSLobbySummariesUpdated(const TArray<FEOSLobbySummaryBP>& Lobbies);

	// ---- Helpers ----
	void RefreshCachedAuthStrings();   // pulls ids from EOS, display name from profile comp
	void WireProfileComponent();       // subscribes to UPlayerProfileComponent events

private:
	// Cached subsystems (GI-level)
	UPROPERTY(Transient) UEOSUnifiedSubsystem* EOS  = nullptr;
	UPROPERTY(Transient) USaveSystemSubsystem* Save = nullptr;

	// Sticky state
	UPROPERTY(VisibleAnywhere, Category="Unified|State") bool    bLoggedIn = false;
	UPROPERTY(VisibleAnywhere, Category="Unified|State") FString CachedEpicId;
	UPROPERTY(VisibleAnywhere, Category="Unified|State") FString CachedProductUserId;
	UPROPERTY(VisibleAnywhere, Category="Unified|State") FString CachedDisplayName;   // from PlayerProfile
	UPROPERTY(VisibleAnywhere, Category="Unified|State") FString CachedSlotName;

	// Forwarded local profile component (controller 0 by default)
	UPROPERTY(Transient) UPlayerProfileComponent* ForwardedProfile = nullptr;
	FPlayerSettings LastSettings;
	bool HasSettings = false;
};
