#include "FWSMatchGameMode.h"
#include "Engine/GameInstance.h"
#include "FWSCore/Shared/FWSWorldPhase.h"
#include "FWSCore/Systems/UI/UIManagerSubsystem.h"

AFWSMatchGameMode::AFWSMatchGameMode()
{
	bUseSeamlessTravel = true;
}

void AFWSMatchGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UUIManagerSubsystem* UI = GI->GetSubsystem<UUIManagerSubsystem>())
		{
			UI->SetPhase(static_cast<uint8>(EWorldPhase::Match));
			UI->SetStatus(TEXT("Match"));
			// Usually hide main menu here; keep pause/settings available via input
		}
	}
}
