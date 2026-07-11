// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"

namespace TrueScope
{
	/** What a scope widget is currently showing (game thread). */
	enum class EScopeState : uint8
	{
		/** Results are arriving, or nothing is being measured for a non-PIE reason. */
		Live,

		/**
		 * A play session is running and the measured viewport has stopped
		 * delivering: the scopes hold their last frame (architecture.md,
		 * "Behaviour during PIE"). The widgets draw the paused hint over it.
		 */
		PausedInPie,
	};

	/**
	 * A stalled capture only counts as paused after this many capture intervals,
	 * so the normal ~2.5 Hz cadence (and the hitch while PIE starts up) never
	 * flickers the hint. Expressed in intervals so it scales with the capture
	 * rate rather than pinning a wall-clock number.
	 */
	inline constexpr int32 PausedAfterMissedIntervals = 3;

	/**
	 * The paused decision, as a pure function of its two inputs.
	 *
	 * Paused requires **both** that a PIE session is running **and** that results
	 * have stopped arriving. The conjunction is what keeps the hint honest:
	 *
	 * - PIE alone is too broad. Starting PIE disables realtime on *all* editor
	 *   viewports (engine: `PlayLevel.cpp`, "Can't allow realtime viewports
	 *   whilst in PIE so disable it for ALL viewports here"), but a viewport the
	 *   user focuses keeps delivering — and while results keep arriving the
	 *   scopes are live, so no hint may appear.
	 * - A stall alone is too broad the other way: with no PIE running, a stalled
	 *   capture is the ordinary "no viewport focused" idle state, which keeps its
	 *   existing (silent) presentation.
	 *
	 * Simulate is deliberately not a paused state: it drives runtime objects while
	 * the scopes keep measuring (architecture.md), so the caller reports it as
	 * "not PIE".
	 */
	EScopeState DetermineScopeState(bool bPlayInEditorActive, double SecondsSinceLastResult,
		double CaptureIntervalSeconds);
}
