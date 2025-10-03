#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SaveIdComponent.generated.h"

/**
 * Attach to any actor that needs stable save identity.
 * Generates and persists a GUID once; can be queried at runtime.
 */
UCLASS(ClassGroup=(Save), Blueprintable, meta=(BlueprintSpawnableComponent))
class FWSCORE_API USaveIdComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USaveIdComponent();

	/** Stable GUID used as the primary key in saves */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Save")
	FGuid SaveGuid;

	/** Returns true if GUID was generated/loaded */
	UFUNCTION(BlueprintCallable, Category="Save")
	bool HasGuid() const { return SaveGuid.IsValid(); }

	/** Ensures there is a GUID; generates one if missing and returns it */
	UFUNCTION(BlueprintCallable, Category="Save")
	FGuid GetOrCreateGuid();

protected:
	virtual void OnRegister() override;
};
