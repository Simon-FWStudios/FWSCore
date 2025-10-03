#pragma once
#include "FWSWorldPhase.generated.h"

UENUM(BlueprintType)
enum class EWorldPhase : uint8
{
	MainMenu UMETA(DisplayName="Main Menu"),
	Lobby    UMETA(DisplayName="Lobby"),
	Match    UMETA(DisplayName="Match")
};