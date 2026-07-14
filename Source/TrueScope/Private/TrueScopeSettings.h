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
};
