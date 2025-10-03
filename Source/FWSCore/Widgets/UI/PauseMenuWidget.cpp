// PauseMenuWidget.cpp

#include "PauseMenuWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

TSharedRef<SWidget> UPauseMenuWidget::RebuildWidget()
{
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"));
	}
	if (!WidgetTree->RootWidget)
	{
		BuildIfMissing();
	}
	return Super::RebuildWidget();
}

void UPauseMenuWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	BuildIfMissing();
}

void UPauseMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildIfMissing();
}

void UPauseMenuWidget::BuildIfMissing()
{
	UCanvasPanel* Canvas = Cast<UCanvasPanel>(GetRootWidget());
	if (!Canvas)
	{
		Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
		WidgetTree->RootWidget = Canvas;
	}

	if (!AutoRoot)
	{
		AutoRoot = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("AutoRoot"));
		if (UCanvasPanelSlot* SSlot = Canvas->AddChildToCanvas(AutoRoot))
		{
			SSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
			SSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			SSlot->SetSize(FVector2D(420.f, 360.f));
		}
	}

	if (!Btn_Resume || !Btn_Settings || !Btn_Leave || !Btn_Quit)
	{
		AutoRoot->ClearChildren();

		Btn_Resume   = MakeButton(TEXT("Resume"),   GET_FUNCTION_NAME_CHECKED(UPauseMenuWidget, OnResumeClicked));
		Btn_Settings = MakeButton(TEXT("Settings"), GET_FUNCTION_NAME_CHECKED(UPauseMenuWidget, OnSettingsClicked));
		Btn_Leave    = MakeButton(TEXT("Leave Match"), GET_FUNCTION_NAME_CHECKED(UPauseMenuWidget, OnLeaveClicked));
		Btn_Quit     = MakeButton(TEXT("Quit to Desktop"), GET_FUNCTION_NAME_CHECKED(UPauseMenuWidget, OnQuitClicked));

		AutoRoot->AddChildToVerticalBox(Btn_Resume);
		AutoRoot->AddChildToVerticalBox(Btn_Settings);
		AutoRoot->AddChildToVerticalBox(Btn_Leave);
		AutoRoot->AddChildToVerticalBox(Btn_Quit);
	}
}

UButton* UPauseMenuWidget::MakeButton(const FString& Label, FName OnClickedUFunction) const
{
	UButton* B = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	T->SetText(FText::FromString(Label));
	B->AddChild(T);

	if (!OnClickedUFunction.IsNone())
	{
		FScriptDelegate Del; Del.BindUFunction(const_cast<UPauseMenuWidget*>(this), OnClickedUFunction);
		B->OnClicked.Add(Del);
	}
	return B;
}

void UPauseMenuWidget::OnResumeClicked()
{
	RemoveFromParent();

	// Return control to game (PC phase handling already aligns input mode). :contentReference[oaicite:4]{index=4}
	if (APlayerController* PC = GetOwningPlayer())
	{
		FInputModeGameOnly M;
		PC->SetInputMode(M);
		PC->bShowMouseCursor = false;
	}
}

void UPauseMenuWidget::OnSettingsClicked()
{
	// Hand-off to your settings UI (UIManager route or a delegate).
	// Left as a no-op stub so you can wire to your menu system.
}

void UPauseMenuWidget::OnLeaveClicked()
{
	// Minimal default: return to main menu map via console/openlevel.
	// Replace with your travel/return flow as needed.
	if (UWorld* W = GetWorld())
	{
		UGameplayStatics::OpenLevel(W, FName(TEXT("MainMenu")));
	}
}

void UPauseMenuWidget::OnQuitClicked()
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		UKismetSystemLibrary::QuitGame(this, PC, EQuitPreference::Quit, false);
	}
}
