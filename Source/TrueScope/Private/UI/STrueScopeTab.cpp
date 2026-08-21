// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "UI/STrueScopeTab.h"

#include "Capture/TrueScopeCaptureGate.h"
#include "TrueScopeModule.h"
#include "TrueScopeSettings.h"
#include "UI/STrueScopePanel.h"
#include "UI/STrueScopeStatusLine.h"
#include "UI/STrueScopeMainBar.h"
#include "UI/TrueScopeType.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "STrueScopeTab"

void STrueScopeTab::Construct(const FArguments& InArgs)
{
	const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe> Analyzer = FTrueScopeModule::Get().GetAnalyzer();
	CaptureGate = FTrueScopeModule::Get().GetCaptureGate();

	// Each panel builds its own scope through the type factory and puts a panel
	// header above it (M6.4 Task 3). Which scope a panel shows comes from the
	// panel's own stored state: settings are owned by the panel, and the type is
	// one of them. Today all four are the compiled defaults, so this reproduces
	// the M4 grid exactly.
	//
	// Each panel carries its own options in its panel header since M6.4, so the
	// tab hands the main bar nothing: the bar is global-only.
	TArray<TSharedRef<STrueScopePanel>> Panels;
	for (int32 PanelIndex = 0; PanelIndex < TrueScope::NumPanels; ++PanelIndex)
	{
		Panels.Emplace(SNew(STrueScopePanel, Analyzer).PanelIndex(PanelIndex));
	}

	ChildSlot
	[
		SNew(SVerticalBox)

		// The main bar: TrueScope-wide options only (M6.4).
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STrueScopeMainBar)
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Vertical)

			+ SSplitter::Slot()
			.Value(0.5f)
			[
				SNew(SSplitter)
				.Orientation(Orient_Horizontal)

				+ SSplitter::Slot()
				.Value(0.5f)
				[
					Panels[0]
				]

				+ SSplitter::Slot()
				.Value(0.5f)
				[
					Panels[1]
				]
			]

			+ SSplitter::Slot()
			.Value(0.5f)
			[
				SNew(SSplitter)
				.Orientation(Orient_Horizontal)

				+ SSplitter::Slot()
				.Value(0.5f)
				[
					Panels[2]
				]

				+ SSplitter::Slot()
				.Value(0.5f)
				[
					Panels[3]
				]
			]
		]

		// What is being measured, in which encoding, on which path — and why
		// nothing is being measured when that is the case (phase-6.1 Task 4).
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STrueScopeStatusLine, Analyzer)
		]
	];
}

int32 STrueScopeTab::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Paint == visible and drawn: a closed tab, a tab behind another docked
	// tab and a minimized window all stop painting, which gates capture off.
	if (CaptureGate)
	{
		CaptureGate->NotifyPanelPainted(FPlatformTime::Seconds());
	}

	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId,
		InWidgetStyle, bParentEnabled);
}

#undef LOCTEXT_NAMESPACE
