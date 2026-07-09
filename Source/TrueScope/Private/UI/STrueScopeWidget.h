// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Analysis/TrueScopeAnalysisResult.h"
#include "Containers/ArrayView.h"
#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class FTrueScopeAnalyzer;

/**
 * One selectable display variant of a scope — same measured data, different
 * presentation (backlog: "scope display modes"). M4's first user is the
 * histogram's separate-channels / RGB-overlay pair.
 */
struct FTrueScopeDisplayMode
{
	/** Scope-local identifier; a scope's own display-mode enum cast to int32. */
	int32 Id = 0;

	FText Label;
};

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
 *
 * - Display modes: a scope that offers more than one presentation of its data
 *   overrides GetDisplayModes()/GetDisplayModeId()/SetDisplayModeId() and gets
 *   a right-click menu to switch between them for free. Scopes without modes
 *   have no menu.
 */
class STrueScopeWidget : public SLeafWidget
{
public:
	virtual FVector2D ComputeDesiredSize(float) const override;

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

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

	/** Display variants this scope offers; empty (default) means no menu. */
	virtual TArrayView<const FTrueScopeDisplayMode> GetDisplayModes() const
	{
		return TArrayView<const FTrueScopeDisplayMode>();
	}

	/** Id of the active display mode. */
	virtual int32 GetDisplayModeId() const
	{
		return 0;
	}

	/** Switch display mode; the base class repaints afterwards. */
	virtual void SetDisplayModeId(int32 /*InModeId*/)
	{
	}

private:
	EActiveTimerReturnType PollAnalyzer(double InCurrentTime, float InDeltaTime);

	TSharedRef<SWidget> BuildDisplayModeMenu();
	void HandleSetDisplayMode(int32 ModeId);
	bool IsDisplayModeActive(int32 ModeId) const;

	TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe> Analyzer;
	TSharedPtr<const FTrueScopeAnalysisResult, ESPMode::ThreadSafe> CachedResult;
	uint64 LastSeenSerial = 0;
	bool bMaintainSquareAspect = false;
};
