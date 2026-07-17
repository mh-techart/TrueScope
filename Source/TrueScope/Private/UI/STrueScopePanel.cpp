// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "UI/STrueScopePanel.h"

#include "Capture/TrueScopeCaptureGate.h"
#include "TrueScopeModule.h"
#include "UI/STrueScopeStatusLine.h"
#include "UI/STrueScopeToolbar.h"
#include "UI/STrueScopeWidget.h"
#include "UI/TrueScopeType.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "STrueScopePanel"

void STrueScopePanel::Construct(const FArguments& InArgs)
{
	const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe> Analyzer = FTrueScopeModule::Get().GetAnalyzer();
	CaptureGate = FTrueScopeModule::Get().GetCaptureGate();

	// The scopes are built through the type factory, and the toolbar is handed
	// the widgets themselves: it generates each scope's display-mode control from
	// the scope's own GetDisplayModes(), so scope types and their options stay
	// in one place (architecture guardrail: extensible scope-type enumeration).
	TArray<TPair<ETrueScopeType, TSharedPtr<STrueScopeWidget>>> Scopes;
	auto MakeScope = [&Analyzer, &Scopes](ETrueScopeType Type)
	{
		TSharedPtr<STrueScopeWidget> Scope;
		TSharedRef<SWidget> Widget = TrueScope::MakeScopeWidget(Type, Analyzer, &Scope);
		Scopes.Emplace(Type, Scope);
		return Widget;
	};

	const TSharedRef<SWidget> Waveform = MakeScope(ETrueScopeType::Waveform);
	const TSharedRef<SWidget> Parade = MakeScope(ETrueScopeType::Parade);
	const TSharedRef<SWidget> Vectorscope = MakeScope(ETrueScopeType::Vectorscope);
	const TSharedRef<SWidget> Histogram = MakeScope(ETrueScopeType::Histogram);

	ChildSlot
	[
		SNew(SVerticalBox)

		// Visible home of the user-facing options (phase-6.1 Task 2).
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STrueScopeToolbar, Scopes)
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
					Waveform
				]

				+ SSplitter::Slot()
				.Value(0.5f)
				[
					Parade
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
					Vectorscope
				]

				+ SSplitter::Slot()
				.Value(0.5f)
				[
					Histogram
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

int32 STrueScopePanel::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
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
