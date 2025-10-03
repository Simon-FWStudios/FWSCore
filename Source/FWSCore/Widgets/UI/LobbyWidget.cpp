// LobbyWidget.cpp

#include "LobbyWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/ScrollBox.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/SizeBox.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "FWSCore/Gameplay/FWSLobbyGameState.h"
#include "FWSCore/Systems/Unified/UnifiedSubsystemManager.h"

TSharedRef<SWidget> ULobbyWidget::RebuildWidget()
{
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"));
	}
	if (!WidgetTree->RootWidget)
	{
		BuildIfMissing(); // constructs RootCanvas + AutoRoot + children
	}
	return Super::RebuildWidget();
}

void ULobbyWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	BuildIfMissing();
}

void ULobbyWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildIfMissing();

	// locate facade (Path B)
	if (UWorld* W = GetWorld())
	{
		if (UGameInstance* GI = W->GetGameInstance())
		{
			Unified = GI->GetSubsystem<UUnifiedSubsystemManager>();
		}
	}

	BindEvents();

	// light polling so the list stays fresh if players join/leave
	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().SetTimer(TickRefreshTimer, this, &ULobbyWidget::RefreshFromWorld, 1.0f, true);
	}

	RefreshFromWorld();
	UpdateIdentityTexts();
}

void ULobbyWidget::NativeDestruct()
{
	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(TickRefreshTimer);
	}
	UnbindEvents();
	Super::NativeDestruct();
}

void ULobbyWidget::BuildIfMissing()
{
	// Root canvas
	UCanvasPanel* Canvas = Cast<UCanvasPanel>(GetRootWidget());
	if (!Canvas)
	{
		Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
		WidgetTree->RootWidget = Canvas;
	}

	// Content root
	if (!AutoRoot)
	{
		AutoRoot = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("AutoRoot"));
		UCanvasPanelSlot* CanvasSlot = Canvas->AddChildToCanvas(AutoRoot);
		CanvasSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		CanvasSlot->SetOffsets(FMargin(12.f));
	}

	// Header
	if (!Text_Title || !Text_Subtitle || !Text_Count)
	{
		UBorder* Header = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Header"));
		Header->SetBrushColor(FLinearColor(0.08f, 0.12f, 0.20f, 0.85f));
		Header->SetPadding(FMargin(8.f));

		UHorizontalBox* HeaderRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("HeaderRow"));

		Text_Title = MakeLabel(TEXT("Lobby"));
		Text_Subtitle = MakeLabel(TEXT(""));
		Text_Count = MakeLabel(TEXT("0 / 0"));

		if (UHorizontalBoxSlot* S1 = HeaderRow->AddChildToHorizontalBox(Text_Title))
		{
			S1->SetPadding(FMargin(0,0,12,0));
		}
		if (UHorizontalBoxSlot* S2 = HeaderRow->AddChildToHorizontalBox(Text_Subtitle))
		{
			S2->SetPadding(FMargin(0,0,12,0));
		}
		if (UHorizontalBoxSlot* S3 = HeaderRow->AddChildToHorizontalBox(Text_Count))
		{
			S3->SetPadding(FMargin(0,0,12,0));
		}

		Header->SetContent(HeaderRow);
		AutoRoot->AddChildToVerticalBox(Header);
	}

	// Buttons row
	if (!Btn_Invite || !Btn_Start || !Btn_Settings || !Btn_Leave)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ButtonsRow"));
		Btn_Invite   = MakeButton(TEXT("Invite"),   GET_FUNCTION_NAME_CHECKED(ULobbyWidget, OnInviteClicked));
		Btn_Start    = MakeButton(TEXT("Start"),    GET_FUNCTION_NAME_CHECKED(ULobbyWidget, OnStartClicked));
		Btn_Settings = MakeButton(TEXT("Settings"), GET_FUNCTION_NAME_CHECKED(ULobbyWidget, OnSettingsClicked));
		Btn_Leave    = MakeButton(TEXT("Leave"),    GET_FUNCTION_NAME_CHECKED(ULobbyWidget, OnLeaveClicked));

		Row->AddChildToHorizontalBox(Btn_Invite);
		Row->AddChildToHorizontalBox(Btn_Start);
		Row->AddChildToHorizontalBox(Btn_Settings);
		Row->AddChildToHorizontalBox(Btn_Leave);

		AutoRoot->AddChildToVerticalBox(Row);
	}

	// Player list
	if (!List_Players)
	{
		List_Players = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("List_Players"));
		if (USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PlayersSize")))
		{
			Box->SetWidthOverride(640.f);
			Box->SetHeightOverride(360.f);
			Box->AddChild(List_Players);
			AutoRoot->AddChildToVerticalBox(Box);
		}
		else
		{
			AutoRoot->AddChildToVerticalBox(List_Players);
		}
	}
	
	if (!Text_Identity)
	{
		UHorizontalBox* Footer = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Footer"));
		UBorder* FooterBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("FooterBorder"));
		FooterBorder->SetBrushColor(FLinearColor(0.08f, 0.08f, 0.08f, 0.95f));
		FooterBorder->SetPadding(FMargin(6.f));
		FooterBorder->SetContent(Footer);

		// left-aligned identity text
		Text_Identity = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("IdentityText"));
		Text_Identity->SetText(FText::FromString(TEXT("")));
		Footer->AddChildToHorizontalBox(Text_Identity);

		AutoRoot->AddChildToVerticalBox(FooterBorder);
	}
}

void ULobbyWidget::BindEvents()
{
	if (!Unified) return;

	Unified->OnLobbySummariesUpdated_U.AddDynamic(this, &ULobbyWidget::HandleLobbySummariesUpdated);
	Unified->OnLobbyCreated.AddDynamic(this, &ULobbyWidget::HandleLobbyCreated);
	Unified->OnLobbyJoined.AddDynamic(this, &ULobbyWidget::HandleLobbyJoined);
	Unified->OnLobbyUpdated.AddDynamic(this, &ULobbyWidget::HandleLobbyUpdated);
	Unified->OnLobbyLeftOrDestroyed.AddDynamic(this, &ULobbyWidget::HandleLobbyLeftOrDestroyed);
	Unified->OnAuthChanged.AddDynamic(this, &ULobbyWidget::HandleAuthChanged);
}

void ULobbyWidget::UnbindEvents()
{
	if (!Unified) return;

	Unified->OnLobbySummariesUpdated_U.RemoveDynamic(this, &ULobbyWidget::HandleLobbySummariesUpdated);
	Unified->OnLobbyCreated.RemoveDynamic(this, &ULobbyWidget::HandleLobbyCreated);
	Unified->OnLobbyJoined.RemoveDynamic(this, &ULobbyWidget::HandleLobbyJoined);
	Unified->OnLobbyUpdated.RemoveDynamic(this, &ULobbyWidget::HandleLobbyUpdated);
	Unified->OnLobbyLeftOrDestroyed.RemoveDynamic(this, &ULobbyWidget::HandleLobbyLeftOrDestroyed);
	Unified->OnAuthChanged.RemoveDynamic(this, &ULobbyWidget::HandleAuthChanged);
}

UTextBlock* ULobbyWidget::MakeLabel(const FString& Text) const
{
	UTextBlock* L = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	L->SetText(FText::FromString(Text));
	L->SetFont(FSlateFontInfo(GEngine->GetMediumFont(), 18));
	return L;
}

UButton* ULobbyWidget::MakeButton(const FString& Label, FName OnClickedUFunction) const
{
	UButton* B = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	T->SetText(FText::FromString(Label));
	B->AddChild(T);

	if (!OnClickedUFunction.IsNone())
	{
		FScriptDelegate Del; Del.BindUFunction(const_cast<ULobbyWidget*>(this), OnClickedUFunction);
		B->OnClicked.Add(Del);
	}
	return B;
}

// ----------------- Clicks -----------------

void ULobbyWidget::OnInviteClicked()
{
	if (Unified)
	{
		Unified->ShowOverlay();
	}
}

void ULobbyWidget::OnStartClicked()
{
	// Host-only: if we are the server, trigger your match start (travel) logic.
	// Commonly this is in LobbyGameMode; doing it here keeps UI simple.
	if (UWorld* W = GetWorld())
	{
		if (W->GetAuthGameMode()) // listen/host only
		{
			// Let your GameMode/flow handle the actual travel.
			// If you already have a callable, call it here.
			// Otherwise, send a console command or broadcast via a delegate.
			W->ServerTravel(TEXT("?listen")); // Placeholder: replace with your travel to match map
		}
	}
}

void ULobbyWidget::OnSettingsClicked()
{
	// Hand-off to your menu router (UIManager can listen if desired)
	// Here we keep it as a no-op placeholder to integrate with your settings screen later.
}

void ULobbyWidget::OnLeaveClicked()
{
	if (Unified)
	{
		Unified->LeaveLobby();
	}
}

// ------------- Subsystem events -------------

void ULobbyWidget::HandleLobbySummariesUpdated(const TArray<FUnifiedLobbySummary>& Summaries)
{
	RefreshFromWorld();
}
void ULobbyWidget::HandleLobbyCreated(bool /*bSuccess*/, const FString& /*Error*/)
{
	RefreshFromWorld();
}
void ULobbyWidget::HandleLobbyJoined()
{
	RefreshFromWorld();
}
void ULobbyWidget::HandleLobbyUpdated()
{
	RefreshFromWorld();
}
void ULobbyWidget::HandleLobbyLeftOrDestroyed()
{
	RefreshFromWorld();
	
	if (Text_Title)    Text_Title->SetText(FText::FromString(TEXT("Lobby")));
	if (Text_Subtitle) Text_Subtitle->SetText(FText::FromString(TEXT("")));
	if (Text_Count)    Text_Count->SetText(FText::FromString(TEXT("0 / 0")));
	if (List_Players)  List_Players->ClearChildren();
}

void ULobbyWidget::HandleAuthChanged(bool /*bLoggedIn*/, const FString& /*Message*/)
{
	UpdateIdentityTexts();
}

// ------------- Populate -------------

void ULobbyWidget::RefreshFromWorld()
{
	UpdateHeaderTexts();
	RebuildPlayerList();

	// Show/Hide Start button based on authority (host only)
	if (Btn_Start)
	{
		const bool bIsHost = GetWorld() && GetWorld()->GetAuthGameMode();
		Btn_Start->SetVisibility(bIsHost ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void ULobbyWidget::UpdateHeaderTexts()
{
	FString LobbyName = TEXT("Lobby");
	FString Map = TEXT("Map");
	FString Mode = TEXT("Mode");
	int32 Members = 0, Max = 0;

	if (AFWSLobbyGameState* LGS = GetWorld() ? GetWorld()->GetGameState<AFWSLobbyGameState>() : nullptr)
	{
		LobbyName = LGS->Lobby.Name;
		Map       = LGS->Lobby.Map;
		Mode      = LGS->Lobby.Mode;
		Members   = LGS->Lobby.MemberCount;
		Max       = LGS->Lobby.MaxMembers;
	}
	else if (Unified)
	{
		TArray<FUnifiedLobbySummary> L;
		Unified->GetCachedLobbySummaries_U(L);
		// find the one where MemberCount includes us or name matches current
		if (L.Num() > 0)
		{
			const auto& S = L[0];
			LobbyName = S.Name; Map = S.Map; Mode = S.Mode; Members = S.MemberCount; Max = S.MaxMembers;
		}
	}
	
	if (Text_Title)    Text_Title->SetText(FText::FromString(LobbyName));
	if (Text_Subtitle) Text_Subtitle->SetText(FText::FromString(FString::Printf(TEXT("%s • %s"), *Map, *Mode)));
	if (Text_Count)    Text_Count->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), Members, Max)));
}

void ULobbyWidget::RebuildPlayerList()
{
	if (!List_Players) return;

	List_Players->ClearChildren();

	const UWorld* W = GetWorld();
	const AGameStateBase* GS = W ? W->GetGameState() : nullptr;
	if (!GS) return;

	for (APlayerState* PS : GS->PlayerArray)
	{
		if (!PS) continue;
		UTextBlock* Row = MakeLabel(PS->GetPlayerName());
		List_Players->AddChild(Row);
	}
}

void ULobbyWidget::UpdateIdentityTexts()
{
	if (!Text_Identity) return;

	FString Display = TEXT(""), EpicId = TEXT(""), Puid = TEXT("");
	if (Unified)
	{
		Display = Unified->GetDisplayName();
		EpicId  = Unified->GetEpicAccountId();
		Puid    = Unified->GetProductUserId();
	}
	const FString Line = FString::Printf(TEXT("%s  |  EAS: %s  |  PUID: %s"), *Display, *EpicId, *Puid);
	Text_Identity->SetText(FText::FromString(Line));
}