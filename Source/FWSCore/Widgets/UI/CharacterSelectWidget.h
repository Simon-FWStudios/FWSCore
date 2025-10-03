#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FWSCore/Shared/CharacterProfileTypes.h"
#include "CharacterSelectWidget.generated.h"

class UPlayerProfileSubsystem;
class UEditableTextBox;
class UButton;
class UListView;

UCLASS(BlueprintType)
class FWSCORE_API UCharacterRow : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadOnly) FString Id;
	UPROPERTY(BlueprintReadOnly) FString Display;
};

UCLASS()
class FWSCORE_API UCharacterSelectWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	virtual void NativeConstruct() override;

protected:
	UPROPERTY(meta=(BindWidgetOptional)) UListView* List_Characters = nullptr;
	UPROPERTY(meta=(BindWidgetOptional)) UEditableTextBox* Input_NewName = nullptr;
	UPROPERTY(meta=(BindWidgetOptional)) UButton* Btn_Create = nullptr;
	UPROPERTY(meta=(BindWidgetOptional)) UButton* Btn_Select = nullptr;
	UPROPERTY(meta=(BindWidgetOptional)) UButton* Btn_Delete = nullptr;

	UFUNCTION() void OnCreateClicked();
	UFUNCTION() void OnSelectClicked();
	UFUNCTION() void OnDeleteClicked();
	UFUNCTION()	void HandleCharacterListChanged(const TArray<FCharacterSummary>& In);

	void RefreshList();

private:
	TArray<FCharacterSummary> Cached;
	UPlayerProfileSubsystem* PPS() const;
	FString GetSelectedCharacterId() const;
};
