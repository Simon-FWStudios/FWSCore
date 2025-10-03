// PauseMenuWidget.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PauseMenuWidget.generated.h"

class UButton;
class UVerticalBox;

/**
 * Minimal pause menu:
 *  - Resume
 *  - Settings
 *  - Leave Match (you wire to return-to-menu/lobby)
 *  - Quit to Desktop
 */
UCLASS()
class FWSCORE_API UPauseMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;

protected:
	UPROPERTY(Transient) UVerticalBox* AutoRoot = nullptr;
	UPROPERTY(Transient) UButton* Btn_Resume   = nullptr;
	UPROPERTY(Transient) UButton* Btn_Settings = nullptr;
	UPROPERTY(Transient) UButton* Btn_Leave    = nullptr;
	UPROPERTY(Transient) UButton* Btn_Quit     = nullptr;

	void BuildIfMissing();

	// Click handlers
	UFUNCTION() void OnResumeClicked();
	UFUNCTION() void OnSettingsClicked();
	UFUNCTION() void OnLeaveClicked();
	UFUNCTION() void OnQuitClicked();

	UButton* MakeButton(const FString& Label, FName OnClickedUFunction) const;
};
