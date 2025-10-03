#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "FWSLobbyPlayerState.generated.h"

class UCharacterProfileComponent;

UCLASS()
class FWSCORE_API AFWSLobbyPlayerState : public APlayerState
{
	GENERATED_BODY()
public:
	AFWSLobbyPlayerState();
	
	UPROPERTY(BlueprintReadOnly, Replicated)
	bool bIsReady = false;

	UFUNCTION(BlueprintCallable, Category="Lobby")
	void SetReady(bool bReady);

	UFUNCTION(Server, Reliable)
	void ServerSetReady(bool bReady);

	virtual void GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	UCharacterProfileComponent* CharacterProfile;
};
