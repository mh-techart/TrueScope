// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "TrueScopeSettings.generated.h"

enum class ETrueScopeType : uint8;

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
 */
UCLASS(config = EditorPerProjectUserSettings)
class UTrueScopeSettings : public UObject
{
	GENERATED_BODY()

public:
	/** The persisted settings object. */
	static UTrueScopeSettings& Get();

	/** Analysis path: true = GPU (performance), false = CPU (compatibility). */
	UPROPERTY(config)
	bool bUseGpuAnalysis = false;

	/** Captures per second (TrueScope::MeasurementRatesHz). */
	UPROPERTY(config)
	float MeasurementRateHz = 2.5f;

	/**
	 * Per-scope display mode, keyed by the scope type's stable key rather than
	 * by index — a scope added to the enumeration must not shift another scope's
	 * remembered mode.
	 */
	UPROPERTY(config)
	TMap<FName, int32> ScopeDisplayModes;

	/** The mode remembered for a scope, or INDEX_NONE if it has none yet. */
	int32 GetScopeDisplayMode(ETrueScopeType Type) const;

	/** Remembers a scope's display mode and writes the config out. */
	void SetScopeDisplayMode(ETrueScopeType Type, int32 ModeId);

	/**
	 * Per-scope on/off display options (phase-6.2, e.g. the vectorscope's Show
	 * Guides), keyed "<scope key>.<option key>" — same stable-key rationale as
	 * ScopeDisplayModes.
	 */
	UPROPERTY(config)
	TMap<FName, bool> ScopeToggleOptions;

	/** True and fills OutValue if a value is remembered for the toggle. */
	bool GetScopeToggleOption(ETrueScopeType Type, FName Key, bool& OutValue) const;

	/** Remembers a scope toggle's value and writes the config out. */
	void SetScopeToggleOption(ETrueScopeType Type, FName Key, bool bValue);

	/**
	 * Per-scope secondary radio groups (work order 15, e.g. the vectorscope's
	 * marker selection), keyed "<scope key>.<group key>" like the toggles.
	 */
	UPROPERTY(config)
	TMap<FName, int32> ScopeOptionGroupValues;

	/** True and fills OutValue if a selection is remembered for the group. */
	bool GetScopeOptionGroupValue(ETrueScopeType Type, FName Key, int32& OutValue) const;

	/** Remembers a group's selection and writes the config out. */
	void SetScopeOptionGroupValue(ETrueScopeType Type, FName Key, int32 ValueId);

	/**
	 * Per-scope gain multipliers (M6.3: the density scopes' Trace Brightness
	 * and the histogram's Histogram Gain), keyed "<scope key>.<gain key>" like
	 * the toggles.
	 *
	 * What is stored is the MULTIPLIER, where 1.0 means "this scope's
	 * calibrated default" — never the slider position and never a derived
	 * percentage. That is deliberate: recalibrating a scope's default gain, or
	 * changing the slider's +/-3-doubling range, must not silently move every
	 * existing user's setting to a different picture.
	 */
	UPROPERTY(config)
	TMap<FName, float> ScopeGainMultipliers;

	/** True and fills OutValue if a multiplier is remembered for the gain. */
	bool GetScopeGainMultiplier(ETrueScopeType Type, FName Key, float& OutValue) const;

	/** Remembers a gain multiplier and writes the config out. */
	void SetScopeGainMultiplier(ETrueScopeType Type, FName Key, float Value);
};
