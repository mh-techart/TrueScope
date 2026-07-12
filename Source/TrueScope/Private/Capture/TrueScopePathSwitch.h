// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"

namespace TrueScope
{
	/** What a path switch must do to take effect promptly (phase-5.1 Task 1). */
	enum class EPathSwitchAction : uint8
	{
		/** No effective change, or no captured frame to re-analyse (cold start). */
		None,

		/** CPU->GPU: run the GPU compute passes on the resident capture RT. */
		ReanalyzeGpu,

		/**
		 * GPU->CPU: seed the CPU path with a one-time readback of the resident
		 * capture RT — the sole, documented exception to the no-readback rule of
		 * the GPU path; it occurs only at the manual switch, never in steady
		 * state.
		 */
		SeedCpuReadback,
	};

	/**
	 * Decision core of the switch-responsiveness fix (approach A: re-analyse the
	 * already-captured last frame; no redraw, no new capture, no viewport
	 * change). Pure, so the regression test can pin the switch/state layer:
	 * without the re-analysis, a switch only takes effect on the next capture —
	 * which never arrives while the viewport is not Realtime.
	 */
	inline EPathSwitchAction DecidePathSwitchAction(bool bOldGpu, bool bNewGpu, bool bHasCapturedFrame)
	{
		// Cold-start guard: before the first capture the RT holds only its
		// initial clear — there is no last frame to re-analyse, and analysing
		// the clear color would fabricate a measurement.
		if (bOldGpu == bNewGpu || !bHasCapturedFrame)
		{
			return EPathSwitchAction::None;
		}
		return bNewGpu ? EPathSwitchAction::ReanalyzeGpu : EPathSwitchAction::SeedCpuReadback;
	}
}
