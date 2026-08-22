// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "UI/STrueScopeWidget.h"

#include "Analysis/TrueScopeAnalyzer.h"
#include "Capture/TrueScopeViewExtension.h"
#include "Editor.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Textures/SlateIcon.h"
#include "TrueScopeModule.h"
#include "TrueScopeSettings.h"
#include "UI/STrueScopeGainSlider.h"
#include "UI/TrueScopeDraw.h"
#include "UI/TrueScopeStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "STrueScopeWidget"

namespace
{
	/**
	 * UI poll rate for new results. Faster than the ~2.5 Hz capture rate so a
	 * fresh result appears without perceptible extra latency; polling is a
	 * serial compare, so an idle tick costs nothing.
	 */
	constexpr float ResultPollPeriodSeconds = 0.1f;

	/**
	 * Gain slider width inside the panel menu. Wide enough that the +/-3
	 * doublings of travel are draggable in meaningful steps (one doubling is a
	 * sixth of this), and the menu is sized by its widest row, so it costs
	 * nothing elsewhere.
	 */
	constexpr float GainSliderWidth = 120.0f;

	/**
	 * The three things the user needs: that the scopes are paused, why, and how to
	 * resume. Deliberately says "another viewport", not "another *suitable*
	 * viewport" — whether the next one is in a scope-friendly view mode is the
	 * user's call, and TrueScope never switches source on its own
	 * (architecture.md, "No automatic fallback").
	 */
	FText GetPausedHintText()
	{
		// — is the em dash: written as an escape so the source stays ASCII and
		// cannot be mangled by a compiler's source charset.
		return LOCTEXT("PausedInPie",
			"Paused \u2014 viewport in Play (PIE).\nYou may also focus another viewport.");
	}

	/**
	 * The unified empty-state (phase-5.2): identical on all four scopes when a
	 * scope has no data to draw \u2014 no source measured yet, or the current
	 * result carries nothing for this scope.
	 */
	FText GetEmptyHintText()
	{
		return LOCTEXT("NoData",
			"No data \u2014 nothing measured yet.\nFocus a Level Editor viewport to measure.");
	}

	/**
	 * Below the readable size (phase-6.1 Task 5). Deliberately one short line:
	 * the panel that triggers it has no room for a second one, and a hint that
	 * does not fit is not drawn at all.
	 */
	FText GetTooSmallHintText()
	{
		return LOCTEXT("PanelTooSmall", "Panel too small");
	}
}

void STrueScopeWidget::InitializeScopeWidget(const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& InAnalyzer,
	ETrueScopeType InScopeType, bool bInMaintainSquareAspect, int32 InPanelIndex)
{
	Analyzer = InAnalyzer;
	RasterSizes = FTrueScopeModule::Get().GetRasterSizes();
	DisplayGains = FTrueScopeModule::Get().GetDisplayGains();
	ScopeType = InScopeType;
	PanelIndex = FMath::Clamp(InPanelIndex, 0, TrueScope::NumPanels - 1);
	bMaintainSquareAspect = bInMaintainSquareAspect;

	RestoreOptionsFromPanelState();

	if (Analyzer)
	{
		RegisterActiveTimer(ResultPollPeriodSeconds,
			FWidgetActiveTimerDelegate::CreateSP(this, &STrueScopeWidget::PollAnalyzer));
	}
}

void STrueScopeWidget::RestoreOptionsFromPanelState()
{
	// Reopen in the display mode the user left this scope in (phase-6.1 Task 3).
	// Validated against the scope's own modes, so a stale or hand-edited ini
	// cannot put a scope into a mode it does not have.
	const int32 SavedMode = UTrueScopeSettings::Get().GetPanelDisplayMode(PanelIndex);
	for (const FTrueScopeDisplayMode& Mode : GetDisplayModes())
	{
		if (Mode.Id == SavedMode)
		{
			SetDisplayModeId(SavedMode);
			break;
		}
	}

	// Toggle options likewise (phase-6.2): only keys the scope actually offers
	// are restored. The panel state always has a value (its compiled default),
	// so unlike the pre-M6.4 map there is no "absent" case to fall back from.
	for (const FTrueScopeToggleOption& Toggle : GetToggleOptions())
	{
		SetToggleOptionValue(Toggle.Key,
			UTrueScopeSettings::Get().GetPanelToggleOption(PanelIndex, Toggle.Key));
	}

	// Option groups likewise (work order 15), validated against the group's own
	// options so a stale ini cannot select a choice the group does not have.
	for (const FTrueScopeOptionGroup& Group : GetOptionGroups())
	{
		const int32 SavedValue = UTrueScopeSettings::Get().GetPanelOptionGroupValue(PanelIndex, Group.Key);
		for (const FTrueScopeDisplayMode& Option : Group.Options)
		{
			if (Option.Id == SavedValue)
			{
				SetOptionGroupValue(Group.Key, SavedValue);
				break;
			}
		}
	}

	// The gain slider likewise (M6.3). What is stored is the multiplier, so a
	// later recalibration of this scope's default gain — or a change to the
	// slider's range — leaves the user's remembered setting meaning the same
	// thing. Clamped to the slider's endpoints so a hand-edited ini cannot put
	// a scope at a gain the slider cannot express or undo.
	if (const FTrueScopeGainOption* Gain = GetGainOption())
	{
		const float SavedGain = UTrueScopeSettings::Get().GetPanelGainMultiplier(PanelIndex, Gain->Key);
		GainMultiplier = FMath::Clamp(SavedGain,
			TrueScope::MinGainMultiplier(), TrueScope::MaxGainMultiplier());
		PublishGainMultiplier();
	}
}

void STrueScopeWidget::RequestRasterSize(TrueScope::ERasterTarget Target, const FGeometry& AllottedGeometry,
	FVector2f LocalSize) const
{
	if (!RasterSizes)
	{
		return; // no producer (commandlet / test); nothing rasterizes on the GPU
	}

	const FVector2f DeviceSize = LocalSize * AllottedGeometry.Scale;
	RasterSizes->Request(Target, TrueScope::ClampRasterSize(DeviceSize));
}

EActiveTimerReturnType STrueScopeWidget::PollAnalyzer(double InCurrentTime, float InDeltaTime)
{
	const uint64 Serial = Analyzer->GetResultSerial();
	if (Serial != LastSeenSerial)
	{
		LastSeenSerial = Serial;
		CachedResult = Analyzer->GetLatestResult();
		LastResultTimeSeconds = InCurrentTime;
		OnAnalysisResultChanged();
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	else if (LastResultTimeSeconds < 0.0)
	{
		// No result yet: start the staleness clock on the first tick, so a scope
		// that has never measured anything does not count as stale from time zero.
		LastResultTimeSeconds = InCurrentTime;
	}

	if (UpdateScopeState(InCurrentTime))
	{
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	return EActiveTimerReturnType::Continue;
}

bool STrueScopeWidget::IsPlayInEditorActive() const
{
	// Simulate drives runtime objects while the scopes keep measuring, so it is
	// not a paused state (architecture.md, "Behaviour during PIE").
	return GEditor != nullptr && GEditor->IsPlayingSessionInEditor() && !GEditor->IsSimulatingInEditor();
}

bool STrueScopeWidget::UpdateScopeState(double NowSeconds)
{
	const double SecondsSinceLastResult =
		LastResultTimeSeconds >= 0.0 ? NowSeconds - LastResultTimeSeconds : 0.0;

	// The staleness clock runs in capture intervals, so it follows the user's
	// measurement rate instead of pinning M2's fixed 2.5 Hz (phase-6.1 Task 2).
	const TrueScope::EScopeState NewState = TrueScope::DetermineScopeState(
		IsPlayInEditorActive(), SecondsSinceLastResult, FTrueScopeModule::Get().GetCaptureIntervalSeconds());

	if (NewState == ScopeState)
	{
		return false;
	}

	ScopeState = NewState;
	return true;
}

int32 STrueScopeWidget::DrawPausedHint(FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FGeometry& AllottedGeometry, const FSlateRect& Content) const
{
	if (!ShouldDrawPausedHint())
	{
		return LayerId;
	}

	return TrueScope::Draw::DrawCenteredHint(OutDrawElements, LayerId, AllottedGeometry, Content,
		GetPausedHintText());
}

int32 STrueScopeWidget::DrawScopeHints(FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FGeometry& AllottedGeometry, const FSlateRect& Content) const
{
	// Too small wins over everything (phase-6.1 Task 5): at this size the scope
	// conveys nothing, so what it would otherwise say about its data is moot —
	// and only one hint can fit anyway. Below the size where even this hint's
	// plate fits, DrawCenteredHint stays silent, which is the intended
	// degradation: nothing, never clipped text.
	if (TrueScope::Draw::IsTooSmallToRead(Content.GetSize()))
	{
		return TrueScope::Draw::DrawCenteredHint(OutDrawElements, LayerId, AllottedGeometry, Content,
			GetTooSmallHintText());
	}

	// Paused wins over "no data": it means values are being HELD (data exists and
	// stays readable under the hint), which is more specific than "no data".
	if (ShouldDrawPausedHint())
	{
		return DrawPausedHint(OutDrawElements, LayerId, AllottedGeometry, Content);
	}

	if (!HasDrawableData())
	{
		return TrueScope::Draw::DrawCenteredHint(OutDrawElements, LayerId, AllottedGeometry, Content,
			GetEmptyHintText());
	}

	return LayerId;
}

FVector2D STrueScopeWidget::ComputeDesiredSize(float) const
{
	// Scopes live in splitter panels that stretch them; the desired size only
	// matters as a lower bound.
	return FVector2D(64.0, 64.0);
}

FSlateRect STrueScopeWidget::GetScopeContentRect(const FGeometry& AllottedGeometry) const
{
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	if (!bMaintainSquareAspect)
	{
		return FSlateRect(0.0f, 0.0f, Size.X, Size.Y);
	}

	// Aspect-locked (vectorscope, M4): largest centered square — uniform
	// scale, never distorted, no matter how narrow or wide the panel.
	const float Side = FMath::Min(Size.X, Size.Y);
	const float Left = (Size.X - Side) * 0.5f;
	const float Top = (Size.Y - Side) * 0.5f;
	return FSlateRect(Left, Top, Left + Side, Top + Side);
}

FText STrueScopeWidget::GetDisplayModeSectionLabel() const
{
	return LOCTEXT("DisplaySection", "Display");
}

TSharedRef<SWidget> STrueScopeWidget::BuildPanelMenu()
{
	// The single builder behind the panel's only menu entry point (M6.4 Task 4,
	// M6.4.1 Task 3): the panel header's menu button. Everything in it is
	// derived from what the scope itself declares, so a panel only ever shows
	// options its own type has.
	// Built with TrueScope's menu style so that every row of this menu starts
	// in the same text column as every row of every other TrueScope menu — the
	// column depends on which check widget the menu contains, so the menu has
	// to declare that up front (TrueScopeStyle.cpp carries the derivation).
	FMenuBuilder MenuBuilder(/*bShouldCloseWindowAfterMenuSelection*/ true, nullptr,
		/*Extender*/ nullptr, /*bCloseSelfOnly*/ false,
		&TrueScope::GetMenuStyle(GetPanelMenuCheckColumn()));

	if (!GetDisplayModes().IsEmpty())
	{
		MenuBuilder.BeginSection(TEXT("TrueScopeDisplayModes"), GetDisplayModeSectionLabel());
		for (const FTrueScopeDisplayMode& Mode : GetDisplayModes())
		{
			MenuBuilder.AddMenuEntry(Mode.Label, FText::GetEmpty(), FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &STrueScopeWidget::ApplyDisplayMode, Mode.Id),
					FCanExecuteAction(),
					FIsActionChecked::CreateSP(this, &STrueScopeWidget::IsDisplayModeActive, Mode.Id)),
				NAME_None, EUserInterfaceActionType::RadioButton);
		}
		AddToggleOptionEntries(MenuBuilder, NAME_None);
		MenuBuilder.EndSection();
	}

	// Each secondary radio group gets its own named section, with the toggles
	// that belong to it after its choices (the skin tone line closes "Markers").
	for (const FTrueScopeOptionGroup& Group : GetOptionGroups())
	{
		MenuBuilder.BeginSection(Group.Key, Group.SectionLabel);
		for (const FTrueScopeDisplayMode& Option : Group.Options)
		{
			MenuBuilder.AddMenuEntry(Option.Label, FText::GetEmpty(), FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &STrueScopeWidget::HandleSelectOptionGroup, Group.Key,
						Option.Id),
					FCanExecuteAction(),
					FIsActionChecked::CreateSP(this, &STrueScopeWidget::IsOptionGroupValueActive, Group.Key,
						Option.Id)),
				NAME_None, EUserInterfaceActionType::RadioButton);
		}
		AddToggleOptionEntries(MenuBuilder, Group.Key);
		MenuBuilder.EndSection();
	}

	// The gain slider, unsectioned, after the choices: it is the only continuous
	// control in the menu, and it is the one an artist reaches for most, so it
	// sits where the eye lands after the radio rows rather than behind a
	// heading of its own.
	if (const FTrueScopeGainOption* Gain = GetGainOption())
	{
		MenuBuilder.AddWidget(BuildGainSlider(*Gain), Gain->Label, /*bNoIndent*/ false,
			/*bSearchable*/ true, Gain->Tooltip);
	}

	// Last entry of every panel menu (M6.4 Task 6), below a separator: it acts
	// on the whole panel rather than on any one option above it.
	MenuBuilder.AddSeparator();
	MenuBuilder.AddMenuEntry(
		FUIAction(FExecuteAction::CreateSP(this, &STrueScopeWidget::ResetPanelToDefaults)),
		BuildResetRow(GetPanelMenuCheckColumn()), NAME_None,
		LOCTEXT("ResetPanelTooltip",
			"Return this panel's display options to their defaults. Affects only this panel."));

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> STrueScopeWidget::BuildResetRow(TrueScope::EMenuCheckColumn CheckColumn)
{
	// The reset row is the one ACTION row in menus otherwise made of STATE
	// rows, and it carries the editor's standard reset arrow to say so: a radio
	// dot and a check box report how things stand, the arrow says what happens
	// on click. The same artwork appears throughout the editor (details panel,
	// viewport show-flags), so it is recognised before the words are read.
	//
	// It is drawn in the CHECK column, not in the engine's icon column, which
	// is why this row is built by hand rather than passed as an FSlateIcon.
	// The engine puts an entry's icon in a box of its own AFTER the checkmark
	// column (SMenuEntryBlock.cpp:581-600: an SBox of Menu.MenuIconSize + 2 in
	// a slot padded 2 and 6), so an icon on one row pushes that row's label
	// MenuIconSize + 10 = 24 units right of every other row — the alignment
	// M6.4.1 Task 2 exists to prevent. The engine's own answer to a mixed
	// section is "give every row an icon" (SMenuEntryBlock.cpp:512-521, the
	// all-or-none menu policy), which is not available here: the other rows'
	// state is drawn by their check widgets, and inventing artwork for each
	// would say less than the dot already says.
	//
	// So the icon goes where the dots go. Supplying custom contents replaces
	// the icon box and the label together (SMenuEntryBlock.cpp:576), and the
	// contents start one fixed distance to the right of the checkmark column:
	//
	//   check slot        FMargin(2, 0, 8, 0) around the column   (:632)
	//   contents slot     FMargin(2, 0, 0, 0)                     (:653)
	//
	// so the column's left edge is its own width plus the 8 and the 2 that
	// follow it — column + 10 units — left of where these contents begin, and
	// the icon reaches it with that much negative left padding. (The check
	// slot's leading 2 sits before the column, not between it and the text, so
	// it is not part of the distance.) Because the check widget on THIS row is collapsed
	// (a plain action has nothing to check, SMenuEntryBlock.cpp:546-549) the
	// space it reserves is empty, so nothing is overlapped or clipped — the
	// icon simply occupies the column its neighbours use.
	//
	// The label then has to land where every other row's label lands. Entry
	// contents begin 2 units before the shared text column, and a horizontal
	// box places the label at the icon slot's total width, so that width must
	// come to exactly 2: PullLeft is spent, the icon's own extent is added, and
	// the right padding makes up the difference. Every term is an engine
	// constant, none is chosen by eye — a wrong one shows up as a failing
	// TrueScope.UI.MenuTextColumn, which measures the built rows.
	const float PullLeft = TrueScope::GetMenuCheckColumnWidth(CheckColumn) + 10.0f;
	const float IconExtent = FCoreStyle::Get().GetFloat("Menu", ".MenuIconSize", 14.0f);
	const float LabelIndent = 2.0f;

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(-PullLeft, 0.0f, PullLeft - IconExtent + LabelIndent, 0.0f))
		[
			SNew(SBox)
			.WidthOverride(IconExtent)
			.HeightOverride(IconExtent)
			[
				SNew(SImage)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				// Named for what it marks in a details panel rather than for
				// what it depicts; the artwork is the reset arrow (M6.4).
				.Image(FAppStyle::GetBrush("PropertyWindow.DiffersFromDefault"))
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.TextStyle(&TrueScope::GetMenuStyle(CheckColumn), "Menu.Label")
			.Text(LOCTEXT("ResetPanel", "Reset to Defaults"))
		];
}

TrueScope::EMenuCheckColumn STrueScopeWidget::GetPanelMenuCheckColumn() const
{
	// Must describe what BuildPanelMenu() above actually adds: a toggle entry
	// draws a check box and widens the menu's checkmark column, a radio entry
	// only a dot, and a menu of plain buttons has no column at all. Toggles are
	// only reachable inside a section, so a toggle whose section never gets
	// built does not count. A drift between this and the builder shows up as a
	// failing TrueScope.UI.MenuTextColumn, which measures the built rows.
	const bool bHasDisplayModes = !GetDisplayModes().IsEmpty();
	const bool bHasGroups = !GetOptionGroups().IsEmpty();

	for (const FTrueScopeToggleOption& Toggle : GetToggleOptions())
	{
		if (Toggle.SectionKey.IsNone() ? bHasDisplayModes
										: GetOptionGroups().ContainsByPredicate(
											  [&Toggle](const FTrueScopeOptionGroup& Group)
											  { return Group.Key == Toggle.SectionKey; }))
		{
			return TrueScope::EMenuCheckColumn::CheckBox;
		}
	}

	return (bHasDisplayModes || bHasGroups) ? TrueScope::EMenuCheckColumn::Radio
											: TrueScope::EMenuCheckColumn::None;
}

void STrueScopeWidget::AddToggleOptionEntries(FMenuBuilder& MenuBuilder, FName SectionKey)
{
	for (const FTrueScopeToggleOption& Toggle : GetToggleOptions())
	{
		if (Toggle.SectionKey != SectionKey)
		{
			continue;
		}

		MenuBuilder.AddMenuEntry(Toggle.Label, Toggle.Tooltip, FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &STrueScopeWidget::HandleFlipToggleOption, Toggle.Key),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &STrueScopeWidget::IsToggleOptionChecked, Toggle.Key)),
			NAME_None, EUserInterfaceActionType::ToggleButton);
	}
}

TSharedRef<SWidget> STrueScopeWidget::BuildGainSlider(const FTrueScopeGainOption& Option)
{
	// The slider works in POSITION space and the scope stores a GAIN, so both
	// directions go through the shared law (M6.3) — there is no second copy of
	// it to drift. Everything else about it is unchanged from the toolbar it
	// moved out of: continuous, centre-detented, double-click resets, and no
	// numeric readout.
	const TWeakPtr<STrueScopeWidget> WeakSelf = SharedThis(this);

	return SNew(SBox)
		.WidthOverride(GainSliderWidth)
		.VAlign(VAlign_Center)
		[
			SNew(STrueScopeGainSlider)
			.ToolTipText(Option.Tooltip)
			.Value_Lambda([WeakSelf]()
			{
				const TSharedPtr<STrueScopeWidget> Pinned = WeakSelf.Pin();
				return Pinned.IsValid()
					? TrueScope::GainToSliderPosition(Pinned->GetGainMultiplier())
					: 0.5f;
			})
			.OnValueChanged_Lambda([WeakSelf](float NewPosition)
			{
				if (const TSharedPtr<STrueScopeWidget> Pinned = WeakSelf.Pin())
				{
					Pinned->ApplyGainMultiplier(TrueScope::SliderPositionToGain(NewPosition));
				}
			})
			.OnResetRequested_Lambda([WeakSelf]()
			{
				if (const TSharedPtr<STrueScopeWidget> Pinned = WeakSelf.Pin())
				{
					Pinned->ResetGainMultiplier();
				}
			})
		];
}

void STrueScopeWidget::ResetPanelToDefaults()
{
	// The settings object holds the only copy of the defaults; the widget just
	// re-reads what it wrote. Always available and never confirmed: every value
	// it touches is one menu click to set again, so a dialog would cost more
	// than the mistake it guards against.
	UTrueScopeSettings::Get().ResetPanelToDefaults(PanelIndex);
	RestoreOptionsFromPanelState();
	Invalidate(EInvalidateWidgetReason::Paint);

	// RestoreOptionsFromPanelState publishes the restored gain directly, so the
	// redraw ApplyGainMultiplier would have asked for has to be asked for here
	// — otherwise "Reset to Defaults" would be the one control that still waits
	// for the next capture (M6.4.1).
	FTrueScopeModule::Get().RequestScopeRedraw();
}

void STrueScopeWidget::ApplyDisplayMode(int32 ModeId)
{
	SetDisplayModeId(ModeId);
	UTrueScopeSettings::Get().SetPanelDisplayMode(PanelIndex, ModeId);
	Invalidate(EInvalidateWidgetReason::Paint);
}

bool STrueScopeWidget::IsDisplayModeActive(int32 ModeId) const
{
	return GetDisplayModeId() == ModeId;
}

void STrueScopeWidget::ApplyToggleOption(FName Key, bool bValue)
{
	SetToggleOptionValue(Key, bValue);
	UTrueScopeSettings::Get().SetPanelToggleOption(PanelIndex, Key, bValue);
	Invalidate(EInvalidateWidgetReason::Paint);
}

void STrueScopeWidget::HandleFlipToggleOption(FName Key)
{
	ApplyToggleOption(Key, !GetToggleOptionValue(Key));
}

bool STrueScopeWidget::IsToggleOptionChecked(FName Key) const
{
	return GetToggleOptionValue(Key);
}

void STrueScopeWidget::PublishGainMultiplier() const
{
	if (!DisplayGains)
	{
		return; // no producer (commandlet / test); nothing reads the gains
	}

	// One place maps a scope to its gain slot, so the CPU and GPU paths cannot
	// end up reading different slots for the same scope.
	switch (ScopeType)
	{
	case ETrueScopeType::Parade:
		DisplayGains->SetDensityGain(TrueScope::EDensityScope::Parade, GainMultiplier);
		break;
	case ETrueScopeType::Waveform:
		DisplayGains->SetDensityGain(TrueScope::EDensityScope::Waveform, GainMultiplier);
		break;
	case ETrueScopeType::Vectorscope:
		DisplayGains->SetDensityGain(TrueScope::EDensityScope::Vectorscope, GainMultiplier);
		break;
	case ETrueScopeType::Histogram:
		DisplayGains->SetHistogramGain(GainMultiplier);
		break;
	}
}

void STrueScopeWidget::ApplyGainMultiplier(float Multiplier)
{
	const FTrueScopeGainOption* Gain = GetGainOption();
	if (!Gain)
	{
		return; // scope has no gain control
	}

	const float NewMultiplier = FMath::Clamp(Multiplier,
		TrueScope::MinGainMultiplier(), TrueScope::MaxGainMultiplier());
	if (NewMultiplier == GainMultiplier)
	{
		// A drag reports every mouse move, including the ones that land on the
		// value already set (and every move once the slider is at an end stop).
		// Re-rendering for those would spend a full analysis on an identical
		// picture.
		return;
	}

	GainMultiplier = NewMultiplier;
	PublishGainMultiplier();
	UTrueScopeSettings::Get().SetPanelGainMultiplier(PanelIndex, Gain->Key, GainMultiplier);

	// A repaint alone only moves the CPU histogram, which maps counts to curve
	// height at draw time. Every other scope draws an image the producer
	// rasterized with the OLD gain baked in, and while a menu is open no
	// further capture arrives to replace it — so ask for a re-render of the
	// frame already measured (M6.4.1). No new capture, no readback.
	Invalidate(EInvalidateWidgetReason::Paint);
	FTrueScopeModule::Get().RequestScopeRedraw();
}

void STrueScopeWidget::ApplyOptionGroup(FName Key, int32 ValueId)
{
	SetOptionGroupValue(Key, ValueId);
	UTrueScopeSettings::Get().SetPanelOptionGroupValue(PanelIndex, Key, ValueId);
	Invalidate(EInvalidateWidgetReason::Paint);
}

void STrueScopeWidget::HandleSelectOptionGroup(FName Key, int32 ValueId)
{
	ApplyOptionGroup(Key, ValueId);
}

bool STrueScopeWidget::IsOptionGroupValueActive(FName Key, int32 ValueId) const
{
	return GetOptionGroupValue(Key) == ValueId;
}

#undef LOCTEXT_NAMESPACE
