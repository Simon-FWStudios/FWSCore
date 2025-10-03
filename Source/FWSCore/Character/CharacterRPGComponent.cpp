#include "CharacterRPGComponent.h"
#include "GameFramework/Character.h"

UCharacterRPGComponent::UCharacterRPGComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicated(false); // purely local application; state should be owned by PS/server elsewhere
}

void UCharacterRPGComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UCharacterRPGComponent::ApplyFromProfile(const FCharacterProfile& Profile)
{
	ApplyHealth(Profile);
	ApplyAbilities(Profile);
	ApplyLoadout(Profile);
}

void UCharacterRPGComponent::ApplyHealth(const FCharacterProfile& P)
{
	if (ACharacter* C = Cast<ACharacter>(GetOwner()))
	{
		const float MaxHP = P.BaseMaxHealth + P.EarnedMaxHealth;
		// If you don’t have an AttributeSet yet, drive a simple variable or a health component
		// Example: if you later add UHealthComponent, call SetMaxHealth(MaxHP) here.
		C->CustomTimeDilation = 1.f; // placeholder no-op to avoid unused warnings
	}
}

void UCharacterRPGComponent::ApplyAbilities(const FCharacterProfile& P)
{
	// Placeholder: when you add ASC, iterate P.AbilitiesUnlocked and grant them.
}

void UCharacterRPGComponent::ApplyLoadout(const FCharacterProfile& P)
{
	// Placeholder: equip starter/earned items from P.ItemsUnlocked or Archetype.
}
