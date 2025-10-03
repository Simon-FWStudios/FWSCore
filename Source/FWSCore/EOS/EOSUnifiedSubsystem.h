// EOSUnifiedSubsystem.h — Drop-in replacement (restores full BP API & delegates)
// Matches UEOSUnifiedTestWidget expectations for callables and events.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"

#include "EOSUnifiedSystem.h"
#include "EOSUnifiedHelpers.h"
#include "EOSUnifiedSubsystem.generated.h"

// ---------- BP data views ----------

USTRUCT(BlueprintType)
struct FEOSFriendView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString DisplayName;
	UPROPERTY(BlueprintReadOnly) FString EpicAccountId;   // canonical string
	UPROPERTY(BlueprintReadOnly) FString ProductUserId;   // canonical string (may be empty if not mapped)
	UPROPERTY(BlueprintReadOnly) int32   Status = 0;
	UPROPERTY(BlueprintReadOnly) FString StatusText;
	UPROPERTY(BlueprintReadOnly) int32   Presence = 0;
	UPROPERTY(BlueprintReadOnly) FString PresenceText;
};

// Mirrors FEOSLobbySummary (native) but BP-friendly
USTRUCT(BlueprintType)
struct FEOSLobbySummaryBP
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString LobbyId;
	UPROPERTY(BlueprintReadOnly) FString Name;
	UPROPERTY(BlueprintReadOnly) FString Map;
	UPROPERTY(BlueprintReadOnly) FString Mode;
	UPROPERTY(BlueprintReadOnly) int32   MaxMembers = 0;
	UPROPERTY(BlueprintReadOnly) int32   MemberCount = 0;
	UPROPERTY(BlueprintReadOnly) bool    bPresenceEnabled = false;
	UPROPERTY(BlueprintReadOnly) bool    bAllowInvites    = true;
};

// ---------- BP events ----------

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAuthStateChanged, bool, bLoggedIn, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFriendsUpdated, const TArray<FEOSFriendView>&, Friends);

// Primary lobby feed for UI
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLobbySummariesUpdated, const TArray<FEOSLobbySummaryBP>&, Summaries);

// High-level lobby lifecycle signals
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLobbyEventId, const FString&, LobbyId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLobbyInviteReceivedBP, const FString&, InviteId, const FString&, SenderPUID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLobbyInviteAcceptedBP, const FString&, InviteId, const FString&, TargetPUID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLobbyLeaveRequestedBP);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDisplayNameUpdated, const FString&, DisplayName);

UCLASS(BlueprintType)
class FWSCORE_API UEOSUnifiedSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// ===== Subsystem lifetime =====
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	bool Tick(float DeltaTime);

	/** Tick EOS once per frame via ticker. */
	void TickEOS(float DeltaSeconds);

	// ===== Blueprint events =====
	UPROPERTY(BlueprintAssignable, Category="EOS|Auth")   FOnAuthStateChanged      OnAuthStateChanged;
	UPROPERTY(BlueprintAssignable, Category="EOS|Friends")FOnFriendsUpdated        OnFriendsUpdated;

	// Lobby feed + lifecycle
	UPROPERTY(BlueprintAssignable, Category="EOS|Lobby")  FLobbySummariesUpdated   OnLobbySummariesUpdated;
	UPROPERTY(BlueprintAssignable, Category="EOS|Lobby")  FOnLobbyEventId          OnLobbyCreated;
	UPROPERTY(BlueprintAssignable, Category="EOS|Lobby")  FOnLobbyEventId          OnLobbyUpdated;
	UPROPERTY(BlueprintAssignable, Category="EOS|Lobby")  FOnLobbyEventId          OnLobbyJoined;
	UPROPERTY(BlueprintAssignable, Category="EOS|Lobby")  FOnLobbyEventId          OnLobbyLeftOrDestroyed;
	UPROPERTY(BlueprintAssignable, Category="EOS|Lobby")  FOnLobbyEventId          OnKickedFromLobby;
	UPROPERTY(BlueprintAssignable, Category="EOS|Lobby")  FOnLobbyInviteReceivedBP OnLobbyInviteReceived;
	UPROPERTY(BlueprintAssignable, Category="EOS|Lobby")  FOnLobbyInviteAcceptedBP OnLobbyInviteAccepted;
	UPROPERTY(BlueprintAssignable, Category="EOS|Lobby")  FOnLobbyLeaveRequestedBP OnLeaveLobbyRequested;

	// ===== Blueprint callable – Auth =====
	UFUNCTION(BlueprintCallable, Category="EOS|Auth") void Login();
	UFUNCTION(BlueprintCallable, Category="EOS|Auth") void LoginViaPortal();
	UFUNCTION(BlueprintCallable, Category="EOS|Auth") void Logout();
	/** Full revoke (Connect logout + Auth logout + delete persistent auth). */
	UFUNCTION(BlueprintCallable, Category="EOS|Auth") void HardLogout();
	UFUNCTION(BlueprintPure, Category="EOS|Auth") FString GetCachedDisplayName() const;
	UPROPERTY(BlueprintAssignable, Category="EOS|Auth")	FOnDisplayNameUpdated OnDisplayNameUpdated;
	UFUNCTION(BlueprintPure, Category="EOS|Auth") bool    IsLoggedIn() const;
	UFUNCTION(BlueprintPure, Category="EOS|Auth") int32   GetLocalUserNum() const;
	UFUNCTION(BlueprintPure, Category="EOS|Auth") FString GetEpicAccountIdString() const;
	UFUNCTION(BlueprintPure, Category="EOS|Auth") FString GetDeploymentOrSandboxId() const;
	
	// ===== Blueprint callable – UI =====
	UFUNCTION(BlueprintCallable, Category="EOS|UI") void ShowOverlay();

	// ===== Blueprint callable – Friends =====
	UFUNCTION(BlueprintCallable, Category="EOS|Friends") void QueryFriends();
	UFUNCTION(BlueprintCallable, Category="EOS|Friends") TArray<FEOSFriendView> GetCachedFriends() const;
	UFUNCTION(BlueprintCallable, Category="EOS|Friends") void SendFriendInvite(const FString& EpicAccountIdStr);
	UFUNCTION(BlueprintCallable, Category="EOS|Friends") void AcceptInvite(const FString& EpicAccountIdStr);
	UFUNCTION(BlueprintCallable, Category="EOS|Friends") void RejectInvite(const FString& EpicAccountIdStr);

	// ===== Blueprint callable – Lobby =====
	UFUNCTION(BlueprintCallable, Category="EOS|Lobby") void CreateLobby();
	UFUNCTION(BlueprintCallable, Category="EOS|Lobby") void SearchLobbies_ByBucket();
	void SearchLobbies_All();
	UFUNCTION(BlueprintCallable, Category="EOS|Lobby") void SearchLobbies_ByName(const FString& NameFilter);
	UFUNCTION(BlueprintCallable, Category="EOS|Lobby") void JoinLobby(const FString& LobbyId);
	UFUNCTION(BlueprintCallable, Category="EOS|Lobby") void LeaveLobby();
	UFUNCTION(BlueprintCallable, Category="EOS|Lobby") void InviteToLobby();
	UFUNCTION(BlueprintCallable, Category="EOS|Lobby") void AcceptLobbyInvite(const FString& InviteId);
	UFUNCTION(BlueprintCallable, Category="EOS|Lobby") void RejectLobbyInvite(const FString& InviteId);

	/** Current cached lobby summaries (copy out for BP/UI). */
	UFUNCTION(BlueprintCallable, Category="EOS|Lobby")
	void GetCachedLobbySummaries(UPARAM(ref) TArray<FEOSLobbySummaryBP>& OutSummaries) const;

	// ===== Blueprint Pure – Convenience =====
	/** Epic Account ID (EAID) string for the current user (empty if not logged in). */
	UFUNCTION(BlueprintPure, Category="EOS|Auth") FString GetLocalEpicAccountIdString() const;

	/** Product User ID (PUID) string for the current user (empty until Connect login). */
	UFUNCTION(BlueprintPure, Category="EOS|Auth") FString GetProductUserIdString() const;

	// ===== C++ accessors =====
	EOSUnifiedSystem&             GetSystem()             { return System; }
	EOSUnifiedAuthManager&        GetAuth()               { return System.GetAuthManager(); }
	EOSUnifiedFriendsManager*     GetFriendsManager()     { return System.GetFriendsManager(); }
	EOSUnifiedLobbyManager*       GetLobbyManager()       { return System.GetLobbyManager(); }

private:
	// Owning system (value type for simple lifetime with the subsystem)
	EOSUnifiedSystem System;

	// Ticker handle so we run Tick() even without a WorldSubsystem
	FTSTicker::FDelegateHandle TickerHandle;

	// Cached BP data
	UPROPERTY() TArray<FEOSFriendView>      CachedFriendsBP;
	UPROPERTY() TArray<FEOSLobbySummaryBP>  CachedLobbySummariesBP;

	// Utilities
	void GetLobbies(TArray<FEOSLobbySummaryBP>& OutSummaries) const;
	void RebuildLobbySummariesCache();
	void RecreateManagersIfPossible();             // calls System.CreateManagers(...) when IDs are ready
	void RefreshFriendsCacheFromManager();         // translates manager cache to FEOSFriendView[]
	void BindFriendsCallbacks();                   // binds OnFriendsListUpdated
	void BindLobbyCallbacks();                     // binds all lobby std::function events

	// Tiny helpers for string conversions (defined inline or in .cpp)
	static inline EOS_EpicAccountId EpicFromStr(const FString& S)
	{
		return EOSUnified::EpicIdFromString(S);
	}
	static inline FString FStringFromUtf8(const std::string& In)
	{
		return UTF8_TO_TCHAR(In.c_str());
	}
};
