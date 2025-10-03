// LobbyWidget.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FWSCore/Shared/FWSTypes.h"
#include "LobbyWidget.generated.h"

class UTextBlock;
class UScrollBox;
class UVerticalBox;
class UButton;
class UEditableTextBox;

class UUnifiedSubsystemManager;

/**
 * Simple, code-built Lobby UI:
 *  - Shows lobby name/map/mode + player count
 *  - Shows live player list (GameState->PlayerArray)
 *  - Invite (EOS overlay via facade), Start (host only), Settings, Leave
 */
UCLASS()
class FWSCORE_API ULobbyWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// External nudges if you want to force a refresh from outside
	UFUNCTION(BlueprintCallable, Category="Lobby")
	void RefreshFromWorld();

protected:
	// ---- UI tree ----
	UPROPERTY(Transient) UVerticalBox* AutoRoot = nullptr;
	UPROPERTY(Transient) UTextBlock*   Text_Title = nullptr;
	UPROPERTY(Transient) UTextBlock*   Text_Subtitle = nullptr;   // map/mode/id
	UPROPERTY(Transient) UTextBlock*   Text_Count = nullptr;      // member count
	UPROPERTY(Transient) UScrollBox*   List_Players = nullptr;

	UPROPERTY(Transient) UButton*      Btn_Invite = nullptr;
	UPROPERTY(Transient) UButton*      Btn_Start  = nullptr;
	UPROPERTY(Transient) UButton*      Btn_Settings = nullptr;
	UPROPERTY(Transient) UButton*      Btn_Leave  = nullptr;

	UPROPERTY(Transient) UTextBlock* Text_Identity = nullptr;
	
	// ---- Facade / helpers ----
	UPROPERTY(Transient) UUnifiedSubsystemManager* Unified = nullptr;

	FTimerHandle TickRefreshTimer;

	// ---- Build / wire ----
	void BuildIfMissing();
	void BindEvents();
	void UnbindEvents();

	// ---- Click handlers ----
	UFUNCTION() void OnInviteClicked();
	UFUNCTION() void OnStartClicked();
	UFUNCTION() void OnSettingsClicked();
	UFUNCTION() void OnLeaveClicked();

	// ---- Subsystem event handlers (facade) ----
	UFUNCTION() void HandleLobbySummariesUpdated(const TArray<struct FUnifiedLobbySummary>& Summaries);
	UFUNCTION() void HandleLobbyCreated(bool bSuccess, const FString& Error);
	UFUNCTION() void HandleLobbyJoined();
	UFUNCTION() void HandleLobbyUpdated();
	UFUNCTION() void HandleLobbyLeftOrDestroyed();
	UFUNCTION() void HandleAuthChanged(bool bLoggedIn, const FString& Message);

	// ---- Internals ----
	void RebuildPlayerList();
	void UpdateHeaderTexts();
	void UpdateIdentityTexts();
	
	// tiny helpers
	UTextBlock* MakeLabel(const FString& Text) const;
	UButton*    MakeButton(const FString& Label, FName OnClickedUFunction) const;
};
