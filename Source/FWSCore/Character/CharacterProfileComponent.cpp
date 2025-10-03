#include "CharacterProfileComponent.h"
#include "FWSCore/Systems/PlayerProfile/PlayerProfileSubsystem.h"
#include "Engine/World.h"
#include "TimerManager.h"

UCharacterProfileComponent::UCharacterProfileComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UCharacterProfileComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UCharacterProfileComponent, ReplicatedSummary);
}

UPlayerProfileSubsystem* UCharacterProfileComponent::PPS() const
{
	if (const UWorld* W = GetWorld())
	{
		if (UGameInstance* GI = W->GetGameInstance())
		{
			return GI->GetSubsystem<UPlayerProfileSubsystem>();
		}
	}
	return nullptr;
}

bool UCharacterProfileComponent::ServerLoadActiveCharacterFor(const FString& CharacterId)
{
	check(GetOwner() && GetOwner()->HasAuthority());
	FCharacterProfile P;
	if (auto* S = PPS(); S && S->LoadCharacter(CharacterId, P))
	{
		Profile = P;
		RebuildSummary();
		OnRep_Summary(); // for server-side UI debug
		return true;
	}
	return false;
}

void UCharacterProfileComponent::RebuildSummary()
{
	ReplicatedSummary.CharacterId   = Profile.CharacterId;
	ReplicatedSummary.CharacterName = Profile.CharacterName;
	ReplicatedSummary.Level         = Profile.Level;
	ReplicatedSummary.MaxHealth     = Profile.BaseMaxHealth + Profile.EarnedMaxHealth;
}

void UCharacterProfileComponent::OnRep_Summary()
{
	// Hook if UI wants to react on client; leave empty otherwise
}

void UCharacterProfileComponent::AddExperience(int32 Delta)
{
	if (!GetOwner()->HasAuthority()) return;
	Profile.Experience = FMath::Max(0, Profile.Experience + Delta);

	// Simple level-up curve placeholder: +100 XP per level
	while (Profile.Experience >= Profile.Level * 100)
	{
		Profile.Experience -= Profile.Level * 100;
		Profile.Level++;
		Profile.EarnedMaxHealth += 5.f; // tiny reward
	}
	RebuildSummary();
	MarkDirtyAndAutosave();
}

void UCharacterProfileComponent::AddCurrency(int32 Delta)
{
	if (!GetOwner()->HasAuthority()) return;
	Profile.Currency = FMath::Max(0, Profile.Currency + Delta);
	MarkDirtyAndAutosave();
}

void UCharacterProfileComponent::UnlockAbility(FName AbilityId)
{
	if (!GetOwner()->HasAuthority()) return;
	if (!Profile.AbilitiesUnlocked.Contains(AbilityId))
	{
		Profile.AbilitiesUnlocked.Add(AbilityId);
		MarkDirtyAndAutosave();
	}
}

void UCharacterProfileComponent::GrantItem(FName ItemId)
{
	if (!GetOwner()->HasAuthority()) return;
	if (!Profile.ItemsUnlocked.Contains(ItemId))
	{
		Profile.ItemsUnlocked.Add(ItemId);
		MarkDirtyAndAutosave();
	}
}

void UCharacterProfileComponent::SaveNow()
{
	if (auto* S = PPS())
	{
		S->SaveCharacter(Profile); // ignore bool; not needed for timer
	}
}

void UCharacterProfileComponent::MarkDirtyAndAutosave()
{
	if (UWorld* W = GetWorld())
	{
		if (AutosaveHandle.IsValid())
		{
			W->GetTimerManager().ClearTimer(AutosaveHandle);
		}
		W->GetTimerManager().SetTimer(
			AutosaveHandle,
			this, &UCharacterProfileComponent::SaveNow,
			1.0f, /*bLoop=*/false
		);
	}
}
