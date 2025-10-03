#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "SaveSystem.generated.h"

/** Per-object payload. */
USTRUCT(BlueprintType)
struct FSaveObjectData
{
	GENERATED_BODY()

	/** String map keeps things simple & BP-friendly; use helpers to set/get typed values. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TMap<FName, FString> SavedFields;

	/** Optional binary payload for compact C++ serialization if you need it later */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<uint8> BinaryPayload;
};

/** Per-player container of object payloads */
USTRUCT(BlueprintType)
struct FPlayerSaveData
{
	GENERATED_BODY()

	/** Legacy name-keyed data (backward compatible) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TMap<FName, FSaveObjectData> ObjectData;

	/** New: GUID-keyed data for stable identities */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TMap<FGuid, FSaveObjectData> GuidObjectData;
};

/** Root save-game object (per-player) */
UCLASS()
class FWSCORE_API USaveSystem : public USaveGame
{
	GENERATED_BODY()

public:
	/** Structured data saved for the player */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category="Save Data")
	FPlayerSaveData PlayerSave;

	/** Save metadata */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Save Metadata")
	int32 SaveVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Save Metadata")
	FDateTime SaveTimestamp;

	/** Optional: build/branch id or content hash for migrations */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Save Metadata")
	FString BuildId;

public:
	/** Entry points used by the subsystem */
	UFUNCTION(BlueprintCallable, Category = "Save System")
	virtual void SaveAllData(TArray<UObject*> SaveableObjects);

	UFUNCTION(BlueprintCallable, Category = "Save System")
	virtual void LoadAllData(TArray<UObject*> LoadableObjects);

	/** ---- Convenience helpers (C++ & BP) ---- */

	/** Name-keyed (legacy) */
	const FSaveObjectData* FindObject(FName ObjectId) const;
	FSaveObjectData& GetOrCreateObject(FName ObjectId);

	/** GUID-keyed (new) */
	const FSaveObjectData* FindObjectByGuid(const FGuid& Guid) const;
	FSaveObjectData& GetOrCreateObjectByGuid(const FGuid& Guid);

	/** Set/Get one field (BP-friendly, name-keyed) */
	UFUNCTION(BlueprintCallable, Category="Save System|Edit")
	void SetField(FName ObjectId, FName Key, const FString& Value);

	UFUNCTION(BlueprintCallable, Category="Save System|Edit")
	bool GetField(FName ObjectId, FName Key, FString& OutValue) const;

	/** Typed helpers (stored as strings) */
	void SetInt(FName ObjectId, FName Key, int32 Value);
	bool GetInt(FName ObjectId, FName Key, int32& Out) const;

	void SetFloat(FName ObjectId, FName Key, float Value);
	bool GetFloat(FName ObjectId, FName Key, float& Out) const;

	void SetBool(FName ObjectId, FName Key, bool bValue);
	bool GetBool(FName ObjectId, FName Key, bool& Out) const;
};
