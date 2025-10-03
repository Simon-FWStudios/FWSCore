#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "FWSCore/Shared/CharacterProfileTypes.h"
#include "PlayerProfileSubsystem.generated.h"

class USaveSystemSubsystem;
class UPlayerProfileComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCharacterListChanged, const TArray<FCharacterSummary>&, Summaries);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActiveCharacterChanged, const FCharacterProfile&, Active);

/**
 * Account + Character Profile manager (uses the SaveSystem slot that PlayerProfileComponent mounted).
 * Exists on both client and server processes.
 */
UCLASS()
class FWSCORE_API UPlayerProfileSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	// ---- API ----
	UFUNCTION(BlueprintCallable, Category="Profiles")
	void RefreshCharacterList(); // loads summaries from slot

	UFUNCTION(BlueprintCallable, Category="Profiles")
	bool CreateCharacter(const FString& Name, const FName ArchetypeId, FCharacterProfile& OutProfile);

	UFUNCTION(BlueprintCallable, Category="Profiles")
	bool DeleteCharacter(const FString& CharacterId);

	UFUNCTION(BlueprintCallable, Category="Profiles")
	bool LoadCharacter(const FString& CharacterId, FCharacterProfile& OutProfile);

	UFUNCTION(BlueprintCallable, Category="Profiles")
	bool SaveCharacter(const FCharacterProfile& Profile);

	UFUNCTION(BlueprintCallable, Category="Profiles")
	void SetActiveCharacterId(const FString& CharacterId);

	UFUNCTION(BlueprintPure, Category="Profiles")
	const FString& GetActiveCharacterId() const { return ActiveCharacterId; }

	UFUNCTION(BlueprintPure, Category="Profiles")
	const TArray<FCharacterSummary>& GetCachedSummaries() const { return CachedSummaries; }

	// Events for UI
	UPROPERTY(BlueprintAssignable) FOnCharacterListChanged OnCharacterListChanged;
	UPROPERTY(BlueprintAssignable) FOnActiveCharacterChanged OnActiveCharacterChanged;

	// Convenience: get the SaveSystem in use
	USaveSystemSubsystem* GetSave() const;

private:
	FString ActiveCharacterId;
	UPROPERTY(Transient) TArray<FCharacterSummary> CachedSummaries;

	// Helpers
	static FString ObjIdForCharacter(const FString& Id) { return FString::Printf(TEXT("Characters/%s"), *Id); }
	bool ReadJson(const FName ObjectId, const FName Key, FString& OutJson) const;
	bool WriteJson(const FName ObjectId, const FName Key, const FString& Json, bool bSaveNow) const;
	bool ToJson(const FCharacterProfile& In, FString& Out) const;
	bool FromJson(const FString& In, FCharacterProfile& Out) const;
	void EnsureActiveConsistency();
};
