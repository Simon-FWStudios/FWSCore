#include "MainMenuWidget.h"

// Canvas + layout widgets
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/WidgetSwitcher.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"

// Controls
#include "FWSCore.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/EditableTextBox.h"
#include "Components/CircularThrobber.h"

#include "Blueprint/WidgetTree.h"
#include "Engine/Engine.h"
#include "FWSCore/Systems/UI/UIManagerSubsystem.h"

#define LOCTEXT_NAMESPACE "MainMenu"

TSharedRef<SWidget> UMainMenuWidget::RebuildWidget()
{
	// Ensure WidgetTree exists
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"));
	}

	// Build once (Designer may call multiple times)
	if (!WidgetTree->RootWidget)
	{
		BuildUI();
	}

	return Super::RebuildWidget();
}

void UMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UE_LOG(LogUIManager, Log, TEXT("[UI] MainMenuWidget::NativeConstruct"));

	// Tree is already built by RebuildWidget(); safe to drive state now.
	SwitchScreen("Home");
	SetStatusLine(TEXT("Ready"));
}

void UMainMenuWidget::NativeDestruct()
{
	Super::NativeDestruct();
}

// ------------------- Builders -------------------

void UMainMenuWidget::BuildUI()
{
	// Root: Canvas that fills the viewport
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	WidgetTree->RootWidget = Canvas;

	// Optional debug background so you SEE the widget while iterating
#if 1
	{
		UImage* Bg = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("DebugBG"));
		Bg->SetColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.45f));
		UCanvasPanelSlot* BgSlot = Canvas->AddChildToCanvas(Bg);
		BgSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		BgSlot->SetOffsets(FMargin(0.f));
	}
#endif

	// Screens switcher (fill)
	Switcher = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), TEXT("Screens"));
	{
		UCanvasPanelSlot* CSlot = Canvas->AddChildToCanvas(Switcher);
		CSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		CSlot->SetOffsets(FMargin(0.f, 0.f, 0.f, 36.f)); // leave room for bottom bar
		CSlot->SetAutoSize(false);
		CSlot->SetAlignment(FVector2D(0.f, 0.f));
	}

	// Status bar (bottom, full width)
	UHorizontalBox* StatusBar = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("StatusBar"));
	{
		UCanvasPanelSlot* CSlot = Canvas->AddChildToCanvas(StatusBar);
		CSlot->SetAnchors(FAnchors(0.f, 1.f, 1.f, 1.f));
		CSlot->SetOffsets(FMargin(0.f, -36.f, 0.f, 36.f)); // height 36
		CSlot->SetAutoSize(false);
		CSlot->SetAlignment(FVector2D(0.f, 1.f));
	}

	// Identity (left)
	{
		AvatarImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Avatar"));
		UHorizontalBoxSlot* AvatarSlot = StatusBar->AddChildToHorizontalBox(AvatarImage);
		AvatarSlot->SetPadding(FMargin(8, 2));

		IdentityText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("IdentityText"));
		IdentityText->SetText(FText::FromString(TEXT("Not logged in")));
		UHorizontalBoxSlot* IdSlot = StatusBar->AddChildToHorizontalBox(IdentityText);
		IdSlot->SetPadding(FMargin(8, 2));
	}

	// Spacer fill
	{
		UTextBlock* Spacer = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Spacer"));
		Spacer->SetText(FText::GetEmpty());
		UHorizontalBoxSlot* SpacerSlot = StatusBar->AddChildToHorizontalBox(Spacer);
		SpacerSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	// Busy throbber + status text (right)
	{
		BusyThrobber = WidgetTree->ConstructWidget<UCircularThrobber>(UCircularThrobber::StaticClass(), TEXT("Busy"));
		BusyThrobber->SetVisibility(ESlateVisibility::Collapsed);
		UHorizontalBoxSlot* BusySlot = StatusBar->AddChildToHorizontalBox(BusyThrobber);
		BusySlot->SetPadding(FMargin(8, 2));

		StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusText"));
		StatusText->SetText(FText::FromString(TEXT("Status")));
		UHorizontalBoxSlot* SSlot = StatusBar->AddChildToHorizontalBox(StatusText);
		SSlot->SetPadding(FMargin(8, 2));
	}

	// Build each screen
	BuildHome();
	BuildSettings();
	BuildCharacter();
	BuildProfile();
	BuildPlay();
	BuildConfirm();
	BuildLog();
}

void UMainMenuWidget::BuildHome()
{
	HomePanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Home"));
	Switcher->AddChild(HomePanel);

	auto MakeBtn = [&](const TCHAR* Name, const FText& Label) -> UButton*
	{
		UButton* B = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
		UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *FString::Printf(TEXT("%s_Text"), Name));
		T->SetText(Label);
		B->AddChild(T);
		HomePanel->AddChild(B);
		return B;
	};

	BtnLogin       = MakeBtn(TEXT("BtnLogin"),       LOCTEXT("Login",       "Login"));
	BtnLoginPortal = MakeBtn(TEXT("BtnLoginPortal"), LOCTEXT("LoginPortal", "Login (Portal)"));
	BtnLogout      = MakeBtn(TEXT("BtnLogout"),      LOCTEXT("Logout",      "Logout"));

	if (BtnLogin)       { BtnLogin->OnClicked.AddDynamic(this, &UMainMenuWidget::OnClick_Login); }
	if (BtnLoginPortal) { BtnLoginPortal->OnClicked.AddDynamic(this, &UMainMenuWidget::OnClick_LoginPortal); }
	if (BtnLogout)      { BtnLogout->OnClicked.AddDynamic(this, &UMainMenuWidget::OnClick_Logout); }

	if (UButton* B = MakeBtn(TEXT("BtnOverlay"),     LOCTEXT("Overlay",     "Show Overlay"))) { B->OnClicked.AddDynamic(this, &UMainMenuWidget::OnClick_ShowOverlay); }

	if (UButton* B = MakeBtn(TEXT("BtnSettings"),    LOCTEXT("Settings",    "Settings")))     { B->OnClicked.AddDynamic(this, &UMainMenuWidget::OnClick_ShowSettings); }
	if (UButton* B = MakeBtn(TEXT("BtnCharacter"),   LOCTEXT("Character",   "Character Select"))){ B->OnClicked.AddDynamic(this, &UMainMenuWidget::OnClick_ShowCharacter); }
	if (UButton* B = MakeBtn(TEXT("BtnProfile"),     LOCTEXT("Profile",     "Player Profile"))){ B->OnClicked.AddDynamic(this, &UMainMenuWidget::OnClick_ShowProfile); }
	if (UButton* B = MakeBtn(TEXT("BtnPlay"),        LOCTEXT("Play",        "Play")))         { B->OnClicked.AddDynamic(this, &UMainMenuWidget::OnClick_ShowPlay); }
	if (UButton* B = MakeBtn(TEXT("BtnExit"),        LOCTEXT("Exit",        "Exit Game")))    { B->OnClicked.AddDynamic(this, &UMainMenuWidget::OnClick_ExitGame); }
	SetAuthState(false);
}

void UMainMenuWidget::SetAuthState(bool bIsLoggedIn)
{
	auto V = [&](bool bShow) { return bShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed; };

	if (BtnLogin)       BtnLogin->SetVisibility(      V(!bIsLoggedIn));
	if (BtnLoginPortal) BtnLoginPortal->SetVisibility(V(!bIsLoggedIn));
	if (BtnLogout)      BtnLogout->SetVisibility(     V( bIsLoggedIn));

	// Optional: tweak identity line immediately
	if (bIsLoggedIn)
	{
		// If your facade exposes getters we can show IDs; otherwise just a friendly label.
		SetStatusLine(TEXT("Signed in"));
	}
	else
	{
		SetStatusLine(TEXT("Not signed in"));
	}
}

void UMainMenuWidget::BuildSettings()
{
	SettingsPanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Settings"));
	Switcher->AddChild(SettingsPanel);

	UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	T->SetText(LOCTEXT("SettingsHeader","Settings (player) – TODO UI"));
	SettingsPanel->AddChild(T);
}

void UMainMenuWidget::BuildCharacter()
{
	CharacterPanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Character"));
	Switcher->AddChild(CharacterPanel);

	UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	T->SetText(LOCTEXT("CharacterHeader","Character Select (TODO UI)"));
	CharacterPanel->AddChild(T);
}

void UMainMenuWidget::BuildProfile()
{
	ProfilePanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Profile"));
	Switcher->AddChild(ProfilePanel);

	UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	T->SetText(LOCTEXT("ProfileHeader","Player Profile (Create/Switch/Delete) – TODO UI"));
	ProfilePanel->AddChild(T);
}

void UMainMenuWidget::BuildPlay()
{
	PlayPanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Play"));
	Switcher->AddChild(PlayPanel);

	UTextBlock* Header = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Header->SetText(LOCTEXT("PlayHeader","Play: Online Solo / Host / Join"));
	PlayPanel->AddChild(Header);

	auto MakeBtn = [&](const TCHAR* Name, const FText& Label) -> UButton*
	{
		UButton* B = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
		UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *FString::Printf(TEXT("%s_Text"), Name));
		T->SetText(Label);
		B->AddChild(T);
		PlayPanel->AddChild(B);
		return B;
	};

	// Host
	if (UButton* HostBtn = MakeBtn(TEXT("BtnHost"), LOCTEXT("HostOnline","Host Online (P2P)")))
	{
		HostBtn->OnClicked.AddDynamic(this, &UMainMenuWidget::OnClick_Play_Host);
	}

	// Join by ID (input + button)
	JoinLobbyIdBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("JoinIdBox"));
	JoinLobbyIdBox->SetHintText(LOCTEXT("JoinIdHint","Enter Lobby Id..."));
	PlayPanel->AddChild(JoinLobbyIdBox);

	if (UButton* JoinBtn = MakeBtn(TEXT("BtnJoinId"), LOCTEXT("JoinById","Join by Lobby Id")))
	{
		JoinBtn->OnClicked.AddDynamic(this, &UMainMenuWidget::OnClick_Play_JoinById);
	}
}

void UMainMenuWidget::BuildConfirm()
{
	ConfirmPanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Confirm"));
	Switcher->AddChild(ConfirmPanel);

	UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	T->SetText(LOCTEXT("ConfirmHeader","Confirm Dialog (OK/Cancel) – TODO UI"));
	ConfirmPanel->AddChild(T);
}

void UMainMenuWidget::BuildLog()
{
	LogPanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Log"));
	Switcher->AddChild(LogPanel);

	LogScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("SysLog"));
	LogPanel->AddChild(LogScroll);
}

// ------------------- Helpers -------------------

void UMainMenuWidget::SwitchScreen(FName ScreenName)
{
	static const TMap<FName, int32> Map = {
		{ "Home", 0 }, { "Settings", 1 }, { "Character", 2 }, { "Profile", 3 },
		{ "Play", 4 }, { "Confirm", 5 }, { "Log", 6 }
	};
	if (const int32* Index = Map.Find(ScreenName))
	{
		Switcher->SetActiveWidgetIndex(*Index);
	}
}

void UMainMenuWidget::SetStatusLine(const FString& Line)
{
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(Line));
	}
}

void UMainMenuWidget::AppendLogLine(const FString& Line)
{
	if (!LogScroll) return;
	UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	T->SetText(FText::FromString(Line));
	LogScroll->AddChild(T);
	LogScroll->ScrollToEnd();
}

void UMainMenuWidget::ShowToast(const FString& Message, float /*Duration*/)
{
	SetStatusLine(Message);
	AppendLogLine(Message);
}

void UMainMenuWidget::SetBusy(bool bBusy)
{
	if (BusyThrobber)
	{
		BusyThrobber->SetVisibility(bBusy ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UMainMenuWidget::SetIdentityText(const FString& Display, const FString& EpicId, const FString& PUID)
{
	if (IdentityText)
	{
		const FString S = FString::Printf(TEXT("%s  |  EAS: %s  |  PUID: %s"),
			*Display, *EpicId, *PUID);
		IdentityText->SetText(FText::FromString(S));
	}
}

// Settings notifications (you can flesh out later)
void UMainMenuWidget::OnSettingsLoaded(const FPlayerSettings& /*Settings*/)
{
	AppendLogLine(TEXT("Settings loaded (UI hook)."));
}
void UMainMenuWidget::OnSettingsApplied(const FPlayerSettings& /*Settings*/)
{
	AppendLogLine(TEXT("Settings applied (UI hook)."));
}

// ------------------- Button handlers -------------------

void UMainMenuWidget::OnClick_Login()        { if (Owner) Owner->Login(); }
void UMainMenuWidget::OnClick_LoginPortal()  { if (Owner) Owner->LoginPortal(); }
void UMainMenuWidget::OnClick_Logout()       { if (Owner) Owner->Logout(); }
void UMainMenuWidget::OnClick_ShowOverlay()  { if (Owner) Owner->ShowOverlay(); }

void UMainMenuWidget::OnClick_ShowSettings() { SwitchScreen("Settings"); }
void UMainMenuWidget::OnClick_ShowCharacter(){ SwitchScreen("Character"); }
void UMainMenuWidget::OnClick_ShowProfile()  { SwitchScreen("Profile"); }
void UMainMenuWidget::OnClick_ShowPlay()     { SwitchScreen("Play"); }

void UMainMenuWidget::OnClick_ExitGame()
{
	
}

void UMainMenuWidget::OnClick_Play_Host()
{
	if (!Owner) return;

	FHostParams P;
	P.Name = TEXT("MyLobby");
	P.Map  = TEXT("LobbyMenu"); 
	P.Mode = TEXT("Default");
	P.MaxMembers    = 4;
	P.bPresence     = true;
	P.bAllowInvites = true;

	Owner->HostOnline(P);
}

void UMainMenuWidget::OnClick_Play_JoinById()
{
	if (!Owner) return;

	const FString LobbyId = JoinLobbyIdBox ? JoinLobbyIdBox->GetText().ToString().TrimStartAndEnd() : FString();
	if (LobbyId.IsEmpty())
	{
		AppendLogLine(TEXT("Please enter a Lobby Id."));
		return;
	}
	Owner->JoinByLobbyId(LobbyId);
}

#undef LOCTEXT_NAMESPACE
