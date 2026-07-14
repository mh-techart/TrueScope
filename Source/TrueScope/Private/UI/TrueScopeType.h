// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"

class FTrueScopeAnalyzer;
class STrueScopeWidget;
class SWidget;

/**
 * The scope types TrueScope can display. Extending this enumeration plus the
 * factory switch (MakeScopeWidget) is all it takes to add a scope — the panel
 * layout, result plumbing and space-usage policy stay untouched. All four are
 * implemented as of M4; CIE / overlays later.
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
	/** Display name for a scope type (toolbar labels, later the layout dropdown). */
	FText GetScopeTypeDisplayName(ETrueScopeType Type);

	/**
	 * Stable, non-localized key for a scope type — what persisted settings are
	 * keyed by (phase-6.1 Task 3), so neither a translation nor a new scope type
	 * in the enumeration can move another scope's remembered options.
	 */
	FName GetScopeTypeKey(ETrueScopeType Type);

	/**
	 * Factory: builds the widget for a scope type, or a labelled placeholder
	 * for types not yet implemented. Analysis-agnostic beyond passing the
	 * analyzer the widget reads its results from.
	 *
	 * OutScopeWidget, if given, receives the scope widget itself — the tab's
	 * toolbar (phase-6.1 Task 2) drives the scopes' display modes through it,
	 * and a placeholder type simply leaves it null.
	 */
	TSharedRef<SWidget> MakeScopeWidget(ETrueScopeType Type,
		const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& Analyzer,
		TSharedPtr<STrueScopeWidget>* OutScopeWidget = nullptr);
}
