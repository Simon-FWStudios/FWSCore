#include "FWSMatchGameState.h"
#include "Net/UnrealNetwork.h"

void AFWSMatchGameState::GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFWSMatchGameState, MatchElapsedSeconds);
}
