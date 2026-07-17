// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Internationalization/Text.h"

namespace TrueScope
{
	/**
	 * What TrueScope is doing right now — the states the status line names
	 * (phase-6.1 Task 4). The three not-measuring states are the ones users
	 * previously had to guess at: the scopes simply stood still and said
	 * nothing, or said "no data" without saying why.
	 */
	enum class EMeasurementState : uint8
	{
		/** A viewport is being measured and results are arriving. */
		Measuring,

		/** No Level Editor viewport is focused — the existing empty state, now named. */
		NoViewport,

		/**
		 * The measured viewport is orthographic. TrueScope's capture hook is
		 * after the tonemap pass, which the ortho path does not run, so nothing
		 * is ever captured (found in M4.1, filed in backlog.md). The scopes are
		 * not broken — they are unfed, and the user is told so.
		 */
		Orthographic,

		/**
		 * A play session runs and the measured viewport dropped out as a source,
		 * so the scopes hold their last frame (architecture.md, "Behaviour during
		 * PIE"). Matches the in-panel paused hint from phase-4.1.
		 */
		PausedInPie,
	};

	/** Everything the status line reports, gathered on the game thread. */
	struct FMeasurementStatus
	{
		EMeasurementState State = EMeasurementState::NoViewport;

		/** The measured viewport: its view type, or the actor it is piloting. */
		FText Source;

		/** Display encoding of the measured signal, e.g. "sRGB / Rec.709". */
		FText DisplayEncoding;

		/** Analysis path actually running (after the diagnostic CVar override). */
		bool bUseGpu = false;
	};

	/** The display encoding V1 measures and calibrates for (architecture.md). */
	FText GetDefaultDisplayEncoding();

	/**
	 * The status line's text, as a pure function of the status — so what the
	 * user is told is unit-testable without an editor, and every state is
	 * guaranteed to say something.
	 */
	FText FormatStatusLine(const FMeasurementStatus& Status);

	/** True while TrueScope is measuring; the status line dims itself then. */
	inline bool IsMeasuring(const FMeasurementStatus& Status)
	{
		return Status.State == EMeasurementState::Measuring;
	}

	/**
	 * Reads the current status from the editor: the same viewport the capture
	 * extension selects (the last focused Level Editor viewport), its view type
	 * and actor lock, the effective analysis path. Game thread.
	 */
	FMeasurementStatus GatherMeasurementStatus(bool bPlayInEditorActive, bool bResultsFlowing);
}
