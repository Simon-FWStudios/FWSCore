#include "PlayerProfileComponent.h"

#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Scalability.h"                 // for quality preset (optional)
#include "FWSCore/EOS/EOSUnifiedSubsystem.h"
#include "FWSCore/Systems/Save/SaveSystemSubsystem.h"

namespace
{
	static const FName PROFILE_OBJECT_ID(TEXT("Profile"));        // meta (DisplayName/EAS/PUID/NS)
	static const FName SETTINGS_OBJECT_ID(TEXT("PlayerSettings")); // settings bucket we own
}

// ---------- small helpers ----------
static bool ReadFloat(USaveSystemSubsystem* S, const FName ObjectId, const FName Key, float& Out)
{
	FString Str;
	if (!S || !S->GetCurrentSaveSystem()->GetField(ObjectId, Key, Str)) return false;
	Out = FCString::Atof(*Str);
	return true;
}
static bool ReadBool(USaveSystemSubsystem* S, const FName ObjectId, const FName Key, bool& Out)
{
	FString Str;
	if (!S || !S->GetCurrentSaveSystem()->GetField(ObjectId, Key, Str)) return false;
	Out = (Str == TEXT("1") || Str.Equals(TEXT("true"), ESearchCase::IgnoreCase));
	return true;
}
static void WriteFloat(USaveSystemSubsystem* S, const FName ObjectId, const FName Key, float Val)
{
	if (!S) return;
	S->GetCurrentSaveSystem()->SetField(ObjectId, Key, FString::SanitizeFloat(Val));
}
static void WriteBool(USaveSystemSubsystem* S, const FName ObjectId, const FName Key, bool Val)
{
	if (!S) return;
	S->GetCurrentSaveSystem()->SetField(ObjectId, Key, Val ? TEXT("1") : TEXT("0"));
}

// ---------- component ----------

UPlayerProfileComponent::UPlayerProfileComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicated(false);
}

void UPlayerProfileComponent::BeginPlay()
{
	Super::BeginPlay();

	// Register as a saveable object (by stable object id)
	if (USaveSystemSubsystem* Save = GetSaveSystem())
	{
		Save->RegisterSaveable(this);
	}

	SubscribeToSaveFrameworkDelegates();
	InitializeProfile(true);
}

void UPlayerProfileComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnsubscribeFromSaveFrameworkDelegates();

	// De-register from the save system
	if (USaveSystemSubsystem* Save = GetSaveSystem())
	{
		Save->UnregisterSaveable(this);
	}

	Super::EndPlay(EndPlayReason);
}

// ---------- meta (profile kv) ----------

void UPlayerProfileComponent::SetProfileMeta(FName Key, const FString& Value)
{
	if (USaveSystemSubsystem* Save = GetSaveSystem())
	{
		Save->GetCurrentSaveSystem()->SetField(PROFILE_OBJECT_ID, Key, Value);

		if (Key == TEXT("DisplayName")) CachedMeta.DisplayName = Value;
		else if (Key == TEXT("PUID"))   CachedMeta.PUID        = Value;
		else if (Key == TEXT("EAS"))    CachedMeta.EAS         = Value;

		BroadcastMetaSnapshot();
		Save->RequestSave(false);
	}
}

void UPlayerProfileComponent::SetProfileMetaMap(const TMap<FName,FString>& Map)
{
	if (USaveSystemSubsystem* Save = GetSaveSystem())
	{
		for (const auto& Kvp : Map)
		{
			Save->GetCurrentSaveSystem()->SetField(PROFILE_OBJECT_ID, Kvp.Key, Kvp.Value);
			if (Kvp.Key == TEXT("DisplayName")) CachedMeta.DisplayName = Kvp.Value;
			else if (Kvp.Key == TEXT("PUID"))   CachedMeta.PUID        = Kvp.Value;
			else if (Kvp.Key == TEXT("EAS"))    CachedMeta.EAS         = Kvp.Value;
		}
		BroadcastMetaSnapshot();
		Save->RequestSave(false);
	}
}

bool UPlayerProfileComponent::GetProfileMeta(FName Key, FString& OutValue) const
{
	if (USaveSystemSubsystem* Save = GetSaveSystem())
	{
		return Save->GetCurrentSaveSystem()->GetField(PROFILE_OBJECT_ID, Key, OutValue);
	}
	return false;
}

FPlayerProfileMeta UPlayerProfileComponent::GetProfileMetaSnapshot() const
{
	FPlayerProfileMeta Meta;
	if (USaveSystem* Save = GetSaveSystem()->GetCurrentSaveSystem())
	{
		FString Val;
		if (Save->GetField(PROFILE_OBJECT_ID, TEXT("DisplayName"), Val)) Meta.KV.Add(TEXT("DisplayName"), Val);
		if (Save->GetField(PROFILE_OBJECT_ID, TEXT("PUID"),        Val)) Meta.KV.Add(TEXT("PUID"),        Val);
		if (Save->GetField(PROFILE_OBJECT_ID, TEXT("EAS"),         Val)) Meta.KV.Add(TEXT("EAS"),         Val);
		if (Save->GetField(PROFILE_OBJECT_ID, TEXT("NS"),          Val)) Meta.KV.Add(TEXT("NS"),          Val);
	}
	return Meta;
}

// ---------- main flow ----------

void UPlayerProfileComponent::InitializeProfile(bool bApplyAfterLoad)
{
	const FString Old = CurrentKey.ToSlotName();
	ResolveProfileKey();

	if (Old != CurrentKey.ToSlotName())
	{
		OnProfileKeyChanged.Broadcast(CurrentKey);
	}

	RefreshDisplayName();
	MountResolvedProfile();              // ensures correct slot & meta in save system

	bool bFound = false;
	LoadSettings(bFound);
	if (bApplyAfterLoad) { ApplySettings(); }
}

void UPlayerProfileComponent::RefreshFromActiveIdentity(bool bApplyAfterLoad)
{
	InitializeProfile(bApplyAfterLoad);
}

// ---------- settings (persisted here; applied here) ----------

void UPlayerProfileComponent::ClampAndMigrate(FPlayerSettings& S) const
{
	S.MasterVolume     = FMath::Clamp(S.MasterVolume, 0.f, 1.f);
	S.SFXVolume        = FMath::Clamp(S.SFXVolume,    0.f, 1.f);
	S.MusicVolume      = FMath::Clamp(S.MusicVolume,  0.f, 1.f);
	S.MouseSensitivity = FMath::Clamp(S.MouseSensitivity, 0.1f, 10.f);
	S.FieldOfView      = FMath::Clamp(S.FieldOfView,  60.f, 120.f);

	if (S.Version < 1)
	{
		S.Version = 1;
		// future migrations here
	}
}

void UPlayerProfileComponent::LoadSettings(bool& bFound)
{
	bFound = false;

	if (USaveSystemSubsystem* Save = GetSaveSystem())
	{
		FPlayerSettings S = CurrentSettings; // start from whatever defaults you ship

		bool any = false;
		any |= ReadFloat(Save, SETTINGS_OBJECT_ID, TEXT("MasterVolume"),     S.MasterVolume);
		any |= ReadFloat(Save, SETTINGS_OBJECT_ID, TEXT("SFXVolume"),        S.SFXVolume);
		any |= ReadFloat(Save, SETTINGS_OBJECT_ID, TEXT("MusicVolume"),      S.MusicVolume);
		any |= ReadFloat(Save, SETTINGS_OBJECT_ID, TEXT("FieldOfView"),      S.FieldOfView);
		any |= ReadFloat(Save, SETTINGS_OBJECT_ID, TEXT("MouseSensitivity"), S.MouseSensitivity);
		any |= ReadBool (Save, SETTINGS_OBJECT_ID, TEXT("bVSync"),           S.bVSync);
		any |= ReadBool (Save, SETTINGS_OBJECT_ID, TEXT("bInvertY"),         S.bInvertY);

		// Optional fields
		{
			FString Str;
			if (Save->GetCurrentSaveSystem()->GetField(SETTINGS_OBJECT_ID, TEXT("PreferredDisplayName"), Str)) { S.PreferredDisplayName = Str; any = true; }
			if (Save->GetCurrentSaveSystem()->GetField(SETTINGS_OBJECT_ID, TEXT("ChosenAvatarId"),       Str)) { S.ChosenAvatarId       = Str; any = true; }
			if (Save->GetCurrentSaveSystem()->GetField(SETTINGS_OBJECT_ID, TEXT("ThemeId"),              Str)) { S.ThemeId = FName(*Str);      any = true; }
			if (Save->GetCurrentSaveSystem()->GetField(SETTINGS_OBJECT_ID, TEXT("QualityPreset"),        Str)) { S.QualityPreset = FCString::Atoi(*Str); any = true; }
			if (Save->GetCurrentSaveSystem()->GetField(SETTINGS_OBJECT_ID, TEXT("Version"),              Str)) { S.Version = FCString::Atoi(*Str); any = true; }
		}

		ClampAndMigrate(S);
		CurrentSettings = S;
		bFound = any;
	}

	bLoaded = true;
	OnSettingsLoaded.Broadcast(CurrentSettings);
}

void UPlayerProfileComponent::ApplyEngineSettings(const FPlayerSettings& S)
{
	// VSync (works without assets)
	if (IConsoleVariable* CVarVSync = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync")))
	{
		CVarVSync->Set(S.bVSync ? 1 : 0);
	}

	// Optional: scalability preset (0..3)
	if (S.QualityPreset >= 0 && S.QualityPreset <= 3)
	{
		Scalability::FQualityLevels Q = Scalability::GetQualityLevels();
		Q.SetFromSingleQualityLevel(S.QualityPreset);
		Scalability::SetQualityLevels(Q);
	}

	// NOTE: Global audio volumes usually require SoundMix/Class assets to do “properly”.
	// If you have those, consider exposing a BlueprintImplementableEvent and apply them in BP.
	// Here we keep it light and let the game handle audio application via the OnSettingsApplied event if desired.
}

void UPlayerProfileComponent::ApplyCameraAndInput(const FPlayerSettings& S)
{
	if (UWorld* W = GetWorld())
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0))
		{
			if (PC && PC->PlayerCameraManager)
			{
				PC->PlayerCameraManager->SetFOV(S.FieldOfView);
			}
		}
	}
	// NOTE: Apply invert-Y & sensitivity via your input system (Enhanced Input or BP).
}

void UPlayerProfileComponent::ApplySettings()
{
	ApplyEngineSettings(CurrentSettings);
	ApplyCameraAndInput(CurrentSettings);
	OnSettingsApplied.Broadcast(CurrentSettings);
}

void UPlayerProfileComponent::SaveSettings()
{
	if (USaveSystemSubsystem* Save = GetSaveSystem())
	{
		const FPlayerSettings& S = CurrentSettings;

		WriteFloat(Save, SETTINGS_OBJECT_ID, TEXT("MasterVolume"),     S.MasterVolume);
		WriteFloat(Save, SETTINGS_OBJECT_ID, TEXT("SFXVolume"),        S.SFXVolume);
		WriteFloat(Save, SETTINGS_OBJECT_ID, TEXT("MusicVolume"),      S.MusicVolume);
		WriteFloat(Save, SETTINGS_OBJECT_ID, TEXT("FieldOfView"),      S.FieldOfView);
		WriteFloat(Save, SETTINGS_OBJECT_ID, TEXT("MouseSensitivity"), S.MouseSensitivity);
		WriteBool (Save, SETTINGS_OBJECT_ID, TEXT("bVSync"),           S.bVSync);
		WriteBool (Save, SETTINGS_OBJECT_ID, TEXT("bInvertY"),         S.bInvertY);

		Save->GetCurrentSaveSystem()->SetField(SETTINGS_OBJECT_ID, TEXT("PreferredDisplayName"), S.PreferredDisplayName);
		Save->GetCurrentSaveSystem()->SetField(SETTINGS_OBJECT_ID, TEXT("ChosenAvatarId"),       S.ChosenAvatarId);
		Save->GetCurrentSaveSystem()->SetField(SETTINGS_OBJECT_ID, TEXT("ThemeId"),              S.ThemeId.ToString());
		Save->GetCurrentSaveSystem()->SetField(SETTINGS_OBJECT_ID, TEXT("QualityPreset"),        FString::FromInt(S.QualityPreset));
		Save->GetCurrentSaveSystem()->SetField(SETTINGS_OBJECT_ID, TEXT("Version"),              FString::FromInt(S.Version));

		Save->RequestSave(false);
	}
}

void UPlayerProfileComponent::UpdateSettings(const FPlayerSettings& NewSettings, bool bAutoSave, bool bAutoApply)
{
	CurrentSettings = NewSettings;
	ClampAndMigrate(CurrentSettings);
	if (bAutoApply) ApplySettings();
	if (bAutoSave)  SaveSettings();
}

// ---------- identity + mount ----------

void UPlayerProfileComponent::ResolveProfileKey()
{
	// Default fallback
	CurrentKey = FSaveProfileKey::LocalFallback(0);

	// Prefer EOS if available
	if (UEOSUnifiedSubsystem* EOS = GetEOSUnified())
	{
		if (EOS->IsLoggedIn())
		{
			CurrentKey.Platform     = TEXT("EOS");
			CurrentKey.UserId       = EOS->GetLocalEpicAccountIdString();
			CurrentKey.LocalUserNum = 0;
			const FString NS = EOS->GetDeploymentOrSandboxId();
			if (!NS.IsEmpty()) { CurrentKey.Namespace = FName(NS); }
		}
	}
}

void UPlayerProfileComponent::RefreshDisplayName()
{
	DisplayName.Empty();
	if (UEOSUnifiedSubsystem* EOS = GetEOSUnified())
	{
		if (EOS->IsLoggedIn())
		{
			const FString Name = EOS->GetCachedDisplayName();
			DisplayName = !Name.IsEmpty() ? Name : CurrentKey.UserId;
			return;
		}
	}
	DisplayName = CurrentKey.UserId; // local fallback
}

void UPlayerProfileComponent::MountResolvedProfile()
{
	if (USaveSystemSubsystem* Save = GetSaveSystem())
	{
		const FString Slot = CurrentKey.ToSlotName();
		Save->SwitchProfile(Slot); // Save layer stays neutral (no EOS knowledge)

		// Build meta snapshot for the slot
		TMap<FName, FString> Meta;
		if (!DisplayName.IsEmpty()) { Meta.Add(TEXT("DisplayName"), DisplayName); CachedMeta.DisplayName = DisplayName; }

		if (UEOSUnifiedSubsystem* EOS = GetEOSUnified())
		{
			const FString Puid = EOS->GetProductUserIdString();
			const FString Eas  = EOS->GetLocalEpicAccountIdString();
			const FString NS   = EOS->GetDeploymentOrSandboxId();

			if (!Puid.IsEmpty()) { Meta.Add(TEXT("PUID"), Puid); CachedMeta.PUID = Puid; }
			if (!Eas.IsEmpty())  { Meta.Add(TEXT("EAS"),  Eas);  CachedMeta.EAS  = Eas;  }
			if (!NS.IsEmpty())   { Meta.Add(TEXT("NS"),   NS); }
		}

		if (Meta.Num() > 0)
		{
			SetProfileMetaMap(Meta);
		}
	}
}

// ---------- SaveFramework delegates (dynamic) ----------

void UPlayerProfileComponent::SubscribeToSaveFrameworkDelegates()
{
	if (USaveSystemSubsystem* Save = GetSaveSystem())
	{
		// Use the declaration that matches your USaveSystemSubsystem:
		// Save->OnProfileMetaUpdated.AddDynamic(this, &UPlayerProfileComponent::HandleProfileMetaUpdated_NoArgs);
		// or
		//Save->OnProfileMetaUpdated.AddDynamic(this, &UPlayerProfileComponent::HandleProfileMetaUpdated_WithMeta);
	}
}

void UPlayerProfileComponent::UnsubscribeFromSaveFrameworkDelegates()
{
	if (USaveSystemSubsystem* Save = GetSaveSystem())
	{
		// Mirror whichever you used in Subscribe:
		// Save->OnProfileMetaUpdated.RemoveDynamic(this, &UPlayerProfileComponent::HandleProfileMetaUpdated_NoArgs);
		// Save->OnProfileMetaUpdated.RemoveDynamic(this, &UPlayerProfileComponent::HandleProfileMetaUpdated_WithMeta);
	}
}

void UPlayerProfileComponent::HandleProfileMetaUpdated_NoArgs()
{
	ResolveProfileKey();
	RefreshDisplayName();
	MountResolvedProfile();

	bool bFound = false;
	LoadSettings(bFound);
	ApplySettings();
}

void UPlayerProfileComponent::HandleProfileMetaUpdated_WithMeta(const FPlayerProfileMeta& /*Meta*/)
{
	HandleProfileMetaUpdated_NoArgs();
}

// ---------- ISaveable (Save/Load passes) ----------

void UPlayerProfileComponent::SaveData_Implementation(USaveSystem* SaveSystemObj)
{
	if (!SaveSystemObj) return;

	// Persist lightweight profile meta (DisplayName/PUID/EAS)
	if (!CachedMeta.DisplayName.IsEmpty()) SaveSystemObj->SetField(PROFILE_OBJECT_ID, TEXT("DisplayName"), CachedMeta.DisplayName);
	if (!CachedMeta.PUID.IsEmpty())        SaveSystemObj->SetField(PROFILE_OBJECT_ID, TEXT("PUID"),        CachedMeta.PUID);
	if (!CachedMeta.EAS.IsEmpty())         SaveSystemObj->SetField(PROFILE_OBJECT_ID, TEXT("EAS"),         CachedMeta.EAS);

	// Persist current settings snapshot under SETTINGS_OBJECT_ID
	SaveSystemObj->SetField(SETTINGS_OBJECT_ID, TEXT("MasterVolume"),     FString::SanitizeFloat(CurrentSettings.MasterVolume));
	SaveSystemObj->SetField(SETTINGS_OBJECT_ID, TEXT("SFXVolume"),        FString::SanitizeFloat(CurrentSettings.SFXVolume));
	SaveSystemObj->SetField(SETTINGS_OBJECT_ID, TEXT("MusicVolume"),      FString::SanitizeFloat(CurrentSettings.MusicVolume));
	SaveSystemObj->SetField(SETTINGS_OBJECT_ID, TEXT("FieldOfView"),      FString::SanitizeFloat(CurrentSettings.FieldOfView));
	SaveSystemObj->SetField(SETTINGS_OBJECT_ID, TEXT("MouseSensitivity"), FString::SanitizeFloat(CurrentSettings.MouseSensitivity));
	SaveSystemObj->SetField(SETTINGS_OBJECT_ID, TEXT("bVSync"),           CurrentSettings.bVSync ? TEXT("1") : TEXT("0"));
	SaveSystemObj->SetField(SETTINGS_OBJECT_ID, TEXT("bInvertY"),         CurrentSettings.bInvertY ? TEXT("1") : TEXT("0"));
	SaveSystemObj->SetField(SETTINGS_OBJECT_ID, TEXT("PreferredDisplayName"), CurrentSettings.PreferredDisplayName);
	SaveSystemObj->SetField(SETTINGS_OBJECT_ID, TEXT("ChosenAvatarId"),       CurrentSettings.ChosenAvatarId);
	SaveSystemObj->SetField(SETTINGS_OBJECT_ID, TEXT("ThemeId"),              CurrentSettings.ThemeId.ToString());
	SaveSystemObj->SetField(SETTINGS_OBJECT_ID, TEXT("QualityPreset"),        FString::FromInt(CurrentSettings.QualityPreset));
	SaveSystemObj->SetField(SETTINGS_OBJECT_ID, TEXT("Version"),              FString::FromInt(CurrentSettings.Version));
}

void UPlayerProfileComponent::LoadData_Implementation(USaveSystem* SaveSystemObj, const FSaveObjectData& /*Value*/)
{
	if (!SaveSystemObj) return;

	// Rebuild local profile meta cache
	SaveSystemObj->GetField(PROFILE_OBJECT_ID, TEXT("DisplayName"), CachedMeta.DisplayName);
	SaveSystemObj->GetField(PROFILE_OBJECT_ID, TEXT("PUID"),        CachedMeta.PUID);
	SaveSystemObj->GetField(PROFILE_OBJECT_ID, TEXT("EAS"),         CachedMeta.EAS);

	// Rehydrate settings into CurrentSettings
	auto GetF = [&](const TCHAR* K, float& Out) { FString S; if (SaveSystemObj->GetField(SETTINGS_OBJECT_ID, K, S)) Out = FCString::Atof(*S); };
	auto GetB = [&](const TCHAR* K, bool&  Out) { FString S; if (SaveSystemObj->GetField(SETTINGS_OBJECT_ID, K, S)) Out = (S==TEXT("1")||S.Equals(TEXT("true"),ESearchCase::IgnoreCase)); };
	auto GetS = [&](const TCHAR* K, FString& Out){ SaveSystemObj->GetField(SETTINGS_OBJECT_ID, K, Out); };

	GetF(TEXT("MasterVolume"),     CurrentSettings.MasterVolume);
	GetF(TEXT("SFXVolume"),        CurrentSettings.SFXVolume);
	GetF(TEXT("MusicVolume"),      CurrentSettings.MusicVolume);
	GetF(TEXT("FieldOfView"),      CurrentSettings.FieldOfView);
	GetF(TEXT("MouseSensitivity"), CurrentSettings.MouseSensitivity);
	GetB(TEXT("bVSync"),           CurrentSettings.bVSync);
	GetB(TEXT("bInvertY"),         CurrentSettings.bInvertY);

	FString Tmp;
	GetS(TEXT("PreferredDisplayName"), CurrentSettings.PreferredDisplayName);
	GetS(TEXT("ChosenAvatarId"),       CurrentSettings.ChosenAvatarId);
	GetS(TEXT("ThemeId"),              Tmp); if (!Tmp.IsEmpty()) CurrentSettings.ThemeId = FName(*Tmp);
	GetS(TEXT("QualityPreset"),        Tmp); if (!Tmp.IsEmpty()) CurrentSettings.QualityPreset = FCString::Atoi(*Tmp);
	GetS(TEXT("Version"),              Tmp); if (!Tmp.IsEmpty()) CurrentSettings.Version       = FCString::Atoi(*Tmp);

	ClampAndMigrate(CurrentSettings);
	bLoaded = true;
}

// ---------- accessors ----------

UEOSUnifiedSubsystem* UPlayerProfileComponent::GetEOSUnified() const
{
	if (UWorld* W = GetWorld())
	{
		if (UGameInstance* GI = W->GetGameInstance())
			return GI->GetSubsystem<UEOSUnifiedSubsystem>();
	}
	return nullptr;
}

USaveSystemSubsystem* UPlayerProfileComponent::GetSaveSystem() const
{
	if (UWorld* W = GetWorld())
	{
		if (UGameInstance* GI = W->GetGameInstance())
			return GI->GetSubsystem<USaveSystemSubsystem>();
	}
	return nullptr;
}

UObject* UPlayerProfileComponent::GetWorldContext() const
{
	return GetOwner() ? (UObject*)GetOwner() : (UObject*)this;
}
