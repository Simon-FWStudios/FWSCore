#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FWSMatchGameMode.generated.h"

UCLASS()
class FWSCORE_API AFWSMatchGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	AFWSMatchGameMode();

	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, Category="Travel To Map")
	TSoftObjectPtr<UWorld> MainMenuMap;
};
