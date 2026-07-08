// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"

class FTrueScopeAnalyzer;
class SWidget;

/**
 * The scope types TrueScope can display. Extending this enumeration plus the
 * factory switch (MakeScopeWidget) is all it takes to add a scope — the panel
 * layout, result plumbing and space-usage policy stay untouched. Waveform and
 * Vectorscope arrive in M4; CIE / overlays later.
 */
enum class ETrueScopeType : uint8
{
	Waveform,
	Parade,
	Vectorscope,
	Histogram,
};

namespace TrueScope
{
	/** Display name for a scope type (tab labels, later the M6 dropdown). */
	FText GetScopeTypeDisplayName(ETrueScopeType Type);

	/**
	 * Factory: builds the widget for a scope type, or a labelled placeholder
	 * for types not yet implemented in the current phase (Waveform,
	 * Vectorscope in M3). Analysis-agnostic beyond passing the analyzer the
	 * widget reads its results from.
	 */
	TSharedRef<SWidget> MakeScopeWidget(ETrueScopeType Type,
		const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& Analyzer);
}
