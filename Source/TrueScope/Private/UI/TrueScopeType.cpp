// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "UI/TrueScopeType.h"

#include "Styling/AppStyle.h"
#include "UI/STrueScopeHistogram.h"
#include "UI/STrueScopeParade.h"
#include "UI/STrueScopeVectorscope.h"
#include "UI/STrueScopeWaveform.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "TrueScopeType"

namespace TrueScope
{

FText GetScopeTypeDisplayName(ETrueScopeType Type)
{
	switch (Type)
	{
	case ETrueScopeType::Waveform:    return LOCTEXT("Waveform", "Waveform");
	case ETrueScopeType::Parade:      return LOCTEXT("Parade", "Parade");
	case ETrueScopeType::Vectorscope: return LOCTEXT("Vectorscope", "Vectorscope");
	case ETrueScopeType::Histogram:   return LOCTEXT("Histogram", "Histogram");
	default:                          return LOCTEXT("Unknown", "Scope");
	}
}

/** Labelled placeholder for scope types not yet implemented this phase. */
static TSharedRef<SWidget> MakePlaceholder(ETrueScopeType Type)
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(GetScopeTypeDisplayName(Type))
		];
}

TSharedRef<SWidget> MakeScopeWidget(ETrueScopeType Type,
	const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& Analyzer)
{
	switch (Type)
	{
	case ETrueScopeType::Waveform:
		return SNew(STrueScopeWaveform, Analyzer);
	case ETrueScopeType::Parade:
		return SNew(STrueScopeParade, Analyzer);
	case ETrueScopeType::Vectorscope:
		return SNew(STrueScopeVectorscope, Analyzer);
	case ETrueScopeType::Histogram:
		return SNew(STrueScopeHistogram, Analyzer);
	default:
		// A scope type added to the enumeration but not yet built (CIE, M8
		// overlays): the panel keeps working, the gap is visible.
		return MakePlaceholder(Type);
	}
}

} // namespace TrueScope

#undef LOCTEXT_NAMESPACE
