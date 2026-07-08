// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Analysis/TrueScopeAnalysisResult.h"
#include "CoreMinimal.h"

/**
 * Pure CPU analysis functions — no rendering, no threading, no state — so the
 * automation tests can feed synthetic pixel arrays directly and the results
 * serve as ground truth for the M5 GPU path.
 */
namespace TrueScope
{
	/**
	 * Builds histogram and parade counts from an 8-bit BGRA pixel array
	 * (FColor layout, row-major, tightly packed). Fills Histogram, Parade and
	 * SourceSize of OutResult; does not touch the derived ParadeImage.
	 */
	void AnalyzePixels(TArrayView<const FColor> Pixels, int32 Width, int32 Height, FTrueScopeAnalysisResult& OutResult);

	/**
	 * Log intensity mapping of a bin count to display brightness in [0,1]:
	 * log(1 + Gain*Count) / log(1 + Gain*MaxCount). Density accumulation
	 * instead of binary on/off pixels is the core of the smooth scope look
	 * (final tuning M4/M6).
	 */
	float CountToIntensity(uint32 Count, uint32 MaxCount);

	/**
	 * Builds the display-mapped parade density image from the parade counts
	 * (see FTrueScopeAnalysisResult::ParadeImage for the layout). Intensities
	 * are normalized against the maximum possible count per cell — the image
	 * height, i.e. a column with every row at one value — so brightness is
	 * stable over time instead of re-scaling with scene content.
	 */
	void BuildParadeImage(FTrueScopeAnalysisResult& InOutResult);
}
