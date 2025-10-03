#include "PlayerProfileSubsystem.h"
#include "FWSCore/Systems/Save/SaveSystemSubsystem.h"
#include "FWSCore/Player/PlayerProfileComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"

USaveSystemSubsystem* UPlayerProfileSubsystem::GetSave() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<USaveSystemSubsystem>() : nullptr;
}

bool UPlayerProfileSubsystem::ReadJson(const FName ObjectId, const FName Key, FString& OutJson) const
{
	if (auto* S = GetSave())
	{
		return S->GetCurrentSaveSystem()->GetField(ObjectId, Key, OutJson);
	}
	return false;
}
bool UPlayerProfileSubsystem::WriteJson(const FName ObjectId, const FName Key, const FString& Json, bool bSaveNow) const
{
	if (auto* S = GetSave())
	{
		S->GetCurrentSaveSystem()->SetField(ObjectId, Key, Json);
		if (bSaveNow) S->RequestSave(true);
		return true;
	}
	return false;
}

// Minimal JSON (UE structs → JSON string)
bool UPlayerProfileSubsystem::ToJson(const FCharacterProfile& In, FString& Out) const
{
	// Simple manual writer (tiny schema)
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("Id"), In.CharacterId);
	Root->SetStringField(TEXT("Name"), In.CharacterName);
	Root->SetStringField(TEXT("Archetype"), In.ArchetypeId.ToString());
	Root->SetNumberField(TEXT("Version"), In.Version);
	Root->SetNumberField(TEXT("Level"), In.Level);
	Root->SetNumberField(TEXT("XP"), In.Experience);
	Root->SetNumberField(TEXT("Currency"), In.Currency);
	Root->SetNumberField(TEXT("BaseMaxHealth"), In.BaseMaxHealth);
	Root->SetNumberField(TEXT("EarnedMaxHealth"), In.EarnedMaxHealth);

	TArray<TSharedPtr<FJsonValue>> Ab;
	for (const FName& N : In.AbilitiesUnlocked) Ab.Add(MakeShared<FJsonValueString>(N.ToString()));
	Root->SetArrayField(TEXT("Abilities"), Ab);

	TArray<TSharedPtr<FJsonValue>> It;
	for (const FName& N : In.ItemsUnlocked) It.Add(MakeShared<FJsonValueString>(N.ToString()));
	Root->SetArrayField(TEXT("Items"), It);

	Root->SetNumberField(TEXT("SecondsPlayed"), (double)In.SecondsPlayed);

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	return FJsonSerializer::Serialize(Root, W);
}
bool UPlayerProfileSubsystem::FromJson(const FString& In, FCharacterProfile& Out) const
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(In);
	if (!FJsonSerializer::Deserialize(R, Root) || !Root.IsValid()) return false;

	Out.CharacterId   = Root->GetStringField(TEXT("Id"));
	Out.CharacterName = Root->GetStringField(TEXT("Name"));
	Out.ArchetypeId   = FName(*Root->GetStringField(TEXT("Archetype")));
	Out.Version       = Root->GetIntegerField(TEXT("Version"));
	Out.Level         = Root->GetIntegerField(TEXT("Level"));
	Out.Experience    = Root->GetIntegerField(TEXT("XP"));
	Out.Currency      = Root->GetIntegerField(TEXT("Currency"));
	Out.BaseMaxHealth   = Root->GetNumberField(TEXT("BaseMaxHealth"));
	Out.EarnedMaxHealth = Root->GetNumberField(TEXT("EarnedMaxHealth"));

	Out.AbilitiesUnlocked.Reset();
	if (const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr; Root->TryGetArrayField(TEXT("Abilities"), Arr))
	{
		for (const auto& V : *Arr) Out.AbilitiesUnlocked.Add(FName(*V->AsString()));
	}
	Out.ItemsUnlocked.Reset();
	if (const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr; Root->TryGetArrayField(TEXT("Items"), Arr))
	{
		for (const auto& V : *Arr) Out.ItemsUnlocked.Add(FName(*V->AsString()));
	}
	Out.SecondsPlayed = (int64)Root->GetNumberField(TEXT("SecondsPlayed"));
	return true;
}

void UPlayerProfileSubsystem::RefreshCharacterList()
{
	CachedSummaries.Reset();

	// This pattern enumerates by a separate index list stored under "Characters/Index".
	// For simplicity we store a CSV of ids under ObjectId="Characters", Key="Index".
	FString Csv;
	if (ReadJson(TEXT("Characters"), TEXT("Index"), Csv))
	{
		TArray<FString> Ids;
		Csv.ParseIntoArray(Ids, TEXT(","), /*CullEmpty*/true);
		for (const FString& Id : Ids)
		{
			FCharacterProfile P;
			FString Json;
			if (ReadJson(*ObjIdForCharacter(Id), TEXT("Data"), Json) && FromJson(Json, P))
			{
				FCharacterSummary S;
				S.CharacterId   = P.CharacterId;
				S.CharacterName = P.CharacterName;
				S.Level         = P.Level;
				S.MaxHealth     = P.BaseMaxHealth + P.EarnedMaxHealth;
				CachedSummaries.Add(MoveTemp(S));
			}
		}
	}

	OnCharacterListChanged.Broadcast(CachedSummaries);
	EnsureActiveConsistency();
}

bool UPlayerProfileSubsystem::CreateCharacter(const FString& Name, const FName ArchetypeId, FCharacterProfile& OutProfile)
{
	// Generate a stable id
	const FString Id = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensInBraces);
	OutProfile = FCharacterProfile{};
	OutProfile.CharacterId   = Id;
	OutProfile.CharacterName = Name;
	OutProfile.ArchetypeId   = ArchetypeId;
	// Defaults (version/level/health already set)

	// Persist
	FString Json;
	if (!ToJson(OutProfile, Json)) return false;
	if (!WriteJson(*ObjIdForCharacter(Id), TEXT("Data"), Json, false)) return false;

	// Update index
	FString Csv;
	ReadJson(TEXT("Characters"), TEXT("Index"), Csv);
	if (!Csv.IsEmpty()) Csv += TEXT(",");
	Csv += Id;
	WriteJson(TEXT("Characters"), TEXT("Index"), Csv, true);

	RefreshCharacterList();
	return true;
}

bool UPlayerProfileSubsystem::DeleteCharacter(const FString& CharacterId)
{
	// Removing a field: there’s no explicit delete in SaveSystem facade yet,
	// so we overwrite the entry with "{}" and remove from the index string.
	FString Csv;
	ReadJson(TEXT("Characters"), TEXT("Index"), Csv);

	TArray<FString> Ids;
	Csv.ParseIntoArray(Ids, TEXT(","), true);
	const int32 Removed = Ids.Remove(CharacterId);

	if (Removed > 0)
	{
		FString NewCsv = FString::Join(Ids, TEXT(","));
		WriteJson(TEXT("Characters"), TEXT("Index"), NewCsv, false);
		WriteJson(*ObjIdForCharacter(CharacterId), TEXT("Data"), TEXT("{}"), false);

		if (ActiveCharacterId == CharacterId) ActiveCharacterId.Empty();
		GetSave()->RequestSave(true);
		RefreshCharacterList();
		return true;
	}
	return false;
}

bool UPlayerProfileSubsystem::LoadCharacter(const FString& CharacterId, FCharacterProfile& OutProfile)
{
	FString Json;
	if (!ReadJson(*ObjIdForCharacter(CharacterId), TEXT("Data"), Json)) return false;
	return FromJson(Json, OutProfile);
}

bool UPlayerProfileSubsystem::SaveCharacter(const FCharacterProfile& Profile)
{
	FString Json;
	if (!ToJson(Profile, Json)) return false;
	const bool Ok = WriteJson(*ObjIdForCharacter(Profile.CharacterId), TEXT("Data"), Json, true);
	if (Ok && Profile.CharacterId == ActiveCharacterId)
	{
		// Reflect to UI if we just saved the active one
		OnActiveCharacterChanged.Broadcast(Profile);
	}
	return Ok;
}

void UPlayerProfileSubsystem::SetActiveCharacterId(const FString& CharacterId)
{
	ActiveCharacterId = CharacterId;
	// Optionally store under "Profile" object so it travels with the slot
	WriteJson(TEXT("Profile"), TEXT("ActiveCharacterId"), CharacterId, true);

	// Broadcast a snapshot if we can load it
	FCharacterProfile P;
	if (LoadCharacter(CharacterId, P))
	{
		OnActiveCharacterChanged.Broadcast(P);
	}
	EnsureActiveConsistency();
}

void UPlayerProfileSubsystem::EnsureActiveConsistency()
{
	if (ActiveCharacterId.IsEmpty())
	{
		// Try restore from save
		FString Saved;
		if (ReadJson(TEXT("Profile"), TEXT("ActiveCharacterId"), Saved) && !Saved.IsEmpty())
		{
			ActiveCharacterId = Saved;
		}
	}

	// If still empty, pick first (if any)
	if (ActiveCharacterId.IsEmpty() && CachedSummaries.Num() > 0)
	{
		ActiveCharacterId = CachedSummaries[0].CharacterId;
	}
}
