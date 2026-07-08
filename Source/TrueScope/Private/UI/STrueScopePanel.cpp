// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "UI/STrueScopePanel.h"

#include "TrueScopeModule.h"
#include "UI/TrueScopeType.h"
#include "Widgets/Layout/SSplitter.h"

#define LOCTEXT_NAMESPACE "STrueScopePanel"

void STrueScopePanel::Construct(const FArguments& InArgs)
{
	const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe> Analyzer = FTrueScopeModule::Get().GetAnalyzer();

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

#undef LOCTEXT_NAMESPACE
