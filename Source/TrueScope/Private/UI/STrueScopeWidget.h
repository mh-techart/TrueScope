// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Analysis/TrueScopeAnalysisResult.h"
#include "Containers/ArrayView.h"
#include "CoreMinimal.h"
#include "UI/TrueScopeScopeState.h"
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
 *
 * - Paused state (M4.1): when the measured viewport enters PIE it drops out as
 *   a source and the scopes hold their last frame — intended, but silent, so
 *   every scope draws a hint over the held values. Subclasses get this for free
 *   by calling DrawPausedHint() at the end of their OnPaint.
 */
class STrueScopeWidget : public SLeafWidget
{
public:
	virtual FVector2D ComputeDesiredSize(float) const override;

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	/** Recompute the scope state from the current inputs; true if it changed. */
	bool UpdateScopeState(double NowSeconds);

	TrueScope::EScopeState GetScopeState() const
	{
		return ScopeState;
	}

	/** The exact condition DrawPausedHint() branches on. */
	bool ShouldDrawPausedHint() const
	{
		return ScopeState == TrueScope::EScopeState::PausedInPie;
	}

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

	/**
	 * Draws the paused hint over the held scope when paused, nothing otherwise.
	 * Call last in OnPaint so it sits above the trace. Returns the next free layer.
	 */
	int32 DrawPausedHint(FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FGeometry& AllottedGeometry, const FSlateRect& Content) const;

	/**
	 * Is a PIE session (not Simulate) running? Virtual so a test can drive the
	 * paused state without an editor play session.
	 */
	virtual bool IsPlayInEditorActive() const;

	/** Test seam: pretend the last result arrived at this time. */
	void SetLastResultTimeSecondsForTesting(double InSeconds)
	{
		LastResultTimeSeconds = InSeconds;
	}

	/** Display variants this scope offers; empty (default) means no menu. */
	virtual TArrayView<const FTrueScopeDisplayMode> GetDisplayModes() const
	{
		return TArrayView<const FTrueScopeDisplayMode>();
	}

	/**
	 * Scope-specific context-menu sections beyond the display modes (M5 first
	 * user: the parade's analysis-path option). Return true from
	 * HasContextMenuExtensions to get the menu even without display modes.
	 */
	virtual void ExtendContextMenu(class FMenuBuilder& MenuBuilder)
	{
	}

	virtual bool HasContextMenuExtensions() const
	{
		return false;
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

	bool HasContextMenu() const
	{
		return !GetDisplayModes().IsEmpty() || HasContextMenuExtensions();
	}

	TSharedRef<SWidget> BuildContextMenu();
	void HandleSetDisplayMode(int32 ModeId);
	bool IsDisplayModeActive(int32 ModeId) const;

	TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe> Analyzer;
	TSharedPtr<const FTrueScopeAnalysisResult, ESPMode::ThreadSafe> CachedResult;
	uint64 LastSeenSerial = 0;
	bool bMaintainSquareAspect = false;

	/** When the last result arrived; negative until the staleness clock starts. */
	double LastResultTimeSeconds = -1.0;
	TrueScope::EScopeState ScopeState = TrueScope::EScopeState::Live;
};
