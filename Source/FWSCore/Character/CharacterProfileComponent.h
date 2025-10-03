#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/UnrealNetwork.h"
#include "FWSCore/Shared/CharacterProfileTypes.h"
#include "CharacterProfileComponent.generated.h"

class UPlayerProfileSubsystem;

/** Server-only authority for the active character during a session. */
UCLASS(ClassGroup=(Game), meta=(BlueprintSpawnableComponent))
class FWSCORE_API UCharacterProfileComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UCharacterProfileComponent();

	// Entry points (server)
	UFUNCTION(BlueprintCallable, Category="Character|Server")
	bool ServerLoadActiveCharacterFor(const FString& CharacterId);

	UFUNCTION(BlueprintCallable, Category="Character|Server")
	void AddExperience(int32 Delta);

	UFUNCTION(BlueprintCallable, Category="Character|Server")
	void AddCurrency(int32 Delta);

	UFUNCTION(BlueprintCallable, Category="Character|Server")
	void UnlockAbility(FName AbilityId);

	UFUNCTION(BlueprintCallable, Category="Character|Server")
	void GrantItem(FName ItemId);

	UFUNCTION(BlueprintCallable, Category="Character|Server")
	void SaveNow();

	// Accessors
	UFUNCTION(BlueprintPure, Category="Character") const FCharacterProfile& GetProfile() const { return Profile; }
	UFUNCTION(BlueprintPure, Category="Character") FCharacterSummary GetRepSummary() const { return ReplicatedSummary; }


protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;

private:
	UPROPERTY(Transient) FCharacterProfile Profile;

	// Lightweight summary replicated to owning client (and optionally to others)
	UPROPERTY(ReplicatedUsing=OnRep_Summary) FCharacterSummary ReplicatedSummary;

	UFUNCTION() void OnRep_Summary();

	void RebuildSummary();
	UPlayerProfileSubsystem* PPS() const;
	void MarkDirtyAndAutosave();
	FTimerHandle AutosaveHandle;
};
