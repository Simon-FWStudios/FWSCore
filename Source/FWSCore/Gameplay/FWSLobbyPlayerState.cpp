#include "FWSLobbyPlayerState.h"

#include "FWSCore/Character/CharacterProfileComponent.h"
#include "Net/UnrealNetwork.h"

AFWSLobbyPlayerState::AFWSLobbyPlayerState()
{
	bIsReady = false;
	CharacterProfile = CreateDefaultSubobject<UCharacterProfileComponent>(TEXT("CharacterProfile"));
}

void AFWSLobbyPlayerState::GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFWSLobbyPlayerState, bIsReady);
}

void AFWSLobbyPlayerState::SetReady(bool bReady)
{
	if (HasAuthority())
	{
		bIsReady = bReady;
	}
	else
	{
		ServerSetReady(bReady);
	}
}

void AFWSLobbyPlayerState::ServerSetReady_Implementation(bool bReady)
{
	bIsReady = bReady;
}
