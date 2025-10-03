#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TimerManager.h"
#include "SaveSystem.h"
#include "Saveable.h"
#include "SaveSystemSubsystem.generated.h"

class UPlayerProfileComponent;
class UEOSUnifiedSubsystem;
/** Delegates */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSaveStarted, FString, SlotName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSaveFinished, FString, SlotName, bool, bSuccess);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnProfileChanged, FString /* NewSlot */);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAutosaveTick);

/**
 * GameInstance-level Save Manager for a single player profile/slot.
 * Stores per-object fields in USaveSystem and exposes helpers for profile meta.
 */
UCLASS()
class FWSCORE_API USaveSystemSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/* ---------- Lifecyle ---------- */

	/** Call once from GameInstance Init. */
	UFUNCTION(BlueprintCallable, Category="Save System")
	void InitializeSystem(bool bPrintDebug = false);

	void Initialize(FSubsystemCollectionBase& Collection) override;
	/** Clean shutdown. */
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category="Save System") void HandleAuthChanged(bool bLoggedIn, const FString& Message);
	void ResolveLoadForAllLocalPlayers(bool bApplyAfterLoad);
	UPlayerProfileComponent* EnsureProfileComponent(APlayerController* PC) const;

	/** Returns the chosen (sanitized) slot name for this run/user. */
	UFUNCTION(BlueprintPure, Category="Save System")
	FString GetCurrentSlotName() const { return SaveSlotName; }

	/** Resolve a slot name from currently available identity (EOS, platform, local). No side effects. */
	FString ResolveSlotName();

	/* ---------- Registration ---------- */

	UFUNCTION(BlueprintCallable, Category="Save System")
	void RegisterSaveable(UObject* Saveable);

	UFUNCTION(BlueprintCallable, Category="Save System")
	void UnregisterSaveable(UObject* Saveable);

	/** Convenience overload for actors being destroyed. */
	void UnregisterSaveable(AActor* DestroyedActor);

	/* ---------- Public API ---------- */

	/** Request a save; coalesced with a short debounce. */
	UFUNCTION(BlueprintCallable, Category="Save System")
	void RequestSave(bool bAsync);

	/** Request a load from disk into all registered objects. */
	UFUNCTION(BlueprintCallable, Category="Save System")
	void RequestLoad(bool bAsync);

	/** Switch the active profile (slot). Loads/creates the new slot. */
	UFUNCTION(BlueprintCallable, Category="Save System|Profiles")
	void SwitchProfile(FString NewProfileName);

	/** Enumerate existing save slots discovered by IPlatformFeaturesModule. */
	UFUNCTION(BlueprintCallable, Category="Save System|Profiles")
	TArray<FString> GetAvailableProfiles();

	/** Create & switch to a new profile if it doesn't exist. */
	UFUNCTION(BlueprintCallable, Category="Save System|Profiles")
	void AddNewProfile(FString NewProfileName);

	/** Delete a profile slot. (Does not affect the current in-memory save.) */
	UFUNCTION(BlueprintCallable, Category="Save System|Profiles")
	void DeleteProfile(const FString& ProfileName);

	/* ---------- Edit Helpers ---------- */

	UFUNCTION(BlueprintPure, Category="Save System|Utilities")
	static FString SanitizeSlotName(const FString& InRaw);

	FSaveObjectData* FindOrCreateSaveObject(FName ObjectId);
	UFUNCTION(BlueprintCallable, Category="Save System|Edit")
	FSaveObjectData FindOrCreateSaveObject_BP(FName ObjectId);

	FGuid GetOrCreateSaveGuid(AActor* Actor);

	FSaveObjectData* FindOrCreateSaveObjectByGuid(const FGuid& Guid);
	UFUNCTION(BlueprintCallable, Category="Save System|Edit")
	FSaveObjectData FindOrCreateSaveObjectByGuid_BP(const FGuid& Guid);

	UFUNCTION(BlueprintCallable, Category="Save System|Edit")
	void EditObjectField(FName ObjectId, FName Key, const FString& NewValue, bool bSaveImmediately);

	/* ---------- Delegates ---------- */

	UPROPERTY(BlueprintAssignable, Category="Save System|Events")
	FOnSaveStarted OnSaveStarted;

	UPROPERTY(BlueprintAssignable, Category="Save System|Events")
	FOnSaveFinished OnSaveFinished;

	UPROPERTY(BlueprintAssignable, Category="Save System|Events")
	FOnAutosaveTick OnAutosaveTick;

	/** C++ only (avoids UHT bloat) */
	FOnProfileChanged OnProfileChanged;

	/* ---------- Config ---------- */

	UPROPERTY(EditAnywhere, Category="Save System|Config")
	TSubclassOf<USaveSystem> SaveSystemClass = USaveSystem::StaticClass();

	UPROPERTY(EditAnywhere, Category="Save System|Config")
	int32 CurrentSaveVersion = 1;

	UPROPERTY(EditAnywhere, Category="Save System|Config")
	bool bEnableAutoSave = true;

	UPROPERTY(EditAnywhere, Category="Save System|Config", meta=(EditCondition="bEnableAutoSave", ClampMin="10.0", UIMin="10.0"))
	float AutoSaveIntervalSeconds = 180.f;

	UPROPERTY(VisibleAnywhere, Category="Save System|State")
	bool bInitialised = false;
	UEOSUnifiedSubsystem* EOSSub;

	UFUNCTION(BlueprintCallable, Category="Save System|Save") USaveSystem * GetCurrentSaveSystem() const { return CurrentSaveSystem; }

protected:
	/* ---------- Internals ---------- */

	void ExecuteLoad(bool bAsync);
	void ExecuteSave(bool bAsync);

	/** Returns true when SaveGameToSlot succeeded. */
	bool PerformSaveSync();
	void PerformSaveAsync();

	/** Autosave */
	void StartAutosaveTimer();
	void StopAutosaveTimer();

	UFUNCTION(BlueprintCallable, Category="Save System|Profiles")
	void SwitchProfileForIdentity(const FString& DisplayName, const FString& PUID, const FString& EAS);

private:
	/** In-memory save object for the active slot. */
	UPROPERTY(Transient)
	USaveSystem* CurrentSaveSystem = nullptr;

	/** Current active slot name. */
	UPROPERTY(VisibleAnywhere, Category="Save System|State")
	FString SaveSlotName = TEXT("DefaultSaveSlot");
	
	/** Registered saveables we will query on save/load. */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UObject>> RegisteredSaveables;

	/** Small debounce for spammy RequestSave calls. */
	FTimerHandle DebouncedSaveHandle;

	/** Autosave recurring timer. */
	FTimerHandle AutosaveTimerHandle;

	/** Pending flag used by debounce. */
	bool bSavePending = false;

	/** Prevent overlapping async saves. */
	bool bSaveInFlight = false;

	/** Optional verbose logging. */
	bool bPrintDebugOutput = false;
};
