#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FWSCore/Shared/FWSTypes.h"
#include "FWSCore/Systems/Save/Saveable.h"
#include "PlayerProfileComponent.generated.h"

class UEOSUnifiedSubsystem;
class USaveSystemSubsystem;
class USaveSystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlayerSettingsLoaded,  const FPlayerSettings&, Settings);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlayerSettingsApplied, const FPlayerSettings&, Settings);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlayerProfileKeyChanged, const FSaveProfileKey&, Key);

/**
 * Owns profile resolution (EOS/local) and directs the SaveSystemSubsystem
 * to switch slots; all persistence and application of PlayerSettings is handled here.
 */
UCLASS(ClassGroup=(Game), meta=(BlueprintSpawnableComponent))
class UPlayerProfileComponent : public UActorComponent, public ISaveable
{
	GENERATED_BODY()

public:
	UPlayerProfileComponent();

	// --- Current state
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Profile")
	FSaveProfileKey CurrentKey;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Profile")
	FPlayerSettings CurrentSettings;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Profile")
	FString DisplayName;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Profile")
	bool bLoaded = false;

	// --- Events
	UPROPERTY(BlueprintAssignable, Category="Profile|Events")
	FOnPlayerSettingsLoaded OnSettingsLoaded;

	UPROPERTY(BlueprintAssignable, Category="Profile|Events")
	FOnPlayerSettingsApplied OnSettingsApplied;

	UPROPERTY(BlueprintAssignable, Category="Profile|Events")
	FOnPlayerProfileKeyChanged OnProfileKeyChanged;

	// --- API (replacing old SaveBindingsLibrary calls)
	UFUNCTION(BlueprintCallable, Category="Profile")
	void InitializeProfile(bool bApplyAfterLoad = true);

	UFUNCTION(BlueprintCallable, Category="Profile")
	void RefreshFromActiveIdentity(bool bApplyAfterLoad = true);

	UFUNCTION(BlueprintCallable, Category="Profile|Settings")
	void LoadSettings(bool& bFound);

	UFUNCTION(BlueprintCallable, Category="Profile|Settings")
	void ApplySettings();

	UFUNCTION(BlueprintCallable, Category="Profile|Settings")
	void SaveSettings();

	UFUNCTION(BlueprintCallable, Category="Profile|Settings")
	void UpdateSettings(const FPlayerSettings& NewSettings, bool bAutoSave = true, bool bAutoApply = true);

	// UI helpers
	UFUNCTION(BlueprintCallable, Category="Profile|UI")
	void GetCurrentSettingsBP(FPlayerSettings& OutSettings) const { OutSettings = CurrentSettings; }

	UFUNCTION(BlueprintCallable, Category="Profile|UI")
	void ApplyAndSaveSettingsBP(const FPlayerSettings& NewSettings, bool bApply = true, bool bSave = true)
	{
		CurrentSettings = NewSettings; if (bApply) ApplySettings(); if (bSave) SaveSettings();
	}

	// ----- Profile meta K/V (lives in the same save slot under a stable object id) -----
	UFUNCTION(BlueprintCallable, Category="Profile|Meta")
	void SetProfileMeta(FName Key, const FString& Value);

	UFUNCTION(BlueprintCallable, Category="Profile|Meta")
	void SetProfileMetaMap(const TMap<FName,FString>& Map);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Profile|Meta")
	bool GetProfileMeta(FName Key, FString& OutValue) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Profile|Meta")
	FPlayerProfileMeta GetProfileMetaSnapshot() const;

	/** Optional hook so UI can react instantly; add your own multicast if you want live updates. */
	void BroadcastMetaSnapshot() {}

	// ----- ISaveable -----
	virtual void SaveData_Implementation(USaveSystem* SaveSystem) override;
	virtual void LoadData_Implementation(USaveSystem* SaveSystem, const FSaveObjectData& Value) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// Identity resolve + mount
	void ResolveProfileKey();
	void RefreshDisplayName();
	void MountResolvedProfile(); // switch the slot + push meta into save subsystem

	// Apply helpers
	void ClampAndMigrate(FPlayerSettings& S) const;
	void ApplyEngineSettings(const FPlayerSettings& S);
	void ApplyCameraAndInput(const FPlayerSettings& S);

	// Accessors
	UEOSUnifiedSubsystem* GetEOSUnified() const;
	USaveSystemSubsystem* GetSaveSystem() const;
	UObject* GetWorldContext() const;

	// SaveFramework dynamic delegate support (bind whichever signature you actually expose)
	UFUNCTION() void HandleProfileMetaUpdated_NoArgs();
	UFUNCTION() void HandleProfileMetaUpdated_WithMeta(const FPlayerProfileMeta& Meta);
	void SubscribeToSaveFrameworkDelegates();
	void UnsubscribeFromSaveFrameworkDelegates();

private:
	// Local cached meta for cheap UI reads
	struct FLocalMetaCache { FString DisplayName, PUID, EAS; bool IsEmpty() const { return DisplayName.IsEmpty() && PUID.IsEmpty() && EAS.IsEmpty(); } };
	FLocalMetaCache CachedMeta;
};
