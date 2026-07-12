// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "UI/STrueScopePanel.h"

#include "Capture/TrueScopeCaptureGate.h"
#include "TrueScopeModule.h"
#include "UI/TrueScopeType.h"
#include "Widgets/Layout/SSplitter.h"

#define LOCTEXT_NAMESPACE "STrueScopePanel"

void STrueScopePanel::Construct(const FArguments& InArgs)
{
	const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe> Analyzer = FTrueScopeModule::Get().GetAnalyzer();
	CaptureGate = FTrueScopeModule::Get().GetCaptureGate();

	ChildSlot
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
				TrueScope::MakeScopeWidget(ETrueScopeType::Waveform, Analyzer)
			]

			+ SSplitter::Slot()
			.Value(0.5f)
			[
				TrueScope::MakeScopeWidget(ETrueScopeType::Parade, Analyzer)
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
				TrueScope::MakeScopeWidget(ETrueScopeType::Vectorscope, Analyzer)
			]

			+ SSplitter::Slot()
			.Value(0.5f)
			[
				TrueScope::MakeScopeWidget(ETrueScopeType::Histogram, Analyzer)
			]
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
