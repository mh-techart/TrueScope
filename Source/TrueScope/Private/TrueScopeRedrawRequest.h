// Copyright 2026 Michael Herber
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"

#include <atomic>

namespace TrueScope
{
	/**
	 * What a redraw producer did when asked to re-render the scopes from the
	 * frame it already holds (M6.4.1).
	 */
	enum class ERedrawOutcome : uint8
	{
		/** A re-render was started; the request is satisfied. */
		Started,

		/**
		 * The producer is busy with a result of its own this frame. The request
		 * stays pending and is retried on the next frame, so the last value of
		 * a drag can never be the one that is dropped.
		 */
		Busy,

		/**
		 * There is nothing to re-render — no frame has been measured yet, or the
		 * producer asked is not the one that produced the last result. The
		 * request is moot and is dropped: whatever arrives next already carries
		 * the current gains.
		 */
		NoData,
	};

	/**
	 * Coalescing latch between the gain sliders and the analysis producers
	 * (M6.4.1).
	 *
	 * A slider drag raises one request per mouse move — potentially several per
	 * rendered frame, and at frame rate for a fast drag — while a re-render
	 * costs a full analysis of the resident frame. The latch collapses any
	 * number of requests into a single pending bit, which the module's per-frame
	 * ticker consumes: **at most one re-render per rendered frame**, and always
	 * with the newest gain, because the producers read the gains when they run
	 * rather than carrying a value from the request.
	 *
	 * Deliberately a bit rather than a queue: a superseded gain has no value
	 * whatsoever — nobody wants to see the brightness the slider passed through
	 * two frames ago — so dropping intermediate requests is the correct
	 * behaviour, not a compromise.
	 */
	class FRedrawRequest
	{
	public:
		/** Ask for a re-render. Any number of calls collapse into one. */
		void Request()
		{
			bPending.store(true, std::memory_order_release);
		}

		/** Whether a request is waiting to be serviced. */
		bool IsPending() const
		{
			return bPending.load(std::memory_order_acquire);
		}

		/**
		 * Takes the pending request, if any: true exactly once per burst of
		 * Request calls. The caller then performs one re-render.
		 */
		bool Consume()
		{
			return bPending.exchange(false, std::memory_order_acq_rel);
		}

	private:
		std::atomic<bool> bPending{false};
	};
}
