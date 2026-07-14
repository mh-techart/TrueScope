// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "UI/TrueScopeType.h"
#include "Widgets/SCompoundWidget.h"

class STrueScopeWidget;

/**
 * The TrueScope tab's toolbar (phase-6.1 Task 2): the visible home of every
 * user-facing option.
 *
 * "The end user is an artist, not an engine programmer" (architecture.md): an
 * option that exists only in a Slate right-click menu is, for practical
 * purposes, hidden — so the analysis path, the per-scope display modes and the
 * measurement rate are all operable from here. The right-click menus stay: they
 * are the fast path for someone already pointing at a scope, and both routes
 * drive exactly the same state (the scope widgets' display modes and the
 * module's global analysis path), so they cannot disagree.
 *
 * The display-mode controls are generated from each scope's own
 * GetDisplayModes(), so a scope that gains a mode gains its toolbar control
 * with no change here.
 */
class STrueScopeToolbar : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STrueScopeToolbar)
		{
		}

	SLATE_END_ARGS()

	/** Scopes shown in the tab, in layout order; entries may be null (placeholders). */
	void Construct(const FArguments& InArgs,
		const TArray<TPair<ETrueScopeType, TSharedPtr<STrueScopeWidget>>>& InScopes);

private:
	/** One labelled group of mutually exclusive toggle buttons. */
	TSharedRef<SWidget> MakeGroup(const FText& Label, TSharedRef<SWidget> Content);

	/** Analysis path: one control for all four scopes (the decided global behaviour). */
	TSharedRef<SWidget> MakeAnalysisPathControl();

	/** A scope's display modes, built from the scope itself. */
	TSharedRef<SWidget> MakeDisplayModeControl(ETrueScopeType Type, const TSharedPtr<STrueScopeWidget>& Scope);

	/** Measurement rate: the shortlist of cadences from TrueScopeViewExtension.h. */
	TSharedRef<SWidget> MakeMeasurementRateControl();

	/** Toggle button whose checked state mirrors the option it sets. */
	TSharedRef<SWidget> MakeOptionButton(const FText& Label, const FText& Tooltip,
		FSimpleDelegate OnSelected, TAttribute<bool> IsSelected);

	void HandleSetAnalysisPath(bool bUseGpu);
	bool IsAnalysisPathSelected(bool bGpu) const;

	void HandleSetDisplayMode(TWeakPtr<STrueScopeWidget> Scope, int32 ModeId);
	bool IsDisplayModeSelected(TWeakPtr<STrueScopeWidget> Scope, int32 ModeId) const;

	void HandleSetMeasurementRate(float RateHz);
	bool IsMeasurementRateSelected(float RateHz) const;
};
