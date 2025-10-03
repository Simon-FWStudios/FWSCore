#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnifiedBPLibrary.generated.h"

class UUnifiedSubsystemManager;

UCLASS()
class FWSCORE_API UUnifiedBPLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintPure, Category="Unified", meta=(WorldContext="WorldContextObject"))
	static UUnifiedSubsystemManager* GetUnified(const UObject* WorldContextObject);

	// Auth
	UFUNCTION(BlueprintCallable, Category="Unified|Auth", meta=(WorldContext="WorldContextObject"))
	static void Login(const UObject* WorldContextObject);
	UFUNCTION(BlueprintCallable, Category="Unified|Auth", meta=(WorldContext="WorldContextObject"))
	static void LoginPortal(const UObject* WorldContextObject);
	UFUNCTION(BlueprintCallable, Category="Unified|Auth", meta=(WorldContext="WorldContextObject"))
	static void Logout(const UObject* WorldContextObject);
	UFUNCTION(BlueprintCallable, Category="Unified|Auth", meta=(WorldContext="WorldContextObject"))
	static void ShowOverlay(const UObject* WorldContextObject);

	// Identity & Profiles
	UFUNCTION(BlueprintPure, Category="Unified|Identity", meta=(WorldContext="WorldContextObject"))
	static FString GetDisplayName(const UObject* WorldContextObject);
	UFUNCTION(BlueprintPure, Category="Unified|Identity", meta=(WorldContext="WorldContextObject"))
	static FString GetEpicAccountId(const UObject* WorldContextObject);
	UFUNCTION(BlueprintPure, Category="Unified|Identity", meta=(WorldContext="WorldContextObject"))
	static FString GetProductUserId(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category="Unified|Profiles", meta=(WorldContext="WorldContextObject"))
	static TArray<FString> GetAvailableProfiles(const UObject* WorldContextObject);
	UFUNCTION(BlueprintCallable, Category="Unified|Profiles", meta=(WorldContext="WorldContextObject"))
	static void SwitchProfile(const UObject* WorldContextObject, const FString& NewProfile);
	UFUNCTION(BlueprintCallable, Category="Unified|Profiles", meta=(WorldContext="WorldContextObject"))
	static void AddNewProfile(const UObject* WorldContextObject, const FString& NewProfile);
	UFUNCTION(BlueprintCallable, Category="Unified|Profiles", meta=(WorldContext="WorldContextObject"))
	static void DeleteProfile(const UObject* WorldContextObject, const FString& Profile);
	UFUNCTION(BlueprintCallable, Category="Unified|Profiles", meta=(WorldContext="WorldContextObject"))
	static void RequestSave(const UObject* WorldContextObject, bool bAsync);
	UFUNCTION(BlueprintCallable, Category="Unified|Profiles", meta=(WorldContext="WorldContextObject"))
	static void RequestLoad(const UObject* WorldContextObject, bool bAsync);

	// Friends
	UFUNCTION(BlueprintCallable, Category="Unified|Friends", meta=(WorldContext="WorldContextObject"))
	static void QueryFriends(const UObject* WorldContextObject);

	// Lobby
	UFUNCTION(BlueprintCallable, Category="Unified|Lobby", meta=(WorldContext="WorldContextObject"))
	static void CreateLobby(const UObject* WorldContextObject);
	UFUNCTION(BlueprintCallable, Category="Unified|Lobby", meta=(WorldContext="WorldContextObject"))
	static void SearchLobbies_All(const UObject* WorldContextObject);
	UFUNCTION(BlueprintCallable, Category="Unified|Lobby", meta=(WorldContext="WorldContextObject"))
	static void SearchLobbies_ByBucket(const UObject* WorldContextObject);
	UFUNCTION(BlueprintCallable, Category="Unified|Lobby", meta=(WorldContext="WorldContextObject"))
	static void SearchLobbies_ByName(const UObject* WorldContextObject, const FString& Name);
	UFUNCTION(BlueprintCallable, Category="Unified|Lobby", meta=(WorldContext="WorldContextObject"))
	static void JoinLobby(const UObject* WorldContextObject, const FString& LobbyId);
	UFUNCTION(BlueprintCallable, Category="Unified|Lobby", meta=(WorldContext="WorldContextObject"))
	static void LeaveLobby(const UObject* WorldContextObject);
	UFUNCTION(BlueprintCallable, Category="Unified|Lobby", meta=(WorldContext="WorldContextObject"))
	static void AcceptLobbyInvite(const UObject* WorldContextObject, const FString& InviteId);
	UFUNCTION(BlueprintCallable, Category="Unified|Lobby", meta=(WorldContext="WorldContextObject"))
	static void RejectLobbyInvite(const UObject* WorldContextObject, const FString& InviteId);
};
