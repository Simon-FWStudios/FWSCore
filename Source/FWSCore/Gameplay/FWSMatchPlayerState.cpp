#include "FWSMatchPlayerState.h"

#include "FWSCore/Character/CharacterProfileComponent.h"
#include "Net/UnrealNetwork.h"

AFWSMatchPlayerState::AFWSMatchPlayerState()
{
	CharacterProfile = CreateDefaultSubobject<UCharacterProfileComponent>(TEXT("CharacterProfile"));
}

void AFWSMatchPlayerState::GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFWSMatchPlayerState, Kills);
	DOREPLIFETIME(AFWSMatchPlayerState, Deaths);
}
