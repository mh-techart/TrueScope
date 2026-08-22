// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Analysis/TrueScopeDisplayGains.h"
#include "Containers/ArrayView.h"
#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "TrueScopeSettings.generated.h"

enum class ETrueScopeType : uint8;

namespace TrueScope
{
	/** The fixed 2x2 grid: four panels, indices 0..3 in reading order (M6.4). */
	inline constexpr int32 NumPanels = 4;

	/**
	 * Stable option keys the generic keyed widget API is mapped onto the named
	 * fields of FTrueScopePanelState by. Declared centrally so the mapping in
	 * TrueScopeSettings.cpp and the scope that offers the option cannot drift
	 * apart. Never renamed — they are what the ini is keyed by.
	 */
	inline const FName SkinToneLineOptionKey(TEXT("ShowSkinToneLine"));
	inline const FName VectorscopeMarkersOptionKey(TEXT("Markers"));
}

/**
 * Everything one panel of the 2x2 grid remembers (M6.4 Task 2).
 *
 * Ownership is the panel, not the scope type. Today every type appears exactly
 * once, so the distinction is invisible; once a panel can choose its type, the
 * SAME type may be shown in two panels with different settings, and only
 * per-panel state can express that. Storing the type here too is what makes the
 * later type switcher an addition rather than a rewrite.
 *
 * One struct carries all fields; those irrelevant to the panel's current type
 * are simply unused (a waveform panel ignores VectorscopeMarkers). That also
 * means a panel that is switched to another type and back finds its old
 * settings intact.
 *
 * **These member initializers are the single central source of the defaults**:
 * the panel menu's "Reset to Defaults" (M6.4 Task 6) and the default 2x2 layout
 * both read them, and nothing inlines a default of its own.
 */
USTRUCT()
struct FTrueScopePanelState
{
	GENERATED_BODY()

	/** Which scope this panel shows; TrueScope::GetScopeTypeKey's stable name. */
	UPROPERTY(config)
	FName ScopeType;

	/**
	 * The panel's radio-style display mode, as the scope's own mode enum cast to
	 * int32: waveform RGB/Luma, histogram Separate/YRGB/Overlay — and the
	 * vectorscope's Normal / 2x Zoom, which IS that scope's display mode rather
	 * than a separate option. Every scope's default is enumerator 0.
	 */
	UPROPERTY(config)
	int32 DisplayMode = 0;

	/** The density scopes' Trace Brightness multiplier; 1.0 = calibrated default. */
	UPROPERTY(config)
	float TraceBrightness = TrueScope::DefaultGainMultiplier;

	/** The histogram's Histogram Gain multiplier; 1.0 = calibrated default. */
	UPROPERTY(config)
	float HistogramGain = TrueScope::DefaultGainMultiplier;

	/** Vectorscope target-box set, ETrueScopeVectorscopeMarkers as int32 (0 = 75 %). */
	UPROPERTY(config)
	int32 VectorscopeMarkers = 0;

	/** Vectorscope skin tone line; on by default. */
	UPROPERTY(config)
	bool bShowSkinToneLine = true;
};

/**
 * The user's TrueScope options, persisted across editor restarts (phase-6.1
 * Task 3).
 *
 * config=EditorPerProjectUserSettings is the editor's standard per-user store
 * (the engine's own tool settings use it, e.g. USpriteEditorSettings): the
 * values land in the user's Saved/Config/.../EditorPerProjectUserSettings.ini —
 * never in the level and never in a shared project file, which is the standing
 * zero-level-footprint constraint. A teammate without TrueScope is unaffected,
 * and a teammate with it keeps their own choices.
 *
 * Everything here is set from the TrueScope tab; this class only remembers it.
 *
 * Two levels since M6.4: the plugin-wide options below (analysis path, capture
 * rate) stay global — one switch for all scopes — while everything a single
 * scope displays lives in PanelStates, owned by the panel.
 */
UCLASS(config = EditorPerProjectUserSettings)
class UTrueScopeSettings : public UObject
{
	GENERATED_BODY()

public:
	/** The single per-user settings instance. */
	static UTrueScopeSettings& Get();

	/**
	 * Analysis path: true = GPU (performance), false = CPU (compatibility).
	 * Global on purpose — a per-scope path would make the user set the same
	 * option four times (architecture.md, settled decision).
	 */
	UPROPERTY(config)
	bool bUseGpuAnalysis = false;

	/** Captures per second; global, like the path it feeds. */
	UPROPERTY(config)
	float MeasurementRateHz = 2.5f;

	/**
	 * One entry per panel, indexed by panel (0..NumPanels-1). Kept exactly
	 * NumPanels long by EnsurePanelStates(); an ini written by an older or newer
	 * build is padded or truncated rather than trusted.
	 */
	UPROPERTY(config)
	TArray<FTrueScopePanelState> PanelStates;

	/** Read-only access to a panel's state; index is clamped to the array. */
	const FTrueScopePanelState& GetPanelState(int32 PanelIndex);

	/** The panel's scope type, from its stored type key. */
	ETrueScopeType GetPanelScopeType(int32 PanelIndex);

	int32 GetPanelDisplayMode(int32 PanelIndex);
	void SetPanelDisplayMode(int32 PanelIndex, int32 ModeId);

	/** Toggle options, addressed by their stable key (see TrueScope::*OptionKey). */
	bool GetPanelToggleOption(int32 PanelIndex, FName Key);
	void SetPanelToggleOption(int32 PanelIndex, FName Key, bool bValue);

	/** Secondary radio groups (the vectorscope's markers), by stable key. */
	int32 GetPanelOptionGroupValue(int32 PanelIndex, FName Key);
	void SetPanelOptionGroupValue(int32 PanelIndex, FName Key, int32 ValueId);

	/** Gain multipliers, by gain key (TraceBrightness / HistogramGain). */
	float GetPanelGainMultiplier(int32 PanelIndex, FName GainKey);
	void SetPanelGainMultiplier(int32 PanelIndex, FName GainKey, float Value);

	/**
	 * Back to the compiled defaults for this panel's own display options,
	 * keeping its scope type (M6.4 Task 6). Touches nothing else: no other
	 * panel, not the analysis path, not the capture rate.
	 */
	void ResetPanelToDefaults(int32 PanelIndex);

private:
	/**
	 * Guarantees PanelStates has exactly NumPanels entries, seeding missing ones
	 * with the default 2x2 arrangement. Called by every accessor, so a fresh
	 * install, a truncated ini and a hand-edited one all behave the same.
	 */
	void EnsurePanelStates();
};

namespace TrueScope
{
	/**
	 * The default 2x2 arrangement, in panel-index order: Waveform, Parade (top
	 * row), Vectorscope, Histogram (bottom row) — the order STrueScopeTab has
	 * built since M4. Everything else in the returned states is the struct's own
	 * default.
	 */
	TArray<FTrueScopePanelState> MakeDefaultPanelStates();

	/** The default state of one panel: the defaults above, with this type set. */
	FTrueScopePanelState MakeDefaultPanelState(ETrueScopeType Type);

	/**
	 * The scope type of each panel index in the default arrangement — the one
	 * list both MakeDefaultPanelStates and the unknown-type fallback read, so
	 * the default grid is stated exactly once.
	 */
	TArrayView<const ETrueScopeType> GetDefaultPanelTypes();
}
