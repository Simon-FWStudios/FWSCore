#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MainMenuWidget.generated.h"

// Forward decls (no heavy includes here)
class UOverlay;
class UCanvasPanel;
class UWidgetSwitcher;
class UVerticalBox;
class UHorizontalBox;
class UTextBlock;
class UButton;
class UImage;
class UScrollBox;
class UEditableTextBox;
class UCircularThrobber;
class UUIManagerSubsystem;
struct FPlayerSettings;

/**
 * Code-built main menu (single widget) with screens: Home / Settings / Character / Profile / Play / Confirm / Log.
 * IMPORTANT: We build the tree in RebuildWidget() so it appears in the UMG Designer and at runtime.
 */
UCLASS()
class FWSCORE_API UMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Build Slate tree here so Designer can display it too
	virtual TSharedRef<SWidget> RebuildWidget() override;

	// Runtime init after the Slate tree exists
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// External wiring
	void SetOwningUI(UUIManagerSubsystem* InOwner) { Owner = InOwner; }

	// View switching
	UFUNCTION(BlueprintCallable, Category="UI")
	void SwitchScreen(FName ScreenName);

	// Status / log utilities
	void SetStatusLine(const FString& Line);
	void AppendLogLine(const FString& Line);
	void ShowToast(const FString& Message, float Duration);
	void SetBusy(bool bBusy);

	// Identity line (from auth)
	void SetIdentityText(const FString& Display, const FString& EpicId, const FString& PUID);

	// Settings notifications (from Unified)
	void OnSettingsLoaded(const FPlayerSettings& Settings);
	void OnSettingsApplied(const FPlayerSettings& Settings);

	// ---- Button handlers (must be UFUNCTION for dynamic binding) ----
	UFUNCTION() void OnClick_Login();
	UFUNCTION() void OnClick_LoginPortal();
	UFUNCTION() void OnClick_Logout();
	UFUNCTION() void OnClick_ShowOverlay();

	UFUNCTION() void OnClick_ShowSettings();
	UFUNCTION() void OnClick_ShowCharacter();
	UFUNCTION() void OnClick_ShowProfile();
	UFUNCTION() void OnClick_ShowPlay();
	UFUNCTION() void OnClick_ExitGame();

	UFUNCTION() void OnClick_Play_Host();
	UFUNCTION() void OnClick_Play_JoinById();

	UFUNCTION(BlueprintCallable, Category="UI")	void SetAuthState(bool bIsLoggedIn);
protected:
	// Builders (called from RebuildWidget)
	void BuildUI();
	void BuildHome();
	void BuildSettings();
	void BuildCharacter();
	void BuildProfile();
	void BuildPlay();
	void BuildConfirm();
	void BuildLog();

	
protected:
	// Owner subsystem (not UPROPERTY to avoid circular asset refs)
	UPROPERTY(Transient) UUIManagerSubsystem* Owner = nullptr;

	// Root & screens
	UPROPERTY(Transient) UWidgetSwitcher*     Switcher       = nullptr;
	UPROPERTY(Transient) UVerticalBox*       HomePanel      = nullptr;
	UPROPERTY(Transient) UVerticalBox*       SettingsPanel  = nullptr;
	UPROPERTY(Transient) UVerticalBox*       CharacterPanel = nullptr;
	UPROPERTY(Transient) UVerticalBox*       ProfilePanel   = nullptr;
	UPROPERTY(Transient) UVerticalBox*       PlayPanel      = nullptr;
	UPROPERTY(Transient) UVerticalBox*       ConfirmPanel   = nullptr;
	UPROPERTY(Transient) UVerticalBox*       LogPanel       = nullptr;

	// Common controls
	UPROPERTY(Transient) UTextBlock*         StatusText     = nullptr;
	UPROPERTY(Transient) UScrollBox*         LogScroll      = nullptr;
	UPROPERTY(Transient) UTextBlock*         IdentityText   = nullptr;
	UPROPERTY(Transient) UImage*             AvatarImage    = nullptr;
	UPROPERTY(Transient) UCircularThrobber*  BusyThrobber   = nullptr;
	UPROPERTY(Transient) UEditableTextBox*   JoinLobbyIdBox = nullptr;

	UPROPERTY(Transient) class UButton* BtnLogin = nullptr;
	UPROPERTY(Transient) class UButton* BtnLoginPortal = nullptr;
	UPROPERTY(Transient) class UButton* BtnLogout = nullptr;
};
