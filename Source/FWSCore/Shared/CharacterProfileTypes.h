#pragma once
#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "CharacterProfileTypes.generated.h"

/** What you want to persist per character. Keep it versioned. */
USTRUCT(BlueprintType)
struct FCharacterProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Version = 1;

	// Identity
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString CharacterId;     // GUID-like stable id
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString CharacterName;   // user-facing name
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName   ArchetypeId;     // loadout template row (optional)

	// Progression
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Level = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Experience = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Currency = 0;

	// Stats
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float BaseMaxHealth = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float EarnedMaxHealth = 0.f;

	// Simple loadout/ability placeholders
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FName> AbilitiesUnlocked; // e.g. tags/soft row names
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FName> ItemsUnlocked;

	// Bookkeeping
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int64 SecondsPlayed = 0; // for a future session timer
};

/** Small replicated summary we’ll put on PlayerState. */
USTRUCT(BlueprintType)
struct FCharacterSummary
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString CharacterId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString CharacterName;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32   Level = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float   MaxHealth = 100.f;
};
