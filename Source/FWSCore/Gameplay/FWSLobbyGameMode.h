#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FWSLobbyGameMode.generated.h"

UCLASS()
class FWSCORE_API AFWSLobbyGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	AFWSLobbyGameMode();

	virtual void BeginPlay() override;

	UFUNCTION()
	void HandleLobbySummariesUpdated(const TArray<FUnifiedLobbySummary>& Lobbies);
	
};
