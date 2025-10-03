#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "FWSMatchPlayerState.generated.h"

class UCharacterProfileComponent;

UCLASS()
class FWSCORE_API AFWSMatchPlayerState : public APlayerState
{
	GENERATED_BODY()
public:
	AFWSMatchPlayerState();
	
	UPROPERTY(BlueprintReadOnly, Replicated)
	int32 Kills = 0;

	UPROPERTY(BlueprintReadOnly, Replicated)
	int32 Deaths = 0;

	virtual void GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	UCharacterProfileComponent* CharacterProfile;
};
