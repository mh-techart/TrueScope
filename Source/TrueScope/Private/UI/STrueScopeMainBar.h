// Copyright 2026 Michael Herber
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/**
 * The TrueScope tab's main bar (phase-6.1 Task 2, reorganised in M6.4): the row
 * above all panels, carrying what is TrueScope-wide and nothing else.
 *
 * "The end user is an artist, not an engine programmer" (architecture.md): an
 * option that exists only in a Slate menu is, for practical purposes, hidden.
 * M6.4 answers that by level of ownership rather than by showing everything at
 * once — a panel's options sit in its own panel header, one click from the scope
 * they change, and the two options that are not any panel's (analysis path,
 * capture rate) sit here behind the gear. A bar carrying four scopes' worth of
 * controls was itself the reason a panel's own settings were hard to find.
 *
 * Each of the two global options is now reachable from exactly one place in the
 * UI. The diagnostic console variable still overrides the analysis path, and the
 * popup shows the EFFECTIVE path, so the two cannot appear to disagree.
 */
class STrueScopeMainBar : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STrueScopeMainBar)
		{
		}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** The settings popup: the TrueScope-wide options. Public so a test can read its rows. */
	TSharedRef<SWidget> BuildSettingsMenu();

	/**
	 * Which analysis-path entry shows as selected. Public so a test can pin that
	 * the popup follows the EFFECTIVE path (i.e. the diagnostic CVar override),
	 * rather than the remembered UI choice the override is currently outvoting.
	 */
	bool IsAnalysisPathSelected(bool bGpu) const;

private:
	void HandleSetAnalysisPath(bool bUseGpu);

	void HandleSetMeasurementRate(float RateHz);
	bool IsMeasurementRateSelected(float RateHz) const;
};
