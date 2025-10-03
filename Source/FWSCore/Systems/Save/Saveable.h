#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Saveable.generated.h"

/**
 * Implement on any UObject that wants to participate in the SaveSystem.
 * Preferred: for Actors, store/load via GUID with USaveIdComponent.
 */
UINTERFACE()
class USaveable : public UInterface
{
	GENERATED_BODY()
};

class FWSCORE_API ISaveable
{
	GENERATED_BODY()
public:
	/** Called by the save system to let the object write its data. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Save")	void SaveData(USaveSystem* SaveSystem);

	/** Called by the save system to apply previously saved data (const-ref to avoid copies). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Load")	void LoadData(USaveSystem* SaveSystem, const FSaveObjectData& Value);
};
