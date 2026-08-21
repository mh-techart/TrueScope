// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Analysis/TrueScopeAnalysisResult.h"
#include "Analysis/TrueScopeDisplayGains.h"
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
 * Like the modes, toggles surface in both the panel menu and the
 * right-click menu, and are persisted per panel.
 */
struct FTrueScopeToggleOption
{
	/** Stable, scope-local settings key (not localized, never renamed). */
	FName Key;

	FText Label;
	FText Tooltip;

	/**
	 * Which option group's menu section this toggle joins, by that group's key;
	 * NAME_None puts it with the display modes (M6.4 Task 4). The vectorscope's
	 * skin tone line belongs to "Markers" — it is one of the things drawn over
	 * the trace, not a separate topic — and the menu says so by placing it as
	 * the last entry of that section.
	 */
	FName SectionKey;
};

/**
 * A secondary radio group of a scope (work order 15) — a mutually exclusive
 * choice that is NOT the scope's display mode, e.g. the vectorscope's marker
 * selection (75 % / 100 % / Both) or, later, the histogram gain. Surfaces in
 * the panel menu and the right-click menu like the display modes; persisted per
 * panel under the group's stable key.
 */
struct FTrueScopeOptionGroup
{
	/** Stable, scope-local settings key (not localized, never renamed). */
	FName Key;

	/** Heading of the group's own section in the panel menu (M6.4). */
	FText SectionLabel;

	/** The radio choices: scope-local ids with their labels. */
	TArray<FTrueScopeDisplayMode> Options;
};

/**
 * A scope's gain slider (M6.3) — the density scopes' "Trace Brightness" and
 * the histogram's "Histogram Gain". A scope has at most one; scopes that
 * declare none simply have no gain control.
 *
 * Both follow the shared logarithmic law (TrueScopeDisplayGains.h): equal
 * travel means equal multiplication, +/-3 doublings, 1.0x exactly at centre,
 * continuous rather than detented, centre marked and snapping, double-click
 * resets, and NO numeric readout. The missing readout is deliberate (human
 * decision): a displayed "1.0x" reads as the officially correct setting, and
 * less experienced artists then feel obliged to leave it alone instead of
 * adjusting it to what they need to see.
 */
struct FTrueScopeGainOption
{
	/** Stable, scope-local settings key (not localized, never renamed). */
	FName Key;

	FText Label;
	FText Tooltip;
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
 * - Panel menu (M6.4): a scope that offers more than one presentation of its
 *   data overrides GetDisplayModes()/GetDisplayModeId()/SetDisplayModeId()
 *   and gets a Display section for free; option groups, toggles and the gain
 *   slider come the same way. BuildPanelMenu() is the single builder behind
 *   both the panel header's menu button and the canvas's right-click menu.
 *   The global options — analysis path, capture rate — are NOT here: they are
 *   TrueScope-wide and live in the main bar's settings popup, so each is
 *   reachable from exactly one place.
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
	 * since phase-6.1: the panel menu builds a visible control per scope from
	 * these, so the modes are reachable one click from the scope they change
	 * ("the end user is an artist"). Panel menu and right-click menu are the
	 * same built menu, so a scope that adds a mode gains it in both at once.
	 */
	virtual TArrayView<const FTrueScopeDisplayMode> GetDisplayModes() const
	{
		return TArrayView<const FTrueScopeDisplayMode>();
	}

	/**
	 * Heading the display modes appear under in the panel menu. "Display" for
	 * every scope whose modes are ways of showing the same data; the vectorscope
	 * overrides it with "Zoom", because Normal / 2x Zoom is a magnification, and
	 * calling that "Display" would leave the user looking for a zoom control
	 * that is already on screen.
	 */
	virtual FText GetDisplayModeSectionLabel() const;

	/** Id of the active display mode. */
	virtual int32 GetDisplayModeId() const
	{
		return 0;
	}

	/**
	 * Switch display mode, repaint, and remember the choice across restarts —
	 * the one entry point the panel menu and the right-click menu share.
	 */
	void ApplyDisplayMode(int32 ModeId);

	/**
	 * On/off display options this scope offers (phase-6.2); empty (default)
	 * means none. Surfaced in the panel menu and the right-click menu exactly like
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
	 * the shared entry point of both menus (mirror of ApplyDisplayMode).
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

	/** Select a group option, repaint, persist — shared by both menus. */
	void ApplyOptionGroup(FName Key, int32 ValueId);

	/**
	 * This scope's gain slider, or null if it has none (M6.3). See
	 * FTrueScopeGainOption; the value itself is held by the base class, so
	 * every scope persists and publishes it the same way.
	 */
	virtual const FTrueScopeGainOption* GetGainOption() const
	{
		return nullptr;
	}

	/**
	 * The user's gain MULTIPLIER for this scope: 1.0 = the scope's calibrated
	 * default, which is what the slider's centre stands for. The absolute gain
	 * the transfer applies is this times that default
	 * (FTrueScopeDisplayGains::GetEffectiveDensityGain), so recalibrating a
	 * default never moves an existing user's stored setting.
	 */
	float GetGainMultiplier() const
	{
		return GainMultiplier;
	}

	/** Set the gain, persist it, publish it to both analysis paths, repaint. */
	void ApplyGainMultiplier(float Multiplier);

	/** Double-click behaviour: back to centre (1.0x). */
	void ResetGainMultiplier()
	{
		ApplyGainMultiplier(TrueScope::DefaultGainMultiplier);
	}

	/** Which scope this is. */
	ETrueScopeType GetScopeType() const
	{
		return ScopeType;
	}

	/**
	 * Which panel of the 2x2 grid this scope sits in — the key its persisted
	 * options are stored under since M6.4. Settings are owned by the panel, not
	 * by the scope type, so two panels showing the same type can differ.
	 */
	int32 GetPanelIndex() const
	{
		return PanelIndex;
	}

	/**
	 * The panel menu of the panel this scope sits in (M6.4 Task 4) — the ONE
	 * builder behind both entry points: the panel header's menu button and the
	 * scope canvas's right-click menu. They cannot offer different options
	 * because there is nothing else to build them from. The right-click menu is
	 * a temporary second entry point and goes away with the type switcher.
	 */
	TSharedRef<SWidget> BuildPanelMenu();

	/** Test seam: inject a result as if the analyzer had published it. */
	void SetCachedResultForTesting(const TSharedPtr<const FTrueScopeAnalysisResult, ESPMode::ThreadSafe>& InResult)
	{
		CachedResult = InResult;
		OnAnalysisResultChanged();
	}

protected:
	/**
	 * Call from the subclass's Construct. Restores the persisted options of the
	 * PANEL this scope sits in (phase-6.1 Task 3, re-keyed to the panel in
	 * M6.4), so a scope opens the way the user left that panel.
	 */
	void InitializeScopeWidget(const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& InAnalyzer,
		ETrueScopeType InScopeType, bool bInMaintainSquareAspect, int32 InPanelIndex);

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
	 * Every scope has a menu: the panel menu, reachable from the panel header
	 * and — until the type switcher lands — from a right-click on the canvas.
	 */
	bool HasContextMenu() const
	{
		return true;
	}

	/** The toggle options belonging to one menu section; NAME_None = the display section. */
	void AddToggleOptionEntries(class FMenuBuilder& MenuBuilder, FName SectionKey);

	/** The gain slider row of the panel menu, in the M6.3 slider law's terms. */
	TSharedRef<SWidget> BuildGainSlider(const FTrueScopeGainOption& Option);

	bool IsDisplayModeActive(int32 ModeId) const;
	void HandleFlipToggleOption(FName Key);
	bool IsToggleOptionChecked(FName Key) const;
	void HandleSelectOptionGroup(FName Key, int32 ValueId);
	bool IsOptionGroupValueActive(FName Key, int32 ValueId) const;

	TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe> Analyzer;

	/** Where the scopes publish their plot rects for the GPU producer. */
	TSharedPtr<FTrueScopeRasterSizes, ESPMode::ThreadSafe> RasterSizes;

	/**
	 * Where the scopes publish their gain multipliers (M6.3), read by BOTH the
	 * CPU analysis task and the GPU producer — the same widget -> both-paths
	 * direction as RasterSizes.
	 */
	TSharedPtr<FTrueScopeDisplayGains, ESPMode::ThreadSafe> DisplayGains;

	TSharedPtr<const FTrueScopeAnalysisResult, ESPMode::ThreadSafe> CachedResult;
	uint64 LastSeenSerial = 0;
	ETrueScopeType ScopeType = ETrueScopeType::Waveform;

	/** Panel slot this scope occupies; what its settings are keyed by (M6.4). */
	int32 PanelIndex = 0;

	bool bMaintainSquareAspect = false;

	/** The user's gain multiplier; 1.0 = this scope's calibrated default. */
	float GainMultiplier = TrueScope::DefaultGainMultiplier;

	/** Publishes GainMultiplier to the analysis paths under this scope's slot. */
	void PublishGainMultiplier() const;

	/** When the last result arrived; negative until the staleness clock starts. */
	double LastResultTimeSeconds = -1.0;
	TrueScope::EScopeState ScopeState = TrueScope::EScopeState::Live;
};
