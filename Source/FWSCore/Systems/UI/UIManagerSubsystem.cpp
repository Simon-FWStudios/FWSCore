#include "UIManagerSubsystem.h"

#include "FWSCore.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/NetDriver.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Subsystems/SubsystemCollection.h"
#include "Blueprint/UserWidget.h"
#include "FWSCore/EOS/EOSUnifiedSubsystem.h"

#include "FWSCore/Shared/FWSWorldPhase.h"
#include "FWSCore/Systems/Unified/UnifiedSubsystemManager.h"
#include "FWSCore/Widgets/UI/MainMenuWidget.h"
#include "FWSCore/Widgets/UI/LobbyWidget.h"
#include "FWSCore/Widgets/UI/PauseMenuWidget.h"

// ===== Lifecycle ============================================================

void UUIManagerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Ensure unified first so we can bind
    Collection.InitializeDependency<UUnifiedSubsystemManager>();
    Unified = GetGameInstance() ? GetGameInstance()->GetSubsystem<UUnifiedSubsystemManager>() : nullptr;

    if (!Unified)
    {
        UE_LOG(LogUIManager, Warning, TEXT("[UIManager] UnifiedSubsystemManager still null after dependency init."));
    }
    else
    {
        BindUnifiedDelegates();
    }
    
    EnsureMainMenuWidget();
    ShowMainMenu();

    if (GEngine)
    {
        GEngine->OnTravelFailure().AddUObject(this, &UUIManagerSubsystem::OnTravelFailure);
        GEngine->OnNetworkFailure().AddUObject(this, &UUIManagerSubsystem::OnNetworkFailure);
    }

    FCoreUObjectDelegates::PreLoadMapWithContext.AddUObject(this, &UUIManagerSubsystem::OnPreLoadMap);
    FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UUIManagerSubsystem::OnPostLoadMap);
}

void UUIManagerSubsystem::Deinitialize()
{
    UnbindUnifiedDelegates();

    if (MainMenuWidget)  { MainMenuWidget->RemoveFromParent();  MainMenuWidget  = nullptr; }
    if (LobbyWidget)     { LobbyWidget->RemoveFromParent();     LobbyWidget     = nullptr; }
    if (PauseWidget)     { PauseWidget->RemoveFromParent();     PauseWidget     = nullptr; }

    Unified = nullptr;
    Super::Deinitialize();
}

// ===== Failure hooks ========================================================

void UUIManagerSubsystem::OnTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Reason)
{
    UE_LOG(LogUIManager, Error, TEXT("[TravelFailure] %s : %s"),
        *UEnum::GetValueAsString(Type), *Reason);
}

void UUIManagerSubsystem::OnNetworkFailure(UWorld* World, UNetDriver* Driver,
    ENetworkFailure::Type Type, const FString& Reason)
{
    UE_LOG(LogUIManager, Error, TEXT("[NetworkFailure] %s : %s"),
        *UEnum::GetValueAsString(Type), *Reason);
}

void UUIManagerSubsystem::OnPreLoadMap(const FWorldContext& WorldContext, const FString& MapName)
{
    UE_LOG(LogUIManager, Display, TEXT("[PreLoadMap] %s"), *MapName);
    // Hide UI during loads
    if (MainMenuWidget) MainMenuWidget->SetVisibility(ESlateVisibility::Collapsed);
    if (LobbyWidget)    LobbyWidget->SetVisibility(ESlateVisibility::Collapsed);
    if (PauseWidget)    PauseWidget->SetVisibility(ESlateVisibility::Collapsed);
}

void UUIManagerSubsystem::OnPostLoadMap(UWorld* NewWorld)
{

    if (PendingPhase.IsSet())
    {
        switch (PendingPhase.GetValue())
        {
        case EWorldPhase::Lobby:    ShowLobbyUI(); break;
        case EWorldPhase::MainMenu: ShowMainMenu(); break;
        case EWorldPhase::Match:
        default:
            if (MainMenuWidget) MainMenuWidget->SetVisibility(ESlateVisibility::Collapsed);
            if (LobbyWidget)    LobbyWidget->SetVisibility(ESlateVisibility::Collapsed);
            if (PauseWidget)    PauseWidget->SetVisibility(ESlateVisibility::Collapsed);
            ApplyGameOnlyInput();
            if (UWorld* W = GetWorld()) UGameplayStatics::SetGamePaused(W, false);
            break;
        }
        PendingPhase.Reset();
        return;
    }
    
    const FName Loaded = FName(*UGameplayStatics::GetCurrentLevelName(NewWorld, /*bRemovePrefix*/true));
    const FName MainName  = LevelNameFrom(MainMenuLevel);
    const FName LobbyName = LevelNameFrom(LobbyLevel);
    const FName MatchName = LevelNameFrom(MatchLevel);

    UE_LOG(LogUIManager, Display, TEXT("[PostLoadMap] %s"), *Loaded.ToString());

    // Make sure widgets exist for this world
    EnsureMainMenuWidget();
    EnsureLobbyWidget();
    EnsurePauseWidget();

    // Route to the correct layer for this map
    if (Loaded == LobbyName && LobbyName != NAME_None)
    {
        ShowLobbyUI();                 // ⬅️ actually show it
        SetStatus(TEXT("Lobby"));
    }
    else if (Loaded == MainName && MainName != NAME_None)
    {
        ShowMainMenu();
        SetStatus(TEXT("Main Menu"));
    }
    else
    {
        // Treat everything else as “match” by default
        if (MainMenuWidget) MainMenuWidget->SetVisibility(ESlateVisibility::Collapsed);
        if (LobbyWidget)    LobbyWidget->SetVisibility(ESlateVisibility::Collapsed);
        if (PauseWidget)    PauseWidget->SetVisibility(ESlateVisibility::Collapsed);
        ApplyGameOnlyInput();
        if (UWorld* W = GetWorld()) UGameplayStatics::SetGamePaused(W, false);
        SetStatus(TEXT("Match"));
    }
}

// ===== Widget Ensurers ======================================================

UMainMenuWidget* UUIManagerSubsystem::EnsureMainMenuWidget()
{
    if (MainMenuWidget && MainMenuWidget->IsValidLowLevelFast()) return MainMenuWidget;

    UWorld* W = GetWorld();
    if (!W || !W->GetFirstPlayerController()) return nullptr;

    UClass* UseClass = MainMenuWidgetClass.IsValid()
        ? MainMenuWidgetClass.Get()
        : MainMenuWidgetClass.LoadSynchronous();

    if (!UseClass) { UseClass = UMainMenuWidget::StaticClass(); }

    MainMenuWidget = CreateWidget<UMainMenuWidget>(W->GetFirstPlayerController(), UseClass);

    // 🔧 IMPORTANT: restore the owning pointer so the WBP can call back into the subsystem.
    if (MainMenuWidget)
    {
        MainMenuWidget->SetOwningUI(this);
        UE_LOG(LogUIManager, Log, TEXT("[UI] MainMenuWidget created and OwningUI set: %s"), *GetNameSafe(this));
    }

    return MainMenuWidget;
}

ULobbyWidget* UUIManagerSubsystem::EnsureLobbyWidget()
{
    if (LobbyWidget && LobbyWidget->IsValidLowLevelFast()) return LobbyWidget;

    UWorld* W = GetWorld();
    if (!W || W->IsNetMode(NM_DedicatedServer)) return nullptr;

    APlayerController* PC = W->GetFirstPlayerController();
    UGameInstance* GI = GetGameInstance();

    UClass* UseClass = LobbyWidgetClass.IsValid() ? LobbyWidgetClass.Get()
                                                  : LobbyWidgetClass.LoadSynchronous();
    if (!UseClass) { UseClass = ULobbyWidget::StaticClass(); }

    // Defer if neither PC nor GI is ready (rare, but can happen during map load)
    if (!PC && !GI)
    {
        W->GetTimerManager().SetTimerForNextTick(
            FTimerDelegate::CreateUObject(this, &UUIManagerSubsystem::EnsureLobbyWidgetDeferred));
        UE_LOG(LogUIManager, Verbose, TEXT("[UI] EnsureLobbyWidget: deferring, PC/GI not ready."));
        return nullptr;
    }

    // Prefer owning PlayerController; fallback to GameInstance
    LobbyWidget = PC
        ? CreateWidget<ULobbyWidget>(PC, UseClass)
        : CreateWidget<ULobbyWidget>(GI, UseClass);

    if (LobbyWidget)
    {
        UE_LOG(LogUIManager, Log, TEXT("[UI] LobbyWidget created (%s)."), *GetNameSafe(UseClass));
    }
    return LobbyWidget;
}

void UUIManagerSubsystem::EnsureLobbyWidgetDeferred()
{
    EnsureLobbyWidget();
}

UPauseMenuWidget* UUIManagerSubsystem::EnsurePauseWidget()
{
    if (PauseWidget && PauseWidget->IsValidLowLevelFast()) return PauseWidget;

    UWorld* W = GetWorld();
    if (!W || W->IsNetMode(NM_DedicatedServer)) return nullptr;

    APlayerController* PC = W->GetFirstPlayerController();
    UGameInstance* GI = GetGameInstance();

    UClass* UseClass = PauseWidgetClass.IsValid() ? PauseWidgetClass.Get()
                                                  : PauseWidgetClass.LoadSynchronous();
    if (!UseClass) { UseClass = UPauseMenuWidget::StaticClass(); }

    if (!PC && !GI)
    {
        W->GetTimerManager().SetTimerForNextTick(
            FTimerDelegate::CreateUObject(this, &UUIManagerSubsystem::EnsurePauseWidgetDeferred));
        UE_LOG(LogUIManager, Verbose, TEXT("[UI] EnsurePauseWidget: deferring, PC/GI not ready."));
        return nullptr;
    }

    PauseWidget = PC
        ? CreateWidget<UPauseMenuWidget>(PC, UseClass)
        : CreateWidget<UPauseMenuWidget>(GI, UseClass);

    if (PauseWidget)
    {
        UE_LOG(LogUIManager, Log, TEXT("[UI] PauseMenuWidget created (%s)."), *GetNameSafe(UseClass));
    }
    return PauseWidget;
}

void UUIManagerSubsystem::EnsurePauseWidgetDeferred()
{
    EnsurePauseWidget();
}

// ---------- Level travel (by soft world ref) ----------

void UUIManagerSubsystem::TravelToMainMenuLevel()
{
    PendingPhase = EWorldPhase::MainMenu;
    if (UWorld* W = GetWorld())
    {
        const FName LevelName = LevelNameFrom(MainMenuLevel);
        if (LevelName != NAME_None) UGameplayStatics::OpenLevel(W, LevelName);
    }
}

void UUIManagerSubsystem::TravelToLobbyLevel()
{
    PendingPhase = EWorldPhase::Lobby;
    if (UWorld* W = GetWorld())
    {
        const FName LevelName = LevelNameFrom(LobbyLevel);
        if (LevelName != NAME_None) UGameplayStatics::OpenLevel(W, LevelName);
    }
}

void UUIManagerSubsystem::TravelToMatchLevel()
{
    PendingPhase = EWorldPhase::Match;
    if (UWorld* W = GetWorld())
    {
        const FName LevelName = LevelNameFrom(MatchLevel);
        if (LevelName != NAME_None) UGameplayStatics::OpenLevel(W, LevelName);
    }
}

// ===== Input Helpers ========================================================

void UUIManagerSubsystem::ApplyUIOnlyInput()
{
    if (UWorld* W = GetWorld())
    {
        if (APlayerController* PC = W->GetFirstPlayerController())
        {
            FInputModeUIOnly M;
            M.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
            PC->SetInputMode(M);
            PC->bShowMouseCursor = true;
        }
    }
}

void UUIManagerSubsystem::ApplyGameOnlyInput()
{
    if (UWorld* W = GetWorld())
    {
        if (APlayerController* PC = W->GetFirstPlayerController())
        {
            FInputModeGameOnly M;
            PC->SetInputMode(M);
            PC->bShowMouseCursor = false;
        }
    }
}

// ===== Public UI Entry Points ==============================================

void UUIManagerSubsystem::ShowMainMenu()
{
    UWorld* World = GetWorld();
    if (!World || World->IsNetMode(NM_DedicatedServer) || !GEngine || !GEngine->GameViewport)
    {
        // Defer until viewport ready
        if (World)
        {
            World->GetTimerManager().SetTimerForNextTick(
                FTimerDelegate::CreateUObject(this, &UUIManagerSubsystem::ShowMainMenu));
        }
        return;
    }

    EnsureMainMenuWidget();
    if (!MainMenuWidget) return;

    // Hide other layers
    if (LobbyWidget)  LobbyWidget->SetVisibility(ESlateVisibility::Collapsed);
    if (PauseWidget)  PauseWidget->SetVisibility(ESlateVisibility::Collapsed);

    if (!MainMenuWidget->IsInViewport())
    {
        MainMenuWidget->AddToPlayerScreen(10);
    }
    MainMenuWidget->SetVisibility(ESlateVisibility::Visible);

    // Main menu implies unpaused UI-only input
    if (UWorld* W = GetWorld()) UGameplayStatics::SetGamePaused(W, false);
    ApplyUIOnlyInput();
}

void UUIManagerSubsystem::ShowScreen(FName ScreenName)
{
    if (MainMenuWidget)
    {
        MainMenuWidget->SwitchScreen(ScreenName);
    }
}

void UUIManagerSubsystem::InviteFriendsToLobby()
{
    if (!Unified) return;
    Unified->InviteToLobby(); // opens overlay or performs invite via unified facade
}

void UUIManagerSubsystem::TogglePauseMenu()
{
    EnsurePauseWidget();
    if (!PauseWidget) return;

    if (!PauseWidget->IsInViewport())
    {
        PauseWidget->AddToPlayerScreen(20);
    }

    bPauseMenuVisible = !bPauseMenuVisible;
    if (bPauseMenuVisible)
    {
        ShowPauseUI();
    }
    else
    {
        HidePauseUI();
    }
}

// ===== Status / Busy / Log =================================================

void UUIManagerSubsystem::SetBusy(bool bInBusy, const FString& Reason)
{
    if (MainMenuWidget)
    {
        MainMenuWidget->SetBusy(bInBusy);
        if (!Reason.IsEmpty())
        {
            MainMenuWidget->SetStatusLine(Reason);
        }
    }
}

void UUIManagerSubsystem::SetStatus(const FString& Line)
{
    if (MainMenuWidget)
    {
        MainMenuWidget->SetStatusLine(Line);
    }
}

void UUIManagerSubsystem::AppendLog(const FString& Line)
{
    if (MainMenuWidget)
    {
        MainMenuWidget->AppendLogLine(Line);
    }
}

void UUIManagerSubsystem::ShowToast(const FString& Message, float Duration)
{
    if (MainMenuWidget)
    {
        MainMenuWidget->ShowToast(Message, Duration);
    }
}

// ===== Phase Routing ========================================================

void UUIManagerSubsystem::SetPhase(uint8 InPhase)
{
    const EWorldPhase Phase = static_cast<EWorldPhase>(InPhase);
    switch (Phase)
    {
    case EWorldPhase::MainMenu:
        ShowMainMenu();
        SetStatus(TEXT("Main Menu"));
        break;

    case EWorldPhase::Lobby:
        ShowLobbyUI();
        SetStatus(TEXT("Lobby"));
        break;

    case EWorldPhase::Match:
        // Ensure no menu/lobby visible; pause menu is hidden by default
        if (MainMenuWidget) MainMenuWidget->SetVisibility(ESlateVisibility::Collapsed);
        if (LobbyWidget)    LobbyWidget->SetVisibility(ESlateVisibility::Collapsed);
        if (PauseWidget)    PauseWidget->SetVisibility(ESlateVisibility::Collapsed);
        ApplyGameOnlyInput();
        if (UWorld* W = GetWorld()) UGameplayStatics::SetGamePaused(W, false);
        SetStatus(TEXT("Match"));
        break;
    }
}

// ===== Lobby / Match helpers ===============================================

void UUIManagerSubsystem::ShowLobbyUI()
{
    if (!EnsureLobbyWidget())
    {
        if (UWorld* W = GetWorld())
        {
            W->GetTimerManager().SetTimerForNextTick(
                FTimerDelegate::CreateUObject(this, &UUIManagerSubsystem::ShowLobbyUI));
            UE_LOG(LogUIManager, Verbose, TEXT("[UI] ShowLobbyUI: retry next tick (widget not ready)."));
        }
        return;
    }

    if (!LobbyWidget->IsInViewport()) LobbyWidget->AddToPlayerScreen(15);
    LobbyWidget->SetVisibility(ESlateVisibility::Visible);

    if (MainMenuWidget) MainMenuWidget->SetVisibility(ESlateVisibility::Collapsed);
    if (PauseWidget)    PauseWidget->SetVisibility(ESlateVisibility::Collapsed);

    ApplyUIOnlyInput();
    if (UWorld* W = GetWorld()) UGameplayStatics::SetGamePaused(W, false);

    UE_LOG(LogUIManager, Log, TEXT("[UI] ShowLobbyUI: visible."));
}

void UUIManagerSubsystem::HideLobbyUI()
{
    if (LobbyWidget)
    {
        LobbyWidget->SetVisibility(ESlateVisibility::Collapsed);
    }
}

void UUIManagerSubsystem::ShowPauseUI()
{
    if (!PauseWidget) return;

    if (!PauseWidget->IsInViewport())
    {
        PauseWidget->AddToPlayerScreen(20);
    }
    PauseWidget->SetVisibility(ESlateVisibility::Visible);
    ApplyUIOnlyInput();
    if (UWorld* W = GetWorld()) UGameplayStatics::SetGamePaused(W, true);
}

void UUIManagerSubsystem::HidePauseUI()
{
    if (!PauseWidget) return;
    PauseWidget->SetVisibility(ESlateVisibility::Collapsed);
    ApplyGameOnlyInput();
    if (UWorld* W = GetWorld()) UGameplayStatics::SetGamePaused(W, false);
}

// ===== Unified delegate wiring =============================================

void UUIManagerSubsystem::BindUnifiedDelegates()
{
    if (!Unified) return;

    Unified->OnAuthChanged.AddDynamic(this, &UUIManagerSubsystem::HandleUnifiedAuthChanged);
    Unified->OnAuthChanged.AddDynamic(this, &UUIManagerSubsystem::HandleAuthChanged);
    Unified->OnSettingsLoaded.AddDynamic(this, &UUIManagerSubsystem::HandleUnifiedSettingsLoaded);
    Unified->OnSettingsApplied.AddDynamic(this, &UUIManagerSubsystem::HandleUnifiedSettingsApplied);
    Unified->OnLobbySummariesUpdated_U.AddDynamic(this, &UUIManagerSubsystem::HandleUnifiedLobbySummariesWrapped);
    Unified->OnLobbyCreated.AddDynamic(this, &UUIManagerSubsystem::HandleLobbyCreated);
    // Travel to Lobby map when we successfully join
    Unified->OnLobbyJoined.AddUniqueDynamic(this, &UUIManagerSubsystem::HandleEOS_LobbyJoined);
    // Optionally: after leaving/destroyed, go back to Main Menu
    Unified->OnLobbyLeftOrDestroyed.AddUniqueDynamic(this, &UUIManagerSubsystem::HandleEOS_LobbyLeftOrDestroyed);
    FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UUIManagerSubsystem::HandlePostLoadMap);
}

void UUIManagerSubsystem::UnbindUnifiedDelegates()
{
    if (!Unified) return;
    Unified->OnAuthChanged.RemoveAll(this);
    Unified->OnSettingsLoaded.RemoveAll(this);
    Unified->OnSettingsApplied.RemoveAll(this);
    Unified->OnLobbySummariesUpdated_U.RemoveAll(this);
    FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);
    Unified->OnLobbyJoined.RemoveAll(this);
    Unified->OnLobbyLeftOrDestroyed.RemoveAll(this);
    Unified = nullptr;
}

// ===== Unified callbacks ====================================================

void UUIManagerSubsystem::HandleLobbyCreated(bool bSuccess, const FString& Error)
{
    if (bSuccess)
    {
        AppendLog(TEXT("[Lobby] Created successfully."));
        // Switch to the Lobby UI immediately
        SetPhase(static_cast<uint8>(EWorldPhase::Lobby));
        EndAsync(TEXT("Lobby created"));
    }
    else
    {
        AppendLog(FString::Printf(TEXT("[Lobby] Create failed: %s"), *Error));
        ShowToast(TEXT("Lobby create failed. Check EOS permissions for Lobbies/Presence."), 4.f);
        EndAsync(TEXT("Lobby create failed"));
        if (MainMenuWidget) { MainMenuWidget->SwitchScreen("Play"); }
    }
}

void UUIManagerSubsystem::HandleUnifiedAuthChanged(bool bLoggedIn, const FString& Message)
{
    EndAsync(bLoggedIn ? TEXT("Logged in") : TEXT("Logged out"));
    SetStatus(Message.IsEmpty() ? (bLoggedIn ? TEXT("Logged in") : TEXT("Logged out")) : Message);

    if (MainMenuWidget && Unified)
    {
        MainMenuWidget->SetIdentityText(
            Unified->GetDisplayName(),
            Unified->GetEpicAccountId(),
            Unified->GetProductUserId()
        );
    }
}

void UUIManagerSubsystem::HandleAuthChanged(bool bLoggedIn, const FString& Msg)
{
    if (MainMenuWidget)
    {
        MainMenuWidget->SetAuthState(bLoggedIn);
        if (Unified)
        {
            MainMenuWidget->SetIdentityText(
                Unified->GetDisplayName(),
                Unified->GetEpicAccountId(),
                Unified->GetProductUserId());
        }
        if (!Msg.IsEmpty()) { MainMenuWidget->AppendLogLine(Msg); }
    }
}

void UUIManagerSubsystem::HandleUnifiedSettingsLoaded(FPlayerSettings Settings)
{
    EndAsync(TEXT("Settings loaded"));
    if (MainMenuWidget) { MainMenuWidget->OnSettingsLoaded(Settings); }
}

void UUIManagerSubsystem::HandleUnifiedSettingsApplied(FPlayerSettings Settings)
{
    EndAsync(TEXT("Settings applied"));
    if (MainMenuWidget) { MainMenuWidget->OnSettingsApplied(Settings); }
}

void UUIManagerSubsystem::HandleUnifiedLobbySummariesWrapped(const TArray<FUnifiedLobbySummary>& Lobbies)
{
    EnsureLobbyWidget();
}

void UUIManagerSubsystem::HandlePostLoadMap(UWorld* /*LoadedWorld*/)
{
    // After seamless travel or manual travel, ensure the right widget exists for the world
    EnsureMainMenuWidget();
    EnsureLobbyWidget();
    EnsurePauseWidget();
}

// ===== Top-level UI actions =================================================

void UUIManagerSubsystem::Login()       { if (Unified) Unified->Login();       BeginAsync(TEXT("Logging in..."));   }
void UUIManagerSubsystem::LoginPortal() { if (Unified) Unified->LoginPortal(); BeginAsync(TEXT("Opening portal...")); }
void UUIManagerSubsystem::Logout()      { if (Unified) Unified->Logout();      BeginAsync(TEXT("Logging out..."));  }
void UUIManagerSubsystem::ShowOverlay() { if (Unified) Unified->ShowOverlay(); }

void UUIManagerSubsystem::RequestSave(bool bAsync) { if (Unified) Unified->RequestSave(bAsync);  BeginAsync(TEXT("Saving...")); }
void UUIManagerSubsystem::RequestLoad(bool bAsync) { if (Unified) Unified->RequestLoad(bAsync);  BeginAsync(TEXT("Loading...")); }

// ---- Play flows ----

void UUIManagerSubsystem::HostOnline(const FHostParams& Params)
{
    if (!Unified) return;

    // Create & switch to lobby
    BeginAsync(TEXT("Creating lobby..."));
    Unified->CreateLobbyWithParams(Params.Name, Params.Map, Params.Mode, Params.MaxMembers, Params.bPresence, Params.bAllowInvites);
}

void UUIManagerSubsystem::JoinByLobbyId(const FString& LobbyId)
{
    if (!Unified) return;
    BeginAsync(FString::Printf(TEXT("Joining lobby %s"), *LobbyId));
    Unified->JoinLobby(LobbyId);
}

void UUIManagerSubsystem::LeaveLobbyToMenu()
{
    if (Unified)
    {
        BeginAsync(TEXT("Leaving lobby..."));
        Unified->LeaveLobby();
    }
}

void UUIManagerSubsystem::BeginAsync(const FString& Status)
{
    // Show a busy overlay and log what we’re starting
    if (!Status.IsEmpty())
    {
        AppendLog(Status);
        SetStatus(Status);
    }
    SetBusy(true);
}

void UUIManagerSubsystem::EndAsync(const FString& Status)
{
    // Clear busy overlay and optionally update status/log
    if (!Status.IsEmpty())
    {
        SetStatus(Status);
        AppendLog(Status);
    }
    SetBusy(false);
}

// === Enhanced Input entry points ===========================================

void UUIManagerSubsystem::OnUIInput_TogglePause(APlayerController* PC)
{
    if (!bPauseMenuVisible)
    {
        ShowPauseUI();
        return;
    }
    HidePauseUI();
}

void UUIManagerSubsystem::OnUIInput_Back(APlayerController* PC)
{
    // Priority: close pause overlay if open
    if (bPauseMenuVisible)
    {
        HidePauseUI();
        return;
    }
}

void UUIManagerSubsystem::OnUIInput_Accept(APlayerController* /*PC*/)
{
    AppendLog(TEXT("[UIInput] Accept."));
}

void UUIManagerSubsystem::HandleEOS_LobbyJoined()
{
    // Travel to your configured Lobby level
    TravelToLobbyLevel(); // uses your INI-configured soft reference
}

void UUIManagerSubsystem::HandleEOS_LobbyLeftOrDestroyed()
{
    EndAsync(TEXT("Left lobby"));
    TravelToMainMenuLevel();   // INI-driven map
    ShowMainMenu();            // optional: show menu UI immediately
}