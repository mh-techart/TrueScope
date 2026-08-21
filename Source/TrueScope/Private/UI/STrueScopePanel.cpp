// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "UI/STrueScopePanel.h"

#include "Styling/AppStyle.h"
#include "Styling/ToolBarStyle.h"
#include "TrueScopeSettings.h"
#include "UI/STrueScopeWidget.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "STrueScopePanel"

namespace
{
	/**
	 * The panel header's own padding. Deliberately tight: the header is chrome
	 * that every panel pays for, and in a 2x2 grid the cost is paid four times
	 * (see phase-6.4 Task 8, where the minimum-canvas thresholds are re-derived
	 * against the height this produces).
	 */
	const FMargin HeaderPadding(6.0f, 1.0f, 2.0f, 1.0f);

	/**
	 * The engine's "hamburger" (Starship/Common/menu.svg). It is registered only
	 * as part of a toolbar style, not under a style name of its own, so this is
	 * how it is reached without copying the file into the plugin — see the
	 * phase-6.4 icon table.
	 */
	const FSlateBrush* GetPanelMenuBrush()
	{
		return &FAppStyle::Get().GetWidgetStyle<FToolBarStyle>("SlimToolBar").WrapButtonStyle.ExpandBrush;
	}
}

void STrueScopePanel::Construct(const FArguments& InArgs,
	const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& InAnalyzer)
{
	PanelIndex = FMath::Clamp(InArgs._PanelIndex, 0, TrueScope::NumPanels - 1);
	ScopeType = UTrueScopeSettings::Get().GetPanelScopeType(PanelIndex);

	const TSharedRef<SWidget> Canvas = TrueScope::MakeScopeWidget(ScopeType, InAnalyzer, PanelIndex, &ScopeWidget);

	ChildSlot
	[
		SNew(SVerticalBox)

		// The panel header is a row of its own, so the canvas below keeps all
		// of its remaining area rather than being drawn under an overlay.
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("Brushes.Header"))
			.Padding(HeaderPadding)
			[
				SNew(SHorizontalBox)

				// Left: which scope this panel shows. Static text for now; the
				// control that switches the type comes with the layout system.
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(TrueScope::GetScopeTypeDisplayName(ScopeType))
					.TextStyle(FAppStyle::Get(), "SmallText")
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]

				// Right: the panel menu. Engine pattern copied from the details
				// panel's "View Options" button (SDetailsView.cpp): a borderless
				// SComboButton with no down arrow, carrying just the icon.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(SComboButton)
					.HasDownArrow(false)
					.ContentPadding(0.0f)
					.ForegroundColor(FSlateColor::UseForeground())
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ToolTipText(LOCTEXT("PanelMenuTooltip", "Panel options"))
					.OnGetMenuContent(FOnGetContent::CreateSP(this, &STrueScopePanel::BuildPanelMenu))
					.ButtonContent()
					[
						SNew(SImage)
						.Image(GetPanelMenuBrush())
						.ColorAndOpacity(FSlateColor::UseForeground())
					]
				]
			]
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			Canvas
		]
	];
}

TSharedRef<SWidget> STrueScopePanel::BuildPanelMenu()
{
	// Built by the scope itself, which is also what its right-click menu calls:
	// the two entry points cannot drift apart because there is one builder
	// (M6.4 Task 4). A type with no widget yet has no options to offer.
	if (ScopeWidget.IsValid())
	{
		return ScopeWidget->BuildPanelMenu();
	}

	return SNullWidget::NullWidget;
}

#undef LOCTEXT_NAMESPACE
