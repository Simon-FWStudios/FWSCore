#include "FWSGameInstance.h"
#include "FWSCore.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "FWSCore/Systems/Unified/UnifiedSubsystemManager.h"
#include "FWSCore/Player/PlayerProfileComponent.h"

void UFWSGameInstance::Init()
{
	Super::Init();

	Unified = GetSubsystem<UUnifiedSubsystemManager>();
	if (!Unified)
	{
		UE_LOG(LogFWSCore, Warning, TEXT("[BaseGI] UUnifiedSubsystemManager not found on GameInstance."));
	}
	else
	{
		UE_LOG(LogFWSCore, Log, TEXT("[BaseGI] Unified subsystem cached."));

		// Prime cached strings & meta
		RefreshCachedIdStrings();

		// Wire a few sticky updates so the cache stays fresh
		Unified->OnAuthChanged.AddDynamic(this, &UFWSGameInstance::HandleUnifiedAuthChanged);
		Unified->OnSettingsLoaded.AddDynamic(this, &UFWSGameInstance::HandleUnifiedSettingsLoaded);
		Unified->OnSettingsApplied.AddDynamic(this, &UFWSGameInstance::HandleUnifiedSettingsApplied);

		// Ensure the local PlayerProfileComponent exists so we can push identity meta
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			if (UPlayerProfileComponent* Profile = Unified->GetOrCreateLocalPlayerProfile(/*ControllerId*/0))
			{
				LocalProfileComp = Profile;
			}
		}

		// Slot bootstrap:
		// - If Unified already resolved/loaded (e.g., logged in), RequestLoad() is harmless.
		// - If offline/guest and no profiles exist, create a "Default" and switch to it.
		FString CurrentSlot = Unified->GetCurrentSlotName();
		if (CurrentSlot.IsEmpty())
		{
			TArray<FString> Profiles = Unified->GetAvailableProfiles();
			if (Profiles.Num() == 0)
			{
				const FString DefaultSlot = TEXT("Default");
				Unified->AddNewProfile(DefaultSlot);
				Unified->SwitchProfile(DefaultSlot);
				CurrentSlot = DefaultSlot;
				UE_LOG(LogFWSCore, Log, TEXT("[BaseGI] Created and switched to '%s' profile."), *DefaultSlot);
			}
			else
			{
				Unified->SwitchProfile(Profiles[0]);
				CurrentSlot = Profiles[0];
				UE_LOG(LogFWSCore, Log, TEXT("[BaseGI] Switched to first available profile '%s'."), *CurrentSlot);
			}
		}

		// Ask the save system (via facade) to (re)load the active slot asynchronously.
		Unified->RequestLoad(/*bAsync*/true);
	}

	// Final refresh in case anything above changed cached strings
	RefreshCachedIdStrings();
}

void UFWSGameInstance::Shutdown()
{
	// Clear cached identity
	CachedLocalEpicId.Reset();
	CachedProductUserId.Reset();
	CachedDisplayName.Reset();

	Unified = nullptr;
	LocalProfileComp.Reset();

	Super::Shutdown();
}

void UFWSGameInstance::RefreshCachedIdStrings()
{
	if (Unified)
	{
		// Unified facade exposes both EAID (EpicAccountId) and PUID (ProductUserId).
		CachedLocalEpicId   = Unified->GetEpicAccountId();
		CachedProductUserId = Unified->GetProductUserId();

		// Prefer PlayerProfile display name (works online/offline/splitscreen)
		CachedDisplayName   = Unified->GetDisplayName();
	}
	else
	{
		CachedLocalEpicId.Reset();
		CachedProductUserId.Reset();
		// Keep CachedDisplayName as-is (could be set by local settings/UI) or clear if you prefer:
		// CachedDisplayName.Reset();
	}
}

FSaveProfileKey UFWSGameInstance::BuildProfileKey() const
{
	FSaveProfileKey Key{};

	// Your previous intent: prefer PUID, then EAID, then a local fallback.
	if (!CachedProductUserId.IsEmpty())
	{
		Key.UserId = CachedProductUserId;
	}
	else if (!CachedLocalEpicId.IsEmpty())
	{
		Key.UserId = CachedLocalEpicId;
	}
	else
	{
		Key.UserId = TEXT("LocalProfile_Default");
	}

	// If your FSaveProfileKey has additional fields (SaveVersion, SlotName, etc.)
	// you can set them here or let your SaveSystem fill them in on resolve.

	return Key;
}

void UFWSGameInstance::HandleUnifiedAuthChanged(bool bLoggedIn, const FString& Message)
{
	UE_LOG(LogFWSCore, Log, TEXT("[Unified] AuthChanged: %s (%s)"),
		bLoggedIn ? TEXT("LoggedIn") : TEXT("LoggedOut"),
		*Message);

	// Example: update any cached fields / broadcast to UI
	const FString Display = Unified->GetDisplayName();
	UE_LOG(LogFWSCore, Verbose, TEXT("[Unified] DisplayName now: %s"), *Display);
	
}

void UFWSGameInstance::HandleUnifiedSettingsLoaded(FPlayerSettings Settings)
{
	UE_LOG(LogFWSCore, Log, TEXT("[Unified] Settings Loaded"));
	// Apply-to-engine or forward to UI as you did in the lambda previously.
}

void UFWSGameInstance::HandleUnifiedSettingsApplied(FPlayerSettings Settings)
{
	UE_LOG(LogFWSCore, Log, TEXT("[Unified] Settings Applied"));
	// Optional: persist UI state, notify widgets, etc.
}
