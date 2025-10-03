#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "FWSMatchGameState.generated.h"

UCLASS()
class FWSCORE_API AFWSMatchGameState : public AGameStateBase
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadOnly, Replicated)
	int32 MatchElapsedSeconds = 0;

	virtual void GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const override;
};
