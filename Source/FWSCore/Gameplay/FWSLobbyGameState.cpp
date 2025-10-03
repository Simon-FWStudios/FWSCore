#include "FWSLobbyGameState.h"
#include "Net/UnrealNetwork.h"
#include "FWSCore/Shared/FWSTypes.h"

AFWSLobbyGameState::AFWSLobbyGameState()
{
}

void AFWSLobbyGameState::GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFWSLobbyGameState, Lobby);
	DOREPLIFETIME(AFWSLobbyGameState, bCountdownActive);
	DOREPLIFETIME(AFWSLobbyGameState, CountdownSeconds);
}

void AFWSLobbyGameState::ServerSetLobbyFromSummary(const FUnifiedLobbySummary& S)
{
	if (!HasAuthority()) return;

	Lobby.LobbyId         = S.LobbyId;
	Lobby.Name            = S.Name;
	Lobby.Map             = S.Map;
	Lobby.Mode            = S.Mode;
	Lobby.MaxMembers      = S.MaxMembers;
	Lobby.MemberCount     = S.MemberCount;
	Lobby.bPresenceEnabled= S.bPresenceEnabled;
	Lobby.bAllowInvites   = S.bAllowInvites;
	// Replicated to clients
}

void AFWSLobbyGameState::ServerSetCountdown(bool bActive, int32 Seconds)
{
	if (!HasAuthority()) return;
	bCountdownActive = bActive;
	CountdownSeconds = Seconds;
}
