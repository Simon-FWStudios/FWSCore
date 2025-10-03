#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "FWSGameInstance.generated.h"

class UUnifiedSubsystemManager;
class UPlayerProfileComponent;

/**
 * Minimal base GI that locates and caches the UnifiedSubsystemManager facade.
 * Keeps legacy cached fields for compatibility with existing code.
 */
UCLASS()
class FWSCORE_API UFWSGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	// UGameInstance
	virtual void Init() override;
	virtual void Shutdown() override;

	/** Helper to build a default profile key from cached IDs (EAID/PUID or local fallback). */
	UFUNCTION(BlueprintCallable, Category="Profiles")
	FSaveProfileKey BuildProfileKey() const;

	UFUNCTION(BlueprintCallable, Category="Subsystem") UUnifiedSubsystemManager* GetUnifiedSubsystem() const { return Unified; }

protected:
	/** Refresh the cached ID/display strings from the unified facade & profile component. */
	void RefreshCachedIdStrings();

private:
	/** Unified facade (Auth + Save/Profile + Friends/Lobbies). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Unified", meta=(AllowPrivateAccess="true"))
	UUnifiedSubsystemManager* Unified = nullptr;

	// ---- Cached identity (kept with the same names you already use) ----
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="EOS", meta=(AllowPrivateAccess="true"))
	FString CachedLocalEpicId;          // EAID (from EOS), or empty if offline/guest

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="EOS", meta=(AllowPrivateAccess="true"))
	FString CachedProductUserId;        // PUID (from EOS), or empty if offline/guest

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="EOS", meta=(AllowPrivateAccess="true"))
	FString CachedDisplayName;          // From PlayerProfile settings (works online/offline)

	// Optional: keep a weak pointer to the local profile component we set meta on
	UPROPERTY(Transient)
	TWeakObjectPtr<UPlayerProfileComponent> LocalProfileComp;

	UFUNCTION()
	void HandleUnifiedAuthChanged(bool bLoggedIn, const FString& Message);

	UFUNCTION()
	void HandleUnifiedSettingsLoaded(FPlayerSettings Settings);

	UFUNCTION()
	void HandleUnifiedSettingsApplied(FPlayerSettings Settings);
};
