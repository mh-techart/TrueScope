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
	 * Builds histogram, parade and vectorscope counts from an 8-bit BGRA pixel
	 * array (FColor layout, row-major, tightly packed). Fills Histogram, Parade,
	 * Vectorscope and SourceSize of OutResult; does not touch the derived
	 * intensity images.
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
	 * Builds the display-mapped per-channel parade density from the parade
	 * counts (see FTrueScopeAnalysisResult::ParadeChannelIntensity for the
	 * layout). Intensities are normalized against the maximum possible count
	 * per cell — the source height, i.e. a column with every row at one value —
	 * so brightness is stable over time instead of re-scaling with scene
	 * content.
	 */
	void BuildParadeImage(FTrueScopeAnalysisResult& InOutResult);

	/**
	 * Builds the display-mapped vectorscope density from the vectorscope counts
	 * (see FTrueScopeAnalysisResult::VectorscopeIntensity). Normalized against
	 * the total pixel count — the maximum a single (Cb, Cr) cell can reach, as
	 * a flat neutral frame does — so brightness does not re-scale with content.
	 */
	void BuildVectorscopeImage(FTrueScopeAnalysisResult& InOutResult);

	/**
	 * Waveform texels: the parade's three channel densities superimposed in one
	 * full-width trace instead of side-by-side blocks (Resolve's default
	 * waveform). Texel = (R density, G density, B density), so equal densities
	 * read white and a channel excursion shows in its own color.
	 *
	 * Requires BuildParadeImage to have run. OutTexels must hold exactly
	 * ParadeImageSize.X * ParadeImageSize.Y entries, laid out like
	 * ParadeChannelIntensity. Kept out of FTrueScopeAnalysisResult so the widget
	 * can fill its upload buffer directly.
	 */
	void BuildWaveformTexels(const FTrueScopeAnalysisResult& Result, TArrayView<FColor> OutTexels);
}
