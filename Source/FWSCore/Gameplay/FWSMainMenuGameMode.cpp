#include "FWSMainMenuGameMode.h"
#include "Engine/GameInstance.h"
#include "FWSCore/Shared/FWSWorldPhase.h"
#include "FWSCore/Systems/UI/UIManagerSubsystem.h"

AFWSMainMenuGameMode::AFWSMainMenuGameMode()
{
	bUseSeamlessTravel = true;
}

void AFWSMainMenuGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UUIManagerSubsystem* UI = GI->GetSubsystem<UUIManagerSubsystem>())
		{
			// Tell UI we're in menu and bring it up.
			UI->SetPhase(static_cast<uint8>(EWorldPhase::MainMenu));
			UI->ShowMainMenu();
			UI->SetStatus(TEXT("Main Menu"));
		}
	}
}
