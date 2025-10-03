#include "SaveSystemSubsystem.h"
#include "FWSCore.h"
#include "PlatformFeatures.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerState.h"
#include "SaveGameSystem.h"
#include "Async/Async.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "SaveIdComponent.h"
#include "GameFramework/Actor.h"
#include "SaveSystem.h"
#include "FWSCore/EOS/EOSUnifiedSubsystem.h"
#include "FWSCore/Player/PlayerProfileComponent.h"


namespace { static const FName PROFILE_OBJECT_ID(TEXT("Profile")); }

/* ---------- Internal helpers ---------- */

static float GetAutosaveJitter(float BaseSeconds)
{
	return FMath::FRandRange(0.0f, FMath::Min(5.0f, BaseSeconds * 0.25f));
}

void USaveSystemSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Grab EOS subsystem once.
	if (UGameInstance* GI = GetGameInstance())
	{
		EOSSub = GI->GetSubsystem<UEOSUnifiedSubsystem>();
	}

	if (EOSSub)
	{
		// Bind to auth-state changes.
		EOSSub->OnAuthStateChanged.RemoveAll(this);
		EOSSub->OnAuthStateChanged.AddDynamic(this, &USaveSystemSubsystem::HandleAuthChanged);

		// If we are already logged in when the subsystem starts, resolve immediately.
		if (EOSSub->IsLoggedIn())
		{
			ResolveLoadForAllLocalPlayers(/*bApplyAfterLoad*/true);
		}
	}
}
void USaveSystemSubsystem::Deinitialize()
{
	StopAutosaveTimer();

	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(DebouncedSaveHandle);
	}

	if (EOSSub)
	{
		EOSSub->OnAuthStateChanged.RemoveAll(this);
		EOSSub = nullptr;
	}
	
	bSaveInFlight = false;
	bSavePending  = false;

	Super::Deinitialize();
}

void USaveSystemSubsystem::HandleAuthChanged(bool bLoggedIn, const FString& Message)
{
	if (!bLoggedIn)
	{
		// (Optional) You could clear any cached state here if you add it later.
		return;
	}

	// On login, resolve key + load (+ apply) for every local player.
	ResolveLoadForAllLocalPlayers(/*bApplyAfterLoad*/true);
}

void USaveSystemSubsystem::ResolveLoadForAllLocalPlayers(bool bApplyAfterLoad)
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) return;

#if WITH_SERVER_CODE
	// Dedicated server has no LocalPlayers—skip.
	if (GI->IsDedicatedServerInstance())
	{
		return;
	}
#endif

	// Iterate all local players (handles split-screen too).
	const TArray<ULocalPlayer*>& LPs = GI->GetLocalPlayers();
	for (ULocalPlayer* LP : LPs)
	{
		if (!LP) continue;

		UWorld* World = GI->GetWorld();
		if (!World) continue;

		// 0..N − if you want strict mapping, use LP->GetControllerId() etc.
		APlayerController* PC = LP->PlayerController;
		if (!PC)
		{
			// Fallback: get by index (works in most single-player menu flows).
			PC = UGameplayStatics::GetPlayerController(World, LP->GetControllerId());
		}
		if (!PC) continue;

		// Ensure the profile component exists and initialize it.
		if (UPlayerProfileComponent* Profile = EnsureProfileComponent(PC))
		{
			// This should: resolve profile key → load settings → optionally apply
			Profile->InitializeProfile(/*bApplyAfterLoad*/bApplyAfterLoad);
		}
	}
}

UPlayerProfileComponent* USaveSystemSubsystem::EnsureProfileComponent(APlayerController* PC) const
{
	if (!PC) return nullptr;

	if (UPlayerProfileComponent* Existing = PC->FindComponentByClass<UPlayerProfileComponent>())
	{
		return Existing;
	}

	// Create one dynamically so games that haven't added it yet still work.
	UPlayerProfileComponent* NewComp =
		NewObject<UPlayerProfileComponent>(PC, UPlayerProfileComponent::StaticClass(), TEXT("PlayerProfileComponent"));
	if (NewComp)
	{
		NewComp->RegisterComponent();
	}
	return NewComp;
}

FString USaveSystemSubsystem::SanitizeSlotName(const FString& InRaw)
{
	FString Out = InRaw;
	Out.TrimStartAndEndInline();
	const TCHAR* Forbidden = TEXT("\\/:*?\"<>|#%&{}$!@+=`^~");
	for (const TCHAR* Ptr = Forbidden; *Ptr; ++Ptr)
	{
		Out.ReplaceCharInline(*Ptr, TEXT('_'));
	}
	while (Out.ReplaceInline(TEXT("  "), TEXT(" ")) > 0) {}
	Out.TrimStartAndEndInline();
	constexpr int32 MaxLen = 64;
	if (Out.Len() > MaxLen) { Out.LeftInline(MaxLen, EAllowShrinking::Default); }
	return Out.IsEmpty() ? FString(TEXT("Player")) : Out;
}

/* ---------- Identity sourcing (EOS-aware) ---------- */

namespace
{
	// Pull best identity info we can without crashing if EOS isn't ready yet.
	struct FIdentityData
	{
		FString PreferredSlotKey; // PUID/EAS/UniqueNetId/Name/...
		FString DisplayName;      // Nice name for UI/profile meta
		FString PUID;             // ProductUserId as string (optional)
		FString EAS;              // EpicAccountId as string (optional)
	};

	static FIdentityData GatherEOSIdentity(UGameInstance* GI, TFunctionRef<FString(const FString&)> SanitizeFn)
	{
		FIdentityData Out;

		if (GI)
		{
			if (UEOSUnifiedSubsystem* EOS = GI->GetSubsystem<UEOSUnifiedSubsystem>())
			{
				// These getters are from the EOS subsystem patch we added
				const FString Puid = EOS->GetProductUserIdString();
				const FString Eas  = EOS->GetLocalEpicAccountIdString();
				const FString Name = EOS->GetCachedDisplayName();

				Out.DisplayName = Name;
				Out.PUID = Puid;
				Out.EAS  = Eas;

				// Prefer PUID for slot stability (works across platforms, no rename issues)
				if (!Puid.IsEmpty())
				{
					Out.PreferredSlotKey = SanitizeFn(Puid);
					return Out;
				}
				// Fall back to EAS if available
				if (!Eas.IsEmpty())
				{
					Out.PreferredSlotKey = SanitizeFn(Eas);
					return Out;
				}
			}
		}

		// No EOS yet — try engine identity
		if (const ULocalPlayer* LP = GI ? GI->GetFirstGamePlayer() : nullptr)
		{
			if (LP->GetPreferredUniqueNetId().IsValid())
			{
				Out.PreferredSlotKey = SanitizeFn(LP->GetPreferredUniqueNetId()->ToString());
			}
		}

		// Try current PlayerState name
		if (Out.PreferredSlotKey.IsEmpty())
		{
			if (APlayerController* PC = GI ? GI->GetFirstLocalPlayerController() : nullptr)
			{
				if (APlayerState* PS = PC->PlayerState)
				{
					const FString Name = PS->GetPlayerName();
					if (!Name.IsEmpty())
					{
						Out.PreferredSlotKey = SanitizeFn(Name);
						if (Out.DisplayName.IsEmpty()) Out.DisplayName = Name;
					}
				}
			}
		}

		// Still nothing — use default sentinel (caller may still replace with first existing slot)
		if (Out.PreferredSlotKey.IsEmpty())
		{
			Out.PreferredSlotKey = TEXT("DefaultSaveSlot");
		}
		return Out;
	}
}

/* ---------- Lifecycle ---------- */

void USaveSystemSubsystem::InitializeSystem(bool bPrintDebug)
{
	bPrintDebugOutput = bPrintDebug;

	if (!SaveSystemClass)
	{
		SaveSystemClass = USaveSystem::StaticClass();
	}

	// Decide initial slot purely by identity (no side effects)
	SaveSlotName = ResolveSlotName();

	// Load or create
	if (UGameplayStatics::DoesSaveGameExist(SaveSlotName, 0))
	{
		CurrentSaveSystem = Cast<USaveSystem>(UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0));
		if (!CurrentSaveSystem)
		{
			CurrentSaveSystem = Cast<USaveSystem>(UGameplayStatics::CreateSaveGameObject(SaveSystemClass));
		}
		ExecuteLoad(false);
		if (bPrintDebugOutput)
			UE_LOG(LogSaveSystem, Log, TEXT("[SaveSystemSubsystem] Loaded Save Slot: %s"), *SaveSlotName);
	}
	else
	{
		CurrentSaveSystem = Cast<USaveSystem>(UGameplayStatics::CreateSaveGameObject(SaveSystemClass));
		CurrentSaveSystem->SaveVersion = CurrentSaveVersion;
		UGameplayStatics::SaveGameToSlot(CurrentSaveSystem, SaveSlotName, 0);
		if (bPrintDebugOutput)
			UE_LOG(LogSaveSystem, Log, TEXT("[SaveSystemSubsystem] Created Save Slot: %s"), *SaveSlotName);
	}

	bInitialised = true;

	// --- After we have *some* slot mounted, see if EOS is ready and prefer its key.
	{
		const FIdentityData Id = GatherEOSIdentity(GetGameInstance(), [this](const FString& S){ return SanitizeSlotName(S); });

		// Write meta if available
		if (!Id.DisplayName.IsEmpty()) CurrentSaveSystem->SetField(PROFILE_OBJECT_ID, TEXT("DisplayName"), Id.DisplayName);
		if (!Id.PUID.IsEmpty())       CurrentSaveSystem->SetField(PROFILE_OBJECT_ID, TEXT("PUID"),        Id.PUID);
		if (!Id.EAS.IsEmpty())        CurrentSaveSystem->SetField(PROFILE_OBJECT_ID, TEXT("EAS"),         Id.EAS);

		// If we ended up on a fallback slot but EOS is ready with a better key, switch once.
		if (!Id.PreferredSlotKey.IsEmpty() && Id.PreferredSlotKey != SaveSlotName)
		{
			if (bPrintDebugOutput)
			{
				UE_LOG(LogSaveSystem, Log, TEXT("[SaveSystemSubsystem] Switching to EOS-preferred slot: %s (from %s)"),
					*Id.PreferredSlotKey, *SaveSlotName);
			}
			SwitchProfile(Id.PreferredSlotKey);
			// Re-apply meta after switch (CurrentSaveSystem changed)
			if (!Id.DisplayName.IsEmpty()) CurrentSaveSystem->SetField(PROFILE_OBJECT_ID, TEXT("DisplayName"), Id.DisplayName);
			if (!Id.PUID.IsEmpty())       CurrentSaveSystem->SetField(PROFILE_OBJECT_ID, TEXT("PUID"),        Id.PUID);
			if (!Id.EAS.IsEmpty())        CurrentSaveSystem->SetField(PROFILE_OBJECT_ID, TEXT("EAS"),         Id.EAS);
		}
	}

	if (bEnableAutoSave)
	{
		StartAutosaveTimer();
	}
}

FString USaveSystemSubsystem::ResolveSlotName()
{
	// EOS / platform identity
	const FIdentityData Id = GatherEOSIdentity(GetGameInstance(), [this](const FString& S){ return SanitizeSlotName(S); });
	FString Result = Id.PreferredSlotKey;

	// If the chosen key doesn't exist and there *is* an existing slot on disk, prefer that (helps first boot / renamed ids)
	if (ISaveGameSystem* UESaveSystem = IPlatformFeaturesModule::Get().GetSaveGameSystem())
	{
		TArray<FString> Slots;
		UESaveSystem->GetSaveGameNames(Slots, 0);

		if (Slots.Num() > 0)
		{
			// If we picked "DefaultSaveSlot" but an older slot exists, stay on the first one for continuity
			if (Result.Equals(TEXT("DefaultSaveSlot"), ESearchCase::IgnoreCase))
			{
				return SanitizeSlotName(Slots[0]);
			}

			// If our preferred key doesn't exist yet but exactly one slot exists, you may want to migrate later.
			// We still return the preferred key here to start a fresh profile; comment out if you want auto-attach.
		}
	}

	return Result;
}

/* ---------- Registration ---------- */

void USaveSystemSubsystem::RegisterSaveable(UObject* Saveable)
{
	if (Saveable && Saveable->Implements<USaveable>())
	{
		RegisteredSaveables.AddUnique(Saveable);
	}
}

void USaveSystemSubsystem::UnregisterSaveable(UObject* Saveable)
{
	RegisteredSaveables.Remove(Saveable);
}

void USaveSystemSubsystem::UnregisterSaveable(AActor* DestroyedActor)
{
	if (ISaveable* Saveable = Cast<ISaveable>(DestroyedActor))
	{
		RegisteredSaveables.Remove(Cast<UObject>(DestroyedActor));
	}
}

/* ---------- Public API ---------- */

void USaveSystemSubsystem::RequestSave(bool bAsync)
{
	if (!CurrentSaveSystem)
	{
		UE_LOG(LogSaveSystem, Error, TEXT("[SaveSystemSubsystem] RequestSave failed: CurrentSaveSystem is null"));
		return;
	}

	if (bPrintDebugOutput)
	{
		UE_LOG(LogSaveSystem, Log, TEXT("[SaveSystemSubsystem] RequestSave (Async=%s)"), bAsync ? TEXT("true") : TEXT("false"));
	}

	// Debounce coalescing
	bSavePending = true;
	const float DebounceSeconds = 0.25f;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			DebouncedSaveHandle,
			[this, bAsync]()
			{
				if (!bSavePending) return;
				bSavePending = false;
				ExecuteSave(bAsync);
			},
			DebounceSeconds, false
		);
	}
}

void USaveSystemSubsystem::RequestLoad(bool bAsync)
{
	if (bPrintDebugOutput)
	{
		UE_LOG(LogSaveSystem, Log, TEXT("[SaveSystemSubsystem] RequestLoad (Async=%s)"), bAsync ? TEXT("true") : TEXT("false"));
	}
	ExecuteLoad(bAsync);
}

void USaveSystemSubsystem::ExecuteLoad(bool /*bAsync*/)
{
	// Load the SaveGame from slot again (source of truth)
	CurrentSaveSystem = Cast<USaveSystem>(UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0));
	if (!CurrentSaveSystem)
	{
		UE_LOG(LogSaveSystem, Warning, TEXT("[SaveSystemSubsystem] ExecuteLoad: No save exists for %s. Creating fresh."), *SaveSlotName);
		CurrentSaveSystem = Cast<USaveSystem>(UGameplayStatics::CreateSaveGameObject(SaveSystemClass));
	}

	TArray<UObject*> ValidObjects;
	for (const TWeakObjectPtr<UObject>& Obj : RegisteredSaveables)
	{
		if (Obj.IsValid())
		{
			ValidObjects.Add(Obj.Get());
		}
	}

	CurrentSaveSystem->LoadAllData(ValidObjects);

	if (bPrintDebugOutput)
	{
		UE_LOG(LogSaveSystem, Log, TEXT("[SaveSystemSubsystem] Loaded %d objects from slot %s"),
			ValidObjects.Num(), *SaveSlotName);
	}
}

void USaveSystemSubsystem::ExecuteSave(bool bAsync)
{
	if (!CurrentSaveSystem)
	{
		UE_LOG(LogSaveSystem, Error, TEXT("[SaveSystemSubsystem] ExecuteSave failed: CurrentSaveSystem is null"));
		return;
	}

	OnSaveStarted.Broadcast(SaveSlotName);

	// Stamp version for this write; SaveAllData updates timestamp
	CurrentSaveSystem->SaveVersion = CurrentSaveVersion;

	if (!bAsync)
	{
		const bool bOk = PerformSaveSync();
		OnSaveFinished.Broadcast(SaveSlotName, bOk);
		return;
	}

	// Async branch: collapse overlapping saves
	if (bSaveInFlight)
	{
		if (bPrintDebugOutput)
		{
			UE_LOG(LogSaveSystem, Verbose, TEXT("[SaveSystemSubsystem] Async save already in flight; collapsing."));
		}
		return;
	}
	bSaveInFlight = true;
	PerformSaveAsync();
}

bool USaveSystemSubsystem::PerformSaveSync()
{
	TArray<UObject*> ValidObjects;
	for (const TWeakObjectPtr<UObject>& Obj : RegisteredSaveables)
	{
		if (Obj.IsValid())
		{
			ValidObjects.Add(Obj.Get());
		}
	}

	// Must run on GT
	CurrentSaveSystem->SaveAllData(ValidObjects);

	const bool bOk = UGameplayStatics::SaveGameToSlot(CurrentSaveSystem, SaveSlotName, 0);

	static int32 SaveCounter = 0;
	if (bOk && (++SaveCounter % 5) == 0) // rotate a backup periodically
	{
		const FString Backup = SaveSlotName + TEXT(".bak");
		UGameplayStatics::SaveGameToSlot(CurrentSaveSystem, Backup, 0);
	}

	if (bPrintDebugOutput)
	{
		UE_LOG(LogSaveSystem, Log, TEXT("[SaveSystemSubsystem] Saved %d objects to slot %s (ok=%d)"),
			ValidObjects.Num(), *SaveSlotName, bOk ? 1 : 0);
	}

	return bOk;
}

void USaveSystemSubsystem::PerformSaveAsync()
{
	// Snapshot what you need
	const TArray<TWeakObjectPtr<UObject>> ObjectsToSave = RegisteredSaveables;
	USaveSystem* SaveObj = CurrentSaveSystem;
	const FString Slot = SaveSlotName;
	const bool bDebug = bPrintDebugOutput;
	const int32 Version = CurrentSaveVersion;

	TWeakObjectPtr<USaveSystemSubsystem> WeakThis(this);

	// Defer to GT (UObject traversal + SaveGameToSlot are GT-bound)
	AsyncTask(ENamedThreads::GameThread, [WeakThis, ObjectsToSave, SaveObj, Slot, bDebug, Version]()
	{
		if (!WeakThis.IsValid())
		{
			return;
		}

		USaveSystemSubsystem* Self = WeakThis.Get();

		TArray<UObject*> ValidObjects;
		for (const TWeakObjectPtr<UObject>& Obj : ObjectsToSave)
		{
			if (Obj.IsValid()) { ValidObjects.Add(Obj.Get()); }
		}

		bool bOk = false;

		if (SaveObj)
		{
			SaveObj->SaveVersion   = Version;
			SaveObj->SaveTimestamp = FDateTime::Now();

			// Must be on GT (interface calls / UObjects)
			SaveObj->SaveAllData(ValidObjects);

			bOk = UGameplayStatics::SaveGameToSlot(SaveObj, Slot, 0);

			if (bDebug)
			{
				UE_LOG(LogSaveSystem, Log, TEXT("[SaveSystemSubsystem] Async save write: %s"),
					   bOk ? TEXT("OK") : TEXT("FAILED"));
			}
		}

		Self->bSaveInFlight = false;
		Self->OnSaveFinished.Broadcast(Slot, bOk);
	});
}

/* ---------- Profiles ---------- */

void USaveSystemSubsystem::SwitchProfile(FString NewProfileName)
{
	if (NewProfileName.IsEmpty()) return;

	StopAutosaveTimer();

	SaveSlotName = SanitizeSlotName(NewProfileName);

	if (UGameplayStatics::DoesSaveGameExist(SaveSlotName, 0))
	{
		CurrentSaveSystem = Cast<USaveSystem>(UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0));
		ExecuteLoad(false);
		if (bPrintDebugOutput)
			UE_LOG(LogSaveSystem, Log, TEXT("[SaveSystemSubsystem] Loaded Save Slot: %s"), *SaveSlotName);
	}
	else
	{
		CurrentSaveSystem = Cast<USaveSystem>(UGameplayStatics::CreateSaveGameObject(SaveSystemClass));
		CurrentSaveSystem->SaveVersion = CurrentSaveVersion;
		UGameplayStatics::SaveGameToSlot(CurrentSaveSystem, SaveSlotName, 0);
		if (bPrintDebugOutput)
			UE_LOG(LogSaveSystem, Log, TEXT("[SaveSystemSubsystem] Created Save Slot: %s"), *SaveSlotName);
	}

	OnProfileChanged.Broadcast(SaveSlotName);
	
	if (bEnableAutoSave)
	{
		StartAutosaveTimer();
	}
}

TArray<FString> USaveSystemSubsystem::GetAvailableProfiles()
{
	ISaveGameSystem* UESaveSystem = IPlatformFeaturesModule::Get().GetSaveGameSystem();
	TArray<FString> Slots;
	if (UESaveSystem)
	{
		UESaveSystem->GetSaveGameNames(Slots, 0);
	}
	return Slots;
}

void USaveSystemSubsystem::AddNewProfile(FString NewProfileName)
{
	if (NewProfileName.IsEmpty()) return;
	if (UGameplayStatics::DoesSaveGameExist(NewProfileName, 0)) return;

	SwitchProfile(NewProfileName);
}

void USaveSystemSubsystem::DeleteProfile(const FString& ProfileName)
{
	if (!ProfileName.IsEmpty() && UGameplayStatics::DoesSaveGameExist(ProfileName, 0))
	{
		UGameplayStatics::DeleteGameInSlot(ProfileName, 0);
		if (bPrintDebugOutput)
		{
			UE_LOG(LogSaveSystem, Log, TEXT("[SaveSystemSubsystem] Deleted Save Slot: %s"), *ProfileName);
		}
	}
}

/* ---------- Edit Helpers ---------- */

FSaveObjectData* USaveSystemSubsystem::FindOrCreateSaveObject(FName ObjectId)
{
	if (!CurrentSaveSystem) return nullptr;
	return &CurrentSaveSystem->GetOrCreateObject(ObjectId);
}

FSaveObjectData USaveSystemSubsystem::FindOrCreateSaveObject_BP(FName ObjectId)
{
	// BP copy semantics; edits should be done via EditObjectField to persist
	if (!CurrentSaveSystem)
	{
		FSaveObjectData Tmp; return Tmp;
	}
	return CurrentSaveSystem->GetOrCreateObject(ObjectId);
}

FGuid USaveSystemSubsystem::GetOrCreateSaveGuid(AActor* Actor)
{
	if (!Actor) return FGuid();

	USaveIdComponent* Idc = Actor->FindComponentByClass<USaveIdComponent>();
	if (!Idc)
	{
		Idc = NewObject<USaveIdComponent>(Actor, USaveIdComponent::StaticClass(), TEXT("SaveIdComponent"));
		Idc->RegisterComponent();
	}
	return Idc->GetOrCreateGuid();
}

FSaveObjectData* USaveSystemSubsystem::FindOrCreateSaveObjectByGuid(const FGuid& Guid)
{
	if (!CurrentSaveSystem) return nullptr;
	return &CurrentSaveSystem->GetOrCreateObjectByGuid(Guid);
}

FSaveObjectData USaveSystemSubsystem::FindOrCreateSaveObjectByGuid_BP(const FGuid& Guid)
{
	if (!CurrentSaveSystem) { FSaveObjectData Tmp; return Tmp; }
	return CurrentSaveSystem->GetOrCreateObjectByGuid(Guid);
}

void USaveSystemSubsystem::EditObjectField(FName ObjectId, FName Key, const FString& NewValue, bool bSaveImmediately)
{
	if (!CurrentSaveSystem) return;

	CurrentSaveSystem->SetField(ObjectId, Key, NewValue);

	if (bSaveImmediately)
	{
		RequestSave(true);
	}
}

/* ---------- Autosave ---------- */

void USaveSystemSubsystem::StartAutosaveTimer()
{
	if (!GetWorld()) return;
	if (!bEnableAutoSave) return;

	if (!AutosaveTimerHandle.IsValid())
	{
		GetWorld()->GetTimerManager().SetTimer(
			AutosaveTimerHandle,
			[this]()
			{
				OnAutosaveTick.Broadcast();
				RequestSave(true);
			},
			AutoSaveIntervalSeconds + GetAutosaveJitter(AutoSaveIntervalSeconds),
			true
		);
	}
}

void USaveSystemSubsystem::StopAutosaveTimer()
{
	if (!GetWorld()) return;

	if (AutosaveTimerHandle.IsValid())
	{
		GetWorld()->GetTimerManager().ClearTimer(AutosaveTimerHandle);
	}
}



void USaveSystemSubsystem::SwitchProfileForIdentity(const FString& DisplayName,
							  const FString& PUID,
							  const FString& EAS)
{
	CurrentSaveSystem->SetField(PROFILE_OBJECT_ID, "DisplayName", DisplayName);
	CurrentSaveSystem->SetField(PROFILE_OBJECT_ID, "PUID", PUID);
	CurrentSaveSystem->SetField(PROFILE_OBJECT_ID, "EAS", EAS);
	const FString Slot = SanitizeSlotName(ResolveSlotName());
	SwitchProfile(Slot); // triggers load/create
}
