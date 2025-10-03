#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FWSPlayerController.generated.h"

struct FPlayerProfileMeta;
class UPlayerProfileComponent;
class UEOSUnifiedSubsystem;
class AFWSPlayerState;
class UInputMappingContext;
class UInputAction;
struct FInputActionValue;

UCLASS()
class FWSCORE_API AFWSPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AFWSPlayerController();
	
	virtual void BeginPlay() override;
	void EndPlay(EEndPlayReason::Type Reason);
	virtual void SetupInputComponent() override;

	UFUNCTION(BlueprintPure, Category="EOS")
	UUnifiedSubsystemManager* GetUnifiedSubsystem() const { return Subsys; }

	UFUNCTION()  // must be UFUNCTION for AddDynamic
	void HandleProfileMetaUpdated(FPlayerProfileMeta MetaSnapshot);

	UFUNCTION() void HandleProfileKeyChanged(const FSaveProfileKey& Key);
	UFUNCTION() void HandleSettingsLoaded(const FPlayerSettings& Settings);
	UFUNCTION() void HandleSettingsApplied(const FPlayerSettings& Settings);

	/** High-level UI mapping context that contains bindings for the actions below. */
	UPROPERTY(EditDefaultsOnly, Category="Input|UI")
	TSoftObjectPtr<UInputMappingContext> UIInputMapping;

	/** Toggle pause / menu (Match phase). */
	UPROPERTY(EditDefaultsOnly, Category="Input|UI")
	TSoftObjectPtr<UInputAction> IA_TogglePause;

	/** “Back / Cancel” action (navigate back / close menus). */
	UPROPERTY(EditDefaultsOnly, Category="Input|UI")
	TSoftObjectPtr<UInputAction> IA_Back;

	/** “Accept / Confirm” action (press button, confirm dialogs). */
	UPROPERTY(EditDefaultsOnly, Category="Input|UI")
	TSoftObjectPtr<UInputAction> IA_Accept;

	UFUNCTION(Server, Reliable)
	void Server_SetActiveCharacterForSession(const FString& CharacterId);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="EOS")
	UUnifiedSubsystemManager* Subsys = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Profile")
	UPlayerProfileComponent* PlayerProfile = nullptr;

	UFUNCTION(BlueprintCallable, Category="Input")
	void ApplyPhaseInput(EWorldPhase Phase);

	UFUNCTION(BlueprintPure, Category="Input")
	EWorldPhase DetectPhaseFromWorld() const;

	
private:
	// Cached loaded assets so we don't sync load repeatedly
	UPROPERTY(Transient) UInputMappingContext* UIInputMappingPtr = nullptr;
	UPROPERTY(Transient) UInputAction*         IA_TogglePausePtr = nullptr;
	UPROPERTY(Transient) UInputAction*         IA_BackPtr        = nullptr;
	UPROPERTY(Transient) UInputAction*         IA_AcceptPtr      = nullptr;

	void AddUIMappingContextIfLocal();

	// Action handlers
	void OnTogglePause(const FInputActionValue& Value);
	void OnBack(const FInputActionValue& Value);
	void OnAccept(const FInputActionValue& Value);
};
