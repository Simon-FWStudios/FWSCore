#include "FWSPlayerController.h"
#include "FWSCore.h"
#include "FWSGameInstance.h"
#include "GameFramework/GameStateBase.h"
#include "FWSLobbyGameState.h"
#include "FWSMatchGameState.h"
#include "FWSCore/Player/PlayerProfileComponent.h"
#include "FWSCore/Shared/FWSWorldPhase.h"
#include "FWSCore/Systems/UI/UIManagerSubsystem.h"
#include "FWSCore/Systems/Unified/UnifiedSubsystemManager.h"
#include "Kismet/GameplayStatics.h"
#include "Components/InputComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputTriggers.h" 
#include "FWSCore/Character/CharacterProfileComponent.h"
#include "FWSCore/Character/CharacterRPGComponent.h"
#include "FWSCore/Systems/UI/UIManagerSubsystem.h" 
#include "GameFramework/PlayerState.h"

AFWSPlayerController::AFWSPlayerController()
{
	// Enable ticking if you need it
	PrimaryActorTick.bCanEverTick = false;
	PlayerProfile = CreateDefaultSubobject<UPlayerProfileComponent>(TEXT("PlayerProfile"));
	bShowMouseCursor = true;
	
	if (PlayerProfile)
	{
		// Pick the events you want – examples:
		PlayerProfile->OnProfileKeyChanged.AddUniqueDynamic(this, &AFWSPlayerController::HandleProfileKeyChanged);
		PlayerProfile->OnSettingsLoaded.AddUniqueDynamic(this, &AFWSPlayerController::HandleSettingsLoaded);
		PlayerProfile->OnSettingsApplied.AddUniqueDynamic(this, &AFWSPlayerController::HandleSettingsApplied);
	}
}

void AFWSPlayerController::BeginPlay()
{
	Super::BeginPlay();
	ApplyPhaseInput(DetectPhaseFromWorld());
	if (UFWSGameInstance* GI = GetGameInstance<UFWSGameInstance>())
	{
		Subsys = GI->GetUnifiedSubsystem();
	}

#if WITH_EDITOR
	if (!Subsys)
	{
		UE_LOG(LogFWSCore, Warning, TEXT("[BasePC] Unified subsystem not found. Ensure plugin/module is loaded."));
	}
#endif

	// Ensure we have a PlayerProfileComponent and let the unified facade own the wiring
	if (!PlayerProfile)
	{
		PlayerProfile = FindComponentByClass<UPlayerProfileComponent>();
		if (!PlayerProfile)
		{
			PlayerProfile = CreateDefaultSubobject<UPlayerProfileComponent>(TEXT("PlayerProfile"));
		}
	}

	// Local controller 0 is typical for single-player/editor PIE
	if (Subsys)
	{
		if (UPlayerProfileComponent* Forwarded = Subsys->GetOrCreateLocalPlayerProfile(/*ControllerId*/0))
		{
			// (Optional) also listen locally if you want controller-scoped reactions for UI
			PlayerProfile = Forwarded;
			PlayerProfile->OnProfileKeyChanged.AddUniqueDynamic(this, &AFWSPlayerController::HandleProfileKeyChanged);
			PlayerProfile->OnSettingsLoaded.AddUniqueDynamic(this, &AFWSPlayerController::HandleSettingsLoaded);
			PlayerProfile->OnSettingsApplied.AddUniqueDynamic(this, &AFWSPlayerController::HandleSettingsApplied);
		}
	}

	if (UIInputMapping.IsValid() == false && UIInputMapping.ToSoftObjectPath().IsValid())
	{
		UIInputMappingPtr = UIInputMapping.LoadSynchronous();
	}
	else
	{
		UIInputMappingPtr = UIInputMapping.Get();
	}

	IA_TogglePausePtr = IA_TogglePause.IsNull() ? nullptr : IA_TogglePause.LoadSynchronous();
	IA_BackPtr        = IA_Back.IsNull()        ? nullptr : IA_Back.LoadSynchronous();
	IA_AcceptPtr      = IA_Accept.IsNull()      ? nullptr : IA_Accept.LoadSynchronous();

	AddUIMappingContextIfLocal();
}


void AFWSPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (PlayerProfile)
	{
		PlayerProfile->OnProfileKeyChanged.RemoveDynamic(this, &AFWSPlayerController::HandleProfileKeyChanged);
		PlayerProfile->OnSettingsLoaded.RemoveDynamic(this, &AFWSPlayerController::HandleSettingsLoaded);
		PlayerProfile->OnSettingsApplied.RemoveDynamic(this, &AFWSPlayerController::HandleSettingsApplied);
	}
	Super::EndPlay(Reason);
}

void AFWSPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		if (IA_TogglePausePtr)
		{
			EIC->BindAction(IA_TogglePausePtr, ETriggerEvent::Triggered, this, &AFWSPlayerController::OnTogglePause);
		}
		if (IA_BackPtr)
		{
			EIC->BindAction(IA_BackPtr, ETriggerEvent::Triggered, this, &AFWSPlayerController::OnBack);
		}
		if (IA_AcceptPtr)
		{
			EIC->BindAction(IA_AcceptPtr, ETriggerEvent::Triggered, this, &AFWSPlayerController::OnAccept);
		}
	}
}

void AFWSPlayerController::AddUIMappingContextIfLocal()
{
	if (!IsLocalController()) return;

	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Sub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			if (UIInputMappingPtr)
			{
				// Priority > default gameplay; tweak as you like.
				Sub->AddMappingContext(UIInputMappingPtr, /*Priority=*/100);
			}
		}
	}
}

void AFWSPlayerController::OnTogglePause(const FInputActionValue& /*Value*/)
{
	if (UWorld* W = GetWorld())
	{
		if (UUIManagerSubsystem* UI = GetGameInstance() ? GetGameInstance()->GetSubsystem<UUIManagerSubsystem>() : nullptr)
		{
			UI->OnUIInput_TogglePause(this);
		}
	}
}

void AFWSPlayerController::OnBack(const FInputActionValue& /*Value*/)
{
	if (UWorld* W = GetWorld())
	{
		if (UUIManagerSubsystem* UI = GetGameInstance() ? GetGameInstance()->GetSubsystem<UUIManagerSubsystem>() : nullptr)
		{
			UI->OnUIInput_Back(this);
		}
	}
}

void AFWSPlayerController::OnAccept(const FInputActionValue& /*Value*/)
{
	if (UWorld* W = GetWorld())
	{
		if (UUIManagerSubsystem* UI = GetGameInstance() ? GetGameInstance()->GetSubsystem<UUIManagerSubsystem>() : nullptr)
		{
			UI->OnUIInput_Accept(this);
		}
	}
}

static void ApplySettingsFromSave(AFWSPlayerController& PC, const FPlayerSettings& S)
{
	// Examples: input mappings, sensitivity, FOV, audio mix, UI theme
	// PC.ClientSetCameraFieldOfView(S.FOV);
	// UMyInputMapper::Apply(PC, S.Input);
	// UMyThemeSubsystem::ApplyTheme(S.ThemeId);
}

void AFWSPlayerController::HandleProfileMetaUpdated(FPlayerProfileMeta /*MetaSnapshot*/)
{
	if (UUnifiedSubsystemManager* Unified = GetGameInstance() ? GetGameInstance()->GetSubsystem<UUnifiedSubsystemManager>() : nullptr)
	{
		// Reload the active slot (Save subsystem will broadcast SettingsLoaded/Applied)
		Unified->RequestLoad(/*bAsync*/ true);
	}
}

void AFWSPlayerController::HandleProfileKeyChanged(const FSaveProfileKey& Key) {}
void AFWSPlayerController::HandleSettingsLoaded(const FPlayerSettings& Settings) {}
void AFWSPlayerController::HandleSettingsApplied(const FPlayerSettings& Settings) {}

EWorldPhase AFWSPlayerController::DetectPhaseFromWorld() const
{
	if (const UWorld* W = GetWorld())
	{
		if (const AGameStateBase* GS = W->GetGameState())
		{
			if (GS->IsA(AFWSLobbyGameState::StaticClass())) return EWorldPhase::Lobby;
			if (GS->IsA(AFWSMatchGameState::StaticClass())) return EWorldPhase::Match;
		}
	}
	return EWorldPhase::MainMenu;
}

void AFWSPlayerController::ApplyPhaseInput(EWorldPhase Phase)
{
	switch (Phase)
	{
	case EWorldPhase::MainMenu:
		{
			FInputModeUIOnly M;
			M.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			SetInputMode(M);
			bShowMouseCursor = true;
			bEnableClickEvents = true;
			bEnableMouseOverEvents = true;
			break;
		}
	case EWorldPhase::Lobby:
		{
			FInputModeGameAndUI M;
			M.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			SetInputMode(M);
			bShowMouseCursor = true;
			bEnableClickEvents = true;
			bEnableMouseOverEvents = true;
			break;
		}
	case EWorldPhase::Match:
	default:
		{
			FInputModeGameOnly M;
			SetInputMode(M);
			bShowMouseCursor = false;
			bEnableClickEvents = false;
			bEnableMouseOverEvents = false;
			break;
		}
	}
}


void AFWSPlayerController::Server_SetActiveCharacterForSession_Implementation(const FString& CharacterId)
{
	if (!HasAuthority()) return;

	if (APlayerState* PS = PlayerState)
	{
		if (UCharacterProfileComponent* Comp = PS->FindComponentByClass<UCharacterProfileComponent>())
		{
			if (Comp->ServerLoadActiveCharacterFor(CharacterId))
			{
				// Optional: hydrate the pawn now if already spawned
				if (APawn* P = GetPawn())
				{
					if (UCharacterRPGComponent* RPG = P->FindComponentByClass<UCharacterRPGComponent>())
					{
						RPG->ApplyFromProfile(Comp->GetProfile());
					}
				}
			}
		}
	}
}