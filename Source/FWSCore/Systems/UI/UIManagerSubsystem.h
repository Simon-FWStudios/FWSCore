#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "FWSCore/Shared/FWSWorldPhase.h"
#include "UIManagerSubsystem.generated.h"

class UUnifiedSubsystemManager;
class UMainMenuWidget;
class ULobbyWidget;
class UPauseMenuWidget;
struct FUnifiedLobbySummary;

USTRUCT(BlueprintType)
struct FHostParams
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite) FString Name;
    UPROPERTY(BlueprintReadWrite) FString Map;
    UPROPERTY(BlueprintReadWrite) FString Mode;
    UPROPERTY(BlueprintReadWrite) int32   MaxMembers    = 4;
    UPROPERTY(BlueprintReadWrite) bool    bPresence     = true;
    UPROPERTY(BlueprintReadWrite) bool    bAllowInvites = true;
};

/** Lightweight app-level UI glue (menus, status, toasts, lobby, pause). */
UCLASS(Config=Game, DefaultConfig)
class FWSCORE_API UUIManagerSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // ---- Lifecycle ----
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    UFUNCTION() void OnTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Reason);
    UFUNCTION() void OnNetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Reason);
    UFUNCTION() void OnPreLoadMap(const FWorldContext& WorldContext, const FString& MapName );
    UFUNCTION() void OnPostLoadMap(UWorld* NewWorld);
    virtual void Deinitialize() override;

    // ---- Accessors ----
    UFUNCTION(BlueprintPure, Category="UI")
    UMainMenuWidget* GetMainMenuWidget() const { return MainMenuWidget; }

    // ---- Screen routing ----
    UFUNCTION(BlueprintCallable, Category="UI")
    void ShowMainMenu(); // adds to viewport if needed and shows Home

    UFUNCTION(BlueprintCallable, Category="UI")
    void ShowScreen(FName ScreenName); // "Home","Settings","Character","Profile","Play","Confirm","Log"

    // ---- Actions (thin forwarding to Unified) ----
    UFUNCTION(BlueprintCallable, Category="UI|Auth")   void Login();
    UFUNCTION(BlueprintCallable, Category="UI|Auth")   void LoginPortal();
    UFUNCTION(BlueprintCallable, Category="UI|Auth")   void Logout();
    UFUNCTION(BlueprintCallable, Category="UI|Auth")   void ShowOverlay();
    UFUNCTION(BlueprintCallable, Category="UI|Save")   void RequestSave(bool bAsync);
    UFUNCTION(BlueprintCallable, Category="UI|Save")   void RequestLoad(bool bAsync);

    // Lobby helpers (exposed to LobbyWidget)
    UFUNCTION(BlueprintCallable, Category="UI|Lobby")  void InviteFriendsToLobby();

    // Pause menu control (bind this to Enhanced Input in your settings menu)
    UFUNCTION(BlueprintCallable, Category="UI|Match")  void TogglePauseMenu();

    // ---- Status / busy / log ----
    UFUNCTION(BlueprintCallable, Category="UI")        void SetBusy(bool bInBusy, const FString& Reason = TEXT(""));
    UFUNCTION(BlueprintCallable, Category="UI")        void SetStatus(const FString& Line);
    UFUNCTION(BlueprintCallable, Category="UI")        void AppendLog(const FString& Line);
    UFUNCTION(BlueprintCallable, Category="UI")        void ShowToast(const FString& Message, float Duration = 2.f);

    UFUNCTION(BlueprintCallable, Category="UI")        void SetPhase(uint8 InPhase);

    // ---- Play Flows ----
    UFUNCTION(BlueprintCallable, Category="UI|Play")   void HostOnline(const FHostParams& Params);
    UFUNCTION(BlueprintCallable, Category="UI|Play")   void JoinByLobbyId(const FString& LobbyId);
    UFUNCTION(BlueprintCallable, Category="UI|Play")   void LeaveLobbyToMenu();

    // --- WIDGET CLASSES (config-driven, safe defaults) ---
    UPROPERTY(Config, EditDefaultsOnly, Category="UI|Classes", meta=(AllowedClasses="/Script/UMG.UserWidget"))
    TSoftClassPtr<class UMainMenuWidget>   MainMenuWidgetClass;

    UPROPERTY(Config, EditDefaultsOnly, Category="UI|Classes", meta=(AllowedClasses="/Script/UMG.UserWidget"))
    TSoftClassPtr<class ULobbyWidget>      LobbyWidgetClass;

    UPROPERTY(Config, EditDefaultsOnly, Category="UI|Classes", meta=(AllowedClasses="/Script/UMG.UserWidget"))
    TSoftClassPtr<class UPauseMenuWidget>  PauseWidgetClass;

    // --- LEVELS (config-driven) ---
    // Use UWorld soft refs so you can point at assets like /Game/Maps/Lobby
    UPROPERTY(Config, EditDefaultsOnly, Category="UI|Levels", meta=(AllowedClasses="/Script/Engine.World"))
    TSoftObjectPtr<class UWorld>           MainMenuLevel;

    UPROPERTY(Config, EditDefaultsOnly, Category="UI|Levels", meta=(AllowedClasses="/Script/Engine.World"))
    TSoftObjectPtr<class UWorld>           LobbyLevel;

    UPROPERTY(Config, EditDefaultsOnly, Category="UI|Levels", meta=(AllowedClasses="/Script/Engine.World"))
    TSoftObjectPtr<class UWorld>           MatchLevel; 

    // === Enhanced Input entry points (called by PlayerController) ===
    UFUNCTION(BlueprintCallable, Category="UI|Input")  void OnUIInput_TogglePause(APlayerController* PC);
    UFUNCTION(BlueprintCallable, Category="UI|Input")  void OnUIInput_Back(APlayerController* PC);
    UFUNCTION(BlueprintCallable, Category="UI|Input")  void OnUIInput_Accept(APlayerController* PC);

    // Optional multicast if UMG wants to react
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPauseShown, bool, bShown);
    UPROPERTY(BlueprintAssignable, Category="UI|Events")
    FOnPauseShown OnPauseShown;
    // Level travel helpers
    void TravelToMainMenuLevel();
    void TravelToLobbyLevel();
    void TravelToMatchLevel();
protected:
    // Unified facade
    UPROPERTY() TObjectPtr<UUnifiedSubsystemManager> Unified = nullptr;

    // Host flow state
    UPROPERTY(Transient) FString PendingHostMapBase;

    // Pause menu state
    UPROPERTY(Transient) bool bPauseMenuVisible = false;
    void SetPauseMenuVisible(APlayerController* PC, bool bShow);

    // Internal helpers
    class UMainMenuWidget*  EnsureMainMenuWidget();
    UFUNCTION() class ULobbyWidget*     EnsureLobbyWidget();
    UFUNCTION() class UPauseMenuWidget* EnsurePauseWidget();

    void ShowLobbyUI();
    void HideLobbyUI();
    void ShowPauseUI();
    void HidePauseUI();

    void ApplyUIOnlyInput();
    void ApplyGameOnlyInput();

    void BindUnifiedDelegates();
    void UnbindUnifiedDelegates();

    void BeginAsync(const FString& Status);
    void EndAsync(const FString& Status);

    // Unified callbacks
    UFUNCTION() void HandleLobbyCreated(bool bSuccess, const FString& Error);
    UFUNCTION() void HandleUnifiedAuthChanged(bool bLoggedIn, const FString& Message);
    UFUNCTION() void HandleAuthChanged(bool bLoggedIn, const FString& Msg);
    UFUNCTION() void HandleUnifiedSettingsLoaded(FPlayerSettings Settings);
    UFUNCTION() void HandleUnifiedSettingsApplied(FPlayerSettings Settings);
    UFUNCTION() void HandleUnifiedLobbySummariesWrapped(const TArray<FUnifiedLobbySummary>& Lobbies);
    UFUNCTION() void HandlePostLoadMap(UWorld* LoadedWorld);
    UFUNCTION() void HandleEOS_LobbyJoined();
    UFUNCTION() void HandleEOS_LobbyLeftOrDestroyed();

private:
    UPROPERTY() UMainMenuWidget*  MainMenuWidget = nullptr;
    UPROPERTY() ULobbyWidget*     LobbyWidget    = nullptr;
    UPROPERTY() UPauseMenuWidget* PauseWidget    = nullptr;
    TOptional<EWorldPhase> PendingPhase;
    static FName LevelNameFrom(const TSoftObjectPtr<UWorld>& WorldRef)
    {
        return WorldRef.IsNull() ? NAME_None : FName(*WorldRef.GetAssetName());
    }
    void EnsureLobbyWidgetDeferred();
    void EnsurePauseWidgetDeferred();
};
