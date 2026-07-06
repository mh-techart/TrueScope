// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "UI/STrueScopePanel.h"

#include "Styling/AppStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "STrueScopePanel"

void STrueScopePanel::Construct(const FArguments& InArgs)
{
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
				MakePlaceholderPanel(LOCTEXT("WaveformLabel", "Waveform"))
			]

			+ SSplitter::Slot()
			.Value(0.5f)
			[
				MakePlaceholderPanel(LOCTEXT("ParadeLabel", "Parade"))
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
				MakePlaceholderPanel(LOCTEXT("VectorscopeLabel", "Vectorscope"))
			]

			+ SSplitter::Slot()
			.Value(0.5f)
			[
				MakePlaceholderPanel(LOCTEXT("HistogramLabel", "Histogram"))
			]
		]
	];
}

TSharedRef<SWidget> STrueScopePanel::MakePlaceholderPanel(const FText& ScopeName)
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(ScopeName)
		];
}

#undef LOCTEXT_NAMESPACE
