#include "UnifiedBPLibrary.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "UnifiedSubsystemManager.h"

static UUnifiedSubsystemManager* GetUnifiedInternal(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	if (const UWorld* W = WorldContextObject->GetWorld())
	{
		if (UGameInstance* GI = W->GetGameInstance())
		{
			return GI->GetSubsystem<UUnifiedSubsystemManager>();
		}
	}
	return nullptr;
}

UUnifiedSubsystemManager* UUnifiedBPLibrary::GetUnified(const UObject* W) { return GetUnifiedInternal(W); }

// Auth
void UUnifiedBPLibrary::Login(const UObject* W)       { if (auto* U=GetUnifiedInternal(W)) U->Login(); }
void UUnifiedBPLibrary::LoginPortal(const UObject* W) { if (auto* U=GetUnifiedInternal(W)) U->LoginPortal(); }
void UUnifiedBPLibrary::Logout(const UObject* W)      { if (auto* U=GetUnifiedInternal(W)) U->Logout(); }
void UUnifiedBPLibrary::ShowOverlay(const UObject* W) { if (auto* U=GetUnifiedInternal(W)) U->ShowOverlay(); }

// Identity & Profiles
FString UUnifiedBPLibrary::GetDisplayName(const UObject* W)   { if (auto* U=GetUnifiedInternal(W)) return U->GetDisplayName();   return {}; }
FString UUnifiedBPLibrary::GetEpicAccountId(const UObject* W) { if (auto* U=GetUnifiedInternal(W)) return U->GetEpicAccountId(); return {}; }
FString UUnifiedBPLibrary::GetProductUserId(const UObject* W) { if (auto* U=GetUnifiedInternal(W)) return U->GetProductUserId(); return {}; }

TArray<FString> UUnifiedBPLibrary::GetAvailableProfiles(const UObject* W) { if (auto* U=GetUnifiedInternal(W)) return U->GetAvailableProfiles(); return {}; }
void UUnifiedBPLibrary::SwitchProfile(const UObject* W, const FString& S) { if (auto* U=GetUnifiedInternal(W)) U->SwitchProfile(S); }
void UUnifiedBPLibrary::AddNewProfile(const UObject* W, const FString& S) { if (auto* U=GetUnifiedInternal(W)) U->AddNewProfile(S); }
void UUnifiedBPLibrary::DeleteProfile(const UObject* W, const FString& S) { if (auto* U=GetUnifiedInternal(W)) U->DeleteProfile(S); }
void UUnifiedBPLibrary::RequestSave(const UObject* W, bool bAsync)        { if (auto* U=GetUnifiedInternal(W)) U->RequestSave(bAsync); }
void UUnifiedBPLibrary::RequestLoad(const UObject* W, bool bAsync)        { if (auto* U=GetUnifiedInternal(W)) U->RequestLoad(bAsync); }

// Friends
void UUnifiedBPLibrary::QueryFriends(const UObject* W)                          { if (auto* U=GetUnifiedInternal(W)) U->QueryFriends(); }

// Lobby
void UUnifiedBPLibrary::CreateLobby(const UObject* W)                           { if (auto* U=GetUnifiedInternal(W)) U->CreateLobby(); }
void UUnifiedBPLibrary::SearchLobbies_All(const UObject* W)                     { if (auto* U=GetUnifiedInternal(W)) U->SearchLobbies_All(); }
void UUnifiedBPLibrary::SearchLobbies_ByBucket(const UObject* W)                { if (auto* U=GetUnifiedInternal(W)) U->SearchLobbies_ByBucket(); }
void UUnifiedBPLibrary::SearchLobbies_ByName(const UObject* W, const FString& N){ if (auto* U=GetUnifiedInternal(W)) U->SearchLobbies_ByName(N); }
void UUnifiedBPLibrary::JoinLobby(const UObject* W, const FString& Id)          { if (auto* U=GetUnifiedInternal(W)) U->JoinLobby(Id); }
void UUnifiedBPLibrary::LeaveLobby(const UObject* W)                            { if (auto* U=GetUnifiedInternal(W)) U->LeaveLobby(); }
void UUnifiedBPLibrary::AcceptLobbyInvite(const UObject* W, const FString& I)   { if (auto* U=GetUnifiedInternal(W)) U->AcceptLobbyInvite(I); }
void UUnifiedBPLibrary::RejectLobbyInvite(const UObject* W, const FString& I)   { if (auto* U=GetUnifiedInternal(W)) U->RejectLobbyInvite(I); }
