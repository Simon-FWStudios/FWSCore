#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FWSMainMenuGameMode.generated.h"

UCLASS()
class FWSCORE_API AFWSMainMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	AFWSMainMenuGameMode();

	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, Category="Travel To Map")
	TSoftObjectPtr<UWorld> LobbyMap;
};
