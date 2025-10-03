#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "FWSLobbyGameState.generated.h"

USTRUCT(BlueprintType)
struct FWSCORE_API FLobbyInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString LobbyId;
	UPROPERTY(BlueprintReadOnly) FString Name;
	UPROPERTY(BlueprintReadOnly) FString Map;
	UPROPERTY(BlueprintReadOnly) FString Mode;
	UPROPERTY(BlueprintReadOnly) int32   MaxMembers = 0;
	UPROPERTY(BlueprintReadOnly) int32   MemberCount = 0;
	UPROPERTY(BlueprintReadOnly) bool    bPresenceEnabled = false;
	UPROPERTY(BlueprintReadOnly) bool    bAllowInvites    = true;
};

UCLASS()
class FWSCORE_API AFWSLobbyGameState : public AGameStateBase
{
	GENERATED_BODY()
public:
	AFWSLobbyGameState();

	// Replicated snapshot for UI
	UPROPERTY(BlueprintReadOnly, Replicated)
	FLobbyInfo Lobby;

	// Optional: countdown (server-driven)
	UPROPERTY(BlueprintReadOnly, Replicated)
	bool bCountdownActive = false;

	UPROPERTY(BlueprintReadOnly, Replicated)
	int32 CountdownSeconds = 0;

	// Helpers to set from server
	void ServerSetLobbyFromSummary(const struct FUnifiedLobbySummary& S);
	void ServerSetCountdown(bool bActive, int32 Seconds);

	// Replication
	virtual void GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& Out) const override;
};
