#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FWSCore/Shared/CharacterProfileTypes.h"
#include "CharacterRPGComponent.generated.h"

class UCharacterProfileComponent;

/** Applies profile-driven stats/abilities/loadout to a pawn. */
UCLASS(ClassGroup=(Game), meta=(BlueprintSpawnableComponent))
class FWSCORE_API UCharacterRPGComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UCharacterRPGComponent();

	/** Apply a profile snapshot to the owning pawn (call on spawn/possess on server). */
	UFUNCTION(BlueprintCallable, Category="RPG")
	void ApplyFromProfile(const FCharacterProfile& Profile);

	// If you later add ASC: hook to grant/remove abilities, set AttributeSet values, etc.

protected:
	virtual void BeginPlay() override;

private:
	void ApplyHealth(const FCharacterProfile& P);
	void ApplyAbilities(const FCharacterProfile& P);
	void ApplyLoadout(const FCharacterProfile& P);
};
