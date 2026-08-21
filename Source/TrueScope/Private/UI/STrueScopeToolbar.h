// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"


/**
 * The TrueScope tab's main bar (phase-6.1 Task 2, reorganised in M6.4): the row
 * above all panels, carrying what is TrueScope-wide.
 *
 * "The end user is an artist, not an engine programmer" (architecture.md): an
 * option that exists only in a Slate menu is, for practical purposes, hidden.
 * M6.4 answers that per level of ownership rather than by showing everything at
 * once: a panel's own options live in its panel header, one click from the scope
 * they change, while the two global options — analysis path and capture rate —
 * live here. Nothing that belongs to a single panel is on this bar any more; a
 * bar carrying four scopes' worth of controls had become the reason a panel's
 * own settings were hard to find.
 */
class STrueScopeToolbar : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STrueScopeToolbar)
		{
		}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	/** One labelled group of mutually exclusive toggle buttons. */
	TSharedRef<SWidget> MakeGroup(const FText& Label, TSharedRef<SWidget> Content);

	/** Analysis path: one control for all four scopes (the decided global behaviour). */
	TSharedRef<SWidget> MakeAnalysisPathControl();

	/** Measurement rate: the shortlist of cadences from TrueScopeViewExtension.h. */
	TSharedRef<SWidget> MakeMeasurementRateControl();

	/** Toggle button whose checked state mirrors the option it sets. */
	TSharedRef<SWidget> MakeOptionButton(const FText& Label, const FText& Tooltip,
		FSimpleDelegate OnSelected, TAttribute<bool> IsSelected);

	void HandleSetAnalysisPath(bool bUseGpu);
	bool IsAnalysisPathSelected(bool bGpu) const;

	void HandleSetMeasurementRate(float RateHz);
	bool IsMeasurementRateSelected(float RateHz) const;
};
