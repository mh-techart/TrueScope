// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Analysis/TrueScopeAnalysisResult.h"
#include "Analysis/TrueScopeRasterSizes.h"
#include "Containers/ArrayView.h"
#include "CoreMinimal.h"
#include "UI/TrueScopeScopeState.h"
#include "UI/TrueScopeType.h"
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
 * One on/off display option of a scope (phase-6.2) — independent of the
 * radio-style display modes, e.g. the vectorscope's "Show Skin Tone Line".
 * Like the modes, toggles surface in both the right-click menu and the
 * toolbar and are persisted per scope.
 */
struct FTrueScopeToggleOption
{
	/** Stable, scope-local settings key (not localized, never renamed). */
	FName Key;

	FText Label;
	FText Tooltip;
};

/**
 * A secondary radio group of a scope (work order 15) — a mutually exclusive
 * choice that is NOT the scope's display mode, e.g. the vectorscope's marker
 * selection (75 % / 100 % / Both) or, later, the histogram gain. Surfaces in
 * the toolbar and the right-click menu like the display modes; persisted per
 * scope under the group's stable key.
 */
struct FTrueScopeOptionGroup
{
	/** Stable, scope-local settings key (not localized, never renamed). */
	FName Key;

	/** The radio choices: scope-local ids with their labels. */
	TArray<FTrueScopeDisplayMode> Options;
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
 * - Right-click menu: a scope that offers more than one presentation of its
 *   data overrides GetDisplayModes()/GetDisplayModeId()/SetDisplayModeId()
 *   and gets a Display section for free. Every scope's menu also carries the
 *   global Analysis Path section (phase-5.2): GPU (performance) /
 *   CPU (compatibility) — ONE switch selecting the path for all four scopes
 *   at once (a per-scope path would make the user set the same option four
 *   times; simple operation for the artist takes priority).
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

	/**
	 * Whether the cached result carries drawable data for THIS scope — the
	 * exact condition the empty-state hint branches on (phase-5.2 unified
	 * empty-state). Each scope overrides it with its own data check, so no
	 * scope can silently hold a stale trace while another blanks: any scope
	 * without data for the current result says so, identically.
	 */
	virtual bool HasDrawableData() const
	{
		return GetCachedResult().IsValid();
	}

	/**
	 * The hint branch every scope's OnPaint ends in: the paused hint over
	 * held values while the measured viewport is in PIE, else the uniform
	 * "no data" hint when this scope has nothing to draw (phase-5.2), else
	 * nothing. Returns the next free layer.
	 */
	int32 DrawScopeHints(FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FGeometry& AllottedGeometry, const FSlateRect& Content) const;

	/**
	 * Display variants this scope offers; empty (default) means none. Public
	 * since phase-6.1: the tab's toolbar builds a visible control per scope from
	 * these, so the modes are reachable without the right-click menu ("the end
	 * user is an artist"). Menu and toolbar drive the same state, so a scope
	 * that adds a mode gains it in both places at once.
	 */
	virtual TArrayView<const FTrueScopeDisplayMode> GetDisplayModes() const
	{
		return TArrayView<const FTrueScopeDisplayMode>();
	}

	/** Id of the active display mode. */
	virtual int32 GetDisplayModeId() const
	{
		return 0;
	}

	/**
	 * Switch display mode, repaint, and remember the choice across restarts —
	 * the one entry point the right-click menu and the toolbar share.
	 */
	void ApplyDisplayMode(int32 ModeId);

	/**
	 * On/off display options this scope offers (phase-6.2); empty (default)
	 * means none. Surfaced in the toolbar and the right-click menu exactly like
	 * the display modes, so a scope that adds a toggle gains it in both places.
	 */
	virtual TArrayView<const FTrueScopeToggleOption> GetToggleOptions() const
	{
		return TArrayView<const FTrueScopeToggleOption>();
	}

	/** Current value of a toggle option; false for unknown keys. */
	virtual bool GetToggleOptionValue(FName Key) const
	{
		return false;
	}

	/**
	 * Flip a toggle option, repaint, and remember the choice across restarts —
	 * the shared entry point of menu and toolbar (mirror of ApplyDisplayMode).
	 */
	void ApplyToggleOption(FName Key, bool bValue);

	/** Secondary radio groups this scope offers (work order 15); empty default. */
	virtual TArrayView<const FTrueScopeOptionGroup> GetOptionGroups() const
	{
		return TArrayView<const FTrueScopeOptionGroup>();
	}

	/** Selected option id of a group; 0 for unknown keys. */
	virtual int32 GetOptionGroupValue(FName Key) const
	{
		return 0;
	}

	/** Select a group option, repaint, persist — shared by menu and toolbar. */
	void ApplyOptionGroup(FName Key, int32 ValueId);

	/** Which scope this is; the key its persisted options are stored under. */
	ETrueScopeType GetScopeType() const
	{
		return ScopeType;
	}

	/** Test seam: inject a result as if the analyzer had published it. */
	void SetCachedResultForTesting(const TSharedPtr<const FTrueScopeAnalysisResult, ESPMode::ThreadSafe>& InResult)
	{
		CachedResult = InResult;
		OnAnalysisResultChanged();
	}

protected:
	/**
	 * Call from the subclass's Construct. Restores the scope's persisted display
	 * mode (phase-6.1 Task 3), so a scope opens the way the user left it.
	 */
	void InitializeScopeWidget(const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& InAnalyzer,
		ETrueScopeType InScopeType, bool bInMaintainSquareAspect);

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
	 * Publishes, from OnPaint, the rect this scope draws a GPU raster into — in
	 * DEVICE pixels, so the producer can rasterize at display resolution
	 * (phase-6.1 Task 1; FTrueScopeRasterSizes). LocalSize is in the widget's
	 * local space; the geometry's scale carries the editor's DPI/application
	 * scale, which is exactly what Slate's line thickness is expressed in, so
	 * the raster and the CPU vector draw end up in the same units.
	 */
	void RequestRasterSize(TrueScope::ERasterTarget Target, const FGeometry& AllottedGeometry,
		FVector2f LocalSize) const;

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

	/** Switch display mode; ApplyDisplayMode repaints afterwards. */
	virtual void SetDisplayModeId(int32 /*InModeId*/)
	{
	}

	/** Set a toggle option's value; ApplyToggleOption repaints afterwards. */
	virtual void SetToggleOptionValue(FName /*Key*/, bool /*bValue*/)
	{
	}

	/** Set a group's selected option; ApplyOptionGroup repaints afterwards. */
	virtual void SetOptionGroupValue(FName /*Key*/, int32 /*ValueId*/)
	{
	}

private:
	EActiveTimerReturnType PollAnalyzer(double InCurrentTime, float InDeltaTime);

	/**
	 * Every scope has a menu since phase-5.2: the global Analysis Path
	 * section is offered on all four scopes, so the option is discoverable
	 * from any of them (it selects the path for all scopes at once).
	 */
	bool HasContextMenu() const
	{
		return true;
	}

	TSharedRef<SWidget> BuildContextMenu();
	bool IsDisplayModeActive(int32 ModeId) const;
	void HandleFlipToggleOption(FName Key);
	bool IsToggleOptionChecked(FName Key) const;
	void HandleSelectOptionGroup(FName Key, int32 ValueId);
	bool IsOptionGroupValueActive(FName Key, int32 ValueId) const;
	void HandleSetUseGpuAnalysis(bool bUseGpu);
	bool IsAnalysisPathActive(bool bGpu) const;

	TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe> Analyzer;

	/** Where the scopes publish their plot rects for the GPU producer. */
	TSharedPtr<FTrueScopeRasterSizes, ESPMode::ThreadSafe> RasterSizes;

	TSharedPtr<const FTrueScopeAnalysisResult, ESPMode::ThreadSafe> CachedResult;
	uint64 LastSeenSerial = 0;
	ETrueScopeType ScopeType = ETrueScopeType::Waveform;
	bool bMaintainSquareAspect = false;

	/** When the last result arrived; negative until the staleness clock starts. */
	double LastResultTimeSeconds = -1.0;
	TrueScope::EScopeState ScopeState = TrueScope::EScopeState::Live;
};
