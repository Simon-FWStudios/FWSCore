#include "CharacterSelectWidget.h"
#include "FWSCore/Systems/PlayerProfile/PlayerProfileSubsystem.h"
#include "Components/ListView.h"
#include "Components/EditableTextBox.h"
#include "Components/Button.h"
#include "Kismet/GameplayStatics.h"
#include "FWSCore/Gameplay/FWSPlayerController.h"

void UCharacterSelectWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (Btn_Create) Btn_Create->OnClicked.AddDynamic(this, &UCharacterSelectWidget::OnCreateClicked);
	if (Btn_Select) Btn_Select->OnClicked.AddDynamic(this, &UCharacterSelectWidget::OnSelectClicked);
	if (Btn_Delete) Btn_Delete->OnClicked.AddDynamic(this, &UCharacterSelectWidget::OnDeleteClicked);

	if (auto* S = PPS())
	{
		// was: AddUniqueDynamic(... lambda ...)
		S->OnCharacterListChanged.AddUniqueDynamic(this, &UCharacterSelectWidget::HandleCharacterListChanged);
		S->RefreshCharacterList();
	}
}


void UCharacterSelectWidget::HandleCharacterListChanged(const TArray<FCharacterSummary>& In)
{
	Cached = In;
	RefreshList();
}


UPlayerProfileSubsystem* UCharacterSelectWidget::PPS() const
{
	if (UGameInstance* GI = GetGameInstance())
	{
		return GI->GetSubsystem<UPlayerProfileSubsystem>();
	}
	return nullptr;
}

void UCharacterSelectWidget::RefreshList()
{
	if (!List_Characters) return;

	List_Characters->ClearListItems();

	for (const FCharacterSummary& S : Cached)
	{
		UCharacterRow* Row = NewObject<UCharacterRow>(this);
		Row->Id = S.CharacterId;
		Row->Display = FString::Printf(TEXT("%s  (Lv%d)  %.0f HP"), *S.CharacterName, S.Level, S.MaxHealth);
		List_Characters->AddItem(Row); // ✅ UObject*
	}
}

FString UCharacterSelectWidget::GetSelectedCharacterId() const
{
	if (!List_Characters) return FString();

	if (UObject* Sel = List_Characters->GetSelectedItem())
	{
		if (const UCharacterRow* Row = Cast<UCharacterRow>(Sel))
		{
			return Row->Id;
		}
	}
	return FString();
}

void UCharacterSelectWidget::OnCreateClicked()
{
	const FString Name = Input_NewName ? Input_NewName->GetText().ToString() : TEXT("Adventurer");
	FCharacterProfile Out;
	if (auto* S = PPS(); S && S->CreateCharacter(Name, /*Archetype*/ NAME_None, Out))
	{
		S->SetActiveCharacterId(Out.CharacterId);
	}
}

void UCharacterSelectWidget::OnSelectClicked()
{
	const FString Id = GetSelectedCharacterId();
	if (Id.IsEmpty()) return;

	if (auto* S = PPS()) S->SetActiveCharacterId(Id);

	if (AFWSPlayerController* PC = GetOwningPlayer<AFWSPlayerController>())
	{
		PC->Server_SetActiveCharacterForSession(Id);
	}
}

void UCharacterSelectWidget::OnDeleteClicked()
{
	const FString Id = GetSelectedCharacterId();
	if (Id.IsEmpty()) return;
	if (auto* S = PPS()) { S->DeleteCharacter(Id); }
}
