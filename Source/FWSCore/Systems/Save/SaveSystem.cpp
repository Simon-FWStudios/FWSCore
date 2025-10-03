#include "SaveSystem.h"
#include "FWSCore.h"
#include "Saveable.h"
#include "SaveIdComponent.h"
#include "GameFramework/Actor.h"
#include "Misc/DefaultValueHelper.h"

void USaveSystem::SaveAllData(TArray<UObject*> SaveableObjects)
{
	// Stamp time; version should be set by subsystem
	SaveTimestamp = FDateTime::Now();

	for (UObject* Obj : SaveableObjects)
	{
		if (!IsValid(Obj)) continue;

		if (ISaveable* Saveable = Cast<ISaveable>(Obj))
		{
			ISaveable::Execute_SaveData(Obj, this);
			UE_LOG(LogSaveSystem, Verbose, TEXT("[SaveSystem] Saved: %s"), *Obj->GetName());
		}
		else
		{
			UE_LOG(LogSaveSystem, Warning, TEXT("[SaveSystem] %s does not implement ISaveable."), *Obj->GetName());
		}
	}
}

void USaveSystem::LoadAllData(TArray<UObject*> LoadableObjects)
{
	for (UObject* Obj : LoadableObjects)
	{
		if (!IsValid(Obj)) continue;

		if (ISaveable* Saveable = Cast<ISaveable>(Obj))
		{
			const FSaveObjectData* Found = nullptr;

			// Prefer GUID lookup if it's an Actor with a SaveIdComponent
			if (const AActor* AsActor = Cast<AActor>(Obj))
			{
				if (const USaveIdComponent* Idc = AsActor->FindComponentByClass<USaveIdComponent>())
				{
					if (Idc->HasGuid())
					{
						Found = FindObjectByGuid(Idc->SaveGuid);
					}
				}
			}

			// Fallback to legacy name-keyed lookup (your previous behavior)
			if (!Found)
			{
				const FName DefaultId = Obj->GetFName();
				Found = FindObject(DefaultId);
			}

			FSaveObjectData Empty;
			const FSaveObjectData& Payload = Found ? *Found : Empty;
			 ISaveable::Execute_LoadData(Obj, this, Payload);

			UE_LOG(LogSaveSystem, Verbose, TEXT("[SaveSystem] Loaded: %s (HasData=%s)"),
				*Obj->GetName(), Found ? TEXT("true") : TEXT("false"));
		}
		else
		{
			UE_LOG(LogSaveSystem, Warning, TEXT("[SaveSystem] %s does not implement ISaveable."), *Obj->GetName());
		}
	}
}

/* ---------- Helpers ---------- */

const FSaveObjectData* USaveSystem::FindObject(FName ObjectId) const
{
	if (const FSaveObjectData* Ptr = PlayerSave.ObjectData.Find(ObjectId))
	{
		return Ptr;
	}
	return nullptr;
}

FSaveObjectData& USaveSystem::GetOrCreateObject(FName ObjectId)
{
	return PlayerSave.ObjectData.FindOrAdd(ObjectId);
}

const FSaveObjectData* USaveSystem::FindObjectByGuid(const FGuid& Guid) const
{
	if (const FSaveObjectData* Ptr = PlayerSave.GuidObjectData.Find(Guid))
	{
		return Ptr;
	}
	return nullptr;
}

FSaveObjectData& USaveSystem::GetOrCreateObjectByGuid(const FGuid& Guid)
{
	return PlayerSave.GuidObjectData.FindOrAdd(Guid);
}

void USaveSystem::SetField(FName ObjectId, FName Key, const FString& Value)
{
	GetOrCreateObject(ObjectId).SavedFields.Add(Key, Value);
}

bool USaveSystem::GetField(FName ObjectId, FName Key, FString& OutValue) const
{
	if (const FSaveObjectData* Obj = FindObject(ObjectId))
	{
		if (const FString* Found = Obj->SavedFields.Find(Key))
		{
			OutValue = *Found;
			return true;
		}
	}
	return false;
}

void USaveSystem::SetInt(FName ObjectId, FName Key, int32 Value)
{
	SetField(ObjectId, Key, LexToString(Value));
}

bool USaveSystem::GetInt(FName ObjectId, FName Key, int32& Out) const
{
	FString S;
	if (GetField(ObjectId, Key, S))
	{
		return FDefaultValueHelper::ParseInt(S, Out);
	}
	return false;
}

void USaveSystem::SetFloat(FName ObjectId, FName Key, float Value)
{
	SetField(ObjectId, Key, LexToString(Value));
}

bool USaveSystem::GetFloat(FName ObjectId, FName Key, float& Out) const
{
	FString S;
	if (GetField(ObjectId, Key, S))
	{
		return FDefaultValueHelper::ParseFloat(S, Out);
	}
	return false;
}

void USaveSystem::SetBool(FName ObjectId, FName Key, bool bValue)
{
	SetField(ObjectId, Key, bValue ? TEXT("1") : TEXT("0"));
}

bool USaveSystem::GetBool(FName ObjectId, FName Key, bool& Out) const
{
	FString S;
	if (GetField(ObjectId, Key, S))
	{
		Out = (S == TEXT("1") || S.Equals(TEXT("true"), ESearchCase::IgnoreCase));
		return true;
	}
	return false;
}
