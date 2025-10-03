#include "FWSLobbyGameMode.h"
#include "Engine/GameInstance.h"
#include "FWSLobbyGameState.h"
#include "Engine/GameInstance.h"
#include "FWSCore/Shared/FWSWorldPhase.h"
#include "FWSCore/Systems/UI/UIManagerSubsystem.h"
#include "FWSCore/Systems/Unified/UnifiedSubsystemManager.h"

AFWSLobbyGameMode::AFWSLobbyGameMode()
{
	bUseSeamlessTravel = true;
}

void AFWSLobbyGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority() && IsRunningDedicatedServer())
	{
		if (UUnifiedSubsystemManager* Unified = GetGameInstance()->GetSubsystem<UUnifiedSubsystemManager>())
		{
			Unified->CreateLobby(); // or CreateLobbyWithParams(...)
		}
	}
	
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UUIManagerSubsystem* UI = GI->GetSubsystem<UUIManagerSubsystem>())
		{
			UI->SetPhase(static_cast<uint8>(EWorldPhase::Lobby));
			UI->SetStatus(TEXT("Lobby"));
		}

		// Server-side: mirror lobby summaries into replicated GameState
		if (HasAuthority())
		{
			if (UUnifiedSubsystemManager* Unified = GI->GetSubsystem<UUnifiedSubsystemManager>())
			{
				Unified->OnLobbySummariesUpdated_U.AddDynamic(
					this, &AFWSLobbyGameMode::HandleLobbySummariesUpdated
				);
			}
		}
	}
}

void AFWSLobbyGameMode::HandleLobbySummariesUpdated(const TArray<FUnifiedLobbySummary>& Lobbies)
{
	if (!HasAuthority()) return;
	AFWSLobbyGameState* LGS = GetGameState<AFWSLobbyGameState>();
	if (!LGS) return;

	if (Lobbies.Num() > 0)
	{
		LGS->ServerSetLobbyFromSummary(Lobbies[0]);
	}
}