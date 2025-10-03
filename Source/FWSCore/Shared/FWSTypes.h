#pragma once
#include "CoreMinimal.h"
#include "FWSTypes.generated.h"

/** SDK-agnostic lobby summary for game/BP surfaces */
USTRUCT(BlueprintType)
struct FUnifiedLobbySummary
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString LobbyId;
	UPROPERTY(BlueprintReadOnly) FString Name;
	UPROPERTY(BlueprintReadOnly) FString Map;
	UPROPERTY(BlueprintReadOnly) FString Mode;

	UPROPERTY(BlueprintReadOnly) int32   MaxMembers    = 0;
	UPROPERTY(BlueprintReadOnly) int32   MemberCount  = 0;

	UPROPERTY(BlueprintReadOnly) bool    bPresenceEnabled = false;
	UPROPERTY(BlueprintReadOnly) bool    bAllowInvites    = true;
};

/**
 * Basic profile meta we may want to surface and persist alongside the slot.
 * Expand as you need (region, language, controller layout, etc.)
 */
USTRUCT(BlueprintType)
struct FPlayerProfileMeta
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Profile")
	TMap<FName, FString> KV;

	// Convenience accessors
	FString Get(const FName& Key, const FString& Default = TEXT("")) const
	{
		if (const FString* V = KV.Find(Key)) return *V;
		return Default;
	}
	void Set(const FName& Key, const FString& Value) { KV.FindOrAdd(Key) = Value; }
	bool IsEmpty() const { return KV.Num() == 0; }
};

/**
 * Versioned, blueprintable player settings persisted by the SaveFramework.
 * Keep to user preferences, not volatile runtime stats.
 */
USTRUCT(BlueprintType)
struct FPlayerSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings")
	int32 Version = 1;

	// ---- Audio ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings|Audio", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MasterVolume = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings|Audio", meta=(ClampMin="0.0", ClampMax="1.0"))
	float SFXVolume = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings|Audio", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MusicVolume = 0.6f;

	// ---- Video/Display ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings|Video", meta=(ClampMin="60.0", ClampMax="120.0"))
	float FieldOfView = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings|Video")
	bool bVSync = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings|Video", meta=(ClampMin="0", ClampMax="3"))
	int32 QualityPreset = 2; // 0=Low,1=Med,2=High,3=Epic

	// ---- Input ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings|Input")
	bool bInvertY = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings|Input", meta=(ClampMin="0.1", ClampMax="10.0"))
	float MouseSensitivity = 1.0f;

	// ---- UI / Accessibility ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings|UI")
	FName ThemeId = TEXT("Default");

	// ---- Identity / Cosmetic ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings|Identity")
	FString PreferredDisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings|Identity")
	FString ChosenAvatarId; // e.g., data-asset row name

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings|Identity")
	TSoftObjectPtr<UTexture2D> AvatarThumbnail;
};

/**
 * The unique key for a player's profile slot/name derived from identity.
 */
USTRUCT(BlueprintType)
struct FSaveProfileKey
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Save")
	FName Platform = TEXT("Local"); // "EOS", "Steam", "Local" etc.

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Save")
	FString UserId;                 // e.g., EpicAccountId string

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Save")
	int32 LocalUserNum = 0;         // splitscreen/fallback

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Save")
	FName Namespace = NAME_None;    // deployment/sandbox or "Default"
	
	FString ToSlotName() const
	{
		const FString NS = Namespace.IsNone() ? TEXT("Default") : Namespace.ToString();
		// Format chosen to be stable & readable
		return FString::Printf(TEXT("Profile_%s_%s_%d_%s"),
			*Platform.ToString(), *NS, LocalUserNum, *UserId);
	}
	
	bool IsValid() const
	{
		return !UserId.IsEmpty() || Platform == TEXT("Local");
	}

	static FSaveProfileKey LocalFallback(int32 LocalIdx = 0)
	{
		FSaveProfileKey K;
		K.Platform     = TEXT("Local");
		K.UserId       = FString::Printf(TEXT("LocalUser%d"), LocalIdx);
		K.LocalUserNum = LocalIdx;
		return K;
	}
};

/**
 * Optional slot info structure you can reuse in UI lists.
 */
USTRUCT(BlueprintType)
struct FProfileSlotInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString SlotName;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Platform;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Namespace;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32   LocalUserNum = 0;
};
