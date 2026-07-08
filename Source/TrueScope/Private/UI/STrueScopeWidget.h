// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Analysis/TrueScopeAnalysisResult.h"
#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class FTrueScopeAnalyzer;

/**
 * Base class for all scope widgets. Provides:
 *
 * - Result access: polls the analyzer at a fixed UI rate (active timer);
 *   when a new FTrueScopeAnalysisResult has been published, caches it,
 *   notifies the subclass (OnAnalysisResultChanged) and invalidates paint.
 *   Widgets know nothing about how results are produced (CPU today, GPU in
 *   M5).
 *
 * - Space-usage policy (architecture guardrail): responsive scopes fill the
 *   panel; aspect-locked scopes (bMaintainSquareAspect, the M4 vectorscope)
 *   scale uniformly and center within the allotted area. Subclasses draw
 *   inside GetScopeContentRect() and remain policy-agnostic.
 */
class STrueScopeWidget : public SLeafWidget
{
public:
	virtual FVector2D ComputeDesiredSize(float) const override;

protected:
	/** Call from the subclass's Construct. */
	void InitializeScopeWidget(const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& InAnalyzer,
		bool bInMaintainSquareAspect);

	/** Latest cached result; null until the first analysis completes. Game thread. */
	const TSharedPtr<const FTrueScopeAnalysisResult, ESPMode::ThreadSafe>& GetCachedResult() const
	{
		return CachedResult;
	}

	/** New result cached (game thread) — update derived resources (e.g. textures). */
	virtual void OnAnalysisResultChanged()
	{
	}

	/**
	 * Local-space rect the scope graphic may draw into, after applying the
	 * space-usage policy to the allotted geometry.
	 */
	FSlateRect GetScopeContentRect(const FGeometry& AllottedGeometry) const;

private:
	EActiveTimerReturnType PollAnalyzer(double InCurrentTime, float InDeltaTime);

	TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe> Analyzer;
	TSharedPtr<const FTrueScopeAnalysisResult, ESPMode::ThreadSafe> CachedResult;
	uint64 LastSeenSerial = 0;
	bool bMaintainSquareAspect = false;
};
