// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Analysis/TrueScopeAnalysisResult.h"
#include "Analysis/TrueScopeDisplayGains.h"
#include "CoreMinimal.h"

/**
 * Pure CPU analysis functions — no rendering, no threading, no state — so the
 * automation tests can feed synthetic pixel arrays directly and the results
 * serve as ground truth for the M5 GPU path.
 */
namespace TrueScope
{
	/**
	 * The density scopes' per-scope Trace Brightness gains, as published by the
	 * widgets (TrueScopeDisplayGains.h). Passed explicitly into every mapping
	 * call rather than read from a global, so the automation tests can pin the
	 * transfer at a chosen gain and the GPU pass provably applies the same one.
	 */
	struct FDensityGains
	{
		/** Effective gain (scope default x user multiplier) per density scope. */
		float Parade = GetDefaultDensityGain(EDensityScope::Parade);
		float Waveform = GetDefaultDensityGain(EDensityScope::Waveform);
		float Vectorscope = GetDefaultDensityGain(EDensityScope::Vectorscope);

		/** The gains the three scopes carry with no user adjustment. */
		static FDensityGains Defaults() { return FDensityGains(); }
	};

	/**
	 * Builds histogram, parade, luma and vectorscope counts from an 8-bit BGRA
	 * pixel array (FColor layout, row-major, tightly packed). Fills Histogram,
	 * Parade, LumaParade, Vectorscope and SourceSize of OutResult; does not
	 * touch the derived intensity images.
	 */
	void AnalyzePixels(TArrayView<const FColor> Pixels, int32 Width, int32 Height, FTrueScopeAnalysisResult& OutResult);

	/**
	 * LINEAR intensity mapping of a bin count to display brightness in [0,1]:
	 * saturate(Count * Gain / MaxCount), hard-clipped at 1.0 = white.
	 *
	 * MaxCount is the scope's normalisation basis — the largest count one cell
	 * can hold (a column's height for the parade and waveform, the whole
	 * frame's pixel count for the vectorscope) — so the mapping stays
	 * resolution-independent and Gain remains a plain artist-facing multiplier
	 * with 1.0 = the scope's calibrated default.
	 *
	 * Linear since M6.3, replacing the M4-M6.2 log transfer
	 * (log(1+16*Count)/log(1+16*MaxCount)). Resolve's density transfer was
	 * MEASURED strictly linear (D1, 2026-07-20: linear-through-origin
	 * R^2 = 0.9999, power exponent p = 1.00), while the log curve lifted a
	 * single pixel in a 540-row column to 31 % brightness — the cause of
	 * "structure drowns in dense areas". See architecture.md, "Density transfer
	 * & Trace Brightness".
	 *
	 * Deliberately NOT the histogram's mapping: that one maps counts to curve
	 * HEIGHT against a fixed clip threshold (CountToCurveHeight). The two are
	 * kept separately named because merging them caused the M4 flat-topping
	 * bug.
	 */
	float CountToIntensity(uint32 Count, uint32 MaxCount, float Gain);

	/**
	 * Builds the display-mapped per-channel parade density from the parade
	 * counts (see FTrueScopeAnalysisResult::ParadeChannelIntensity for the
	 * layout). Intensities are normalized against the maximum possible count
	 * per cell — the source height, i.e. a column with every row at one value —
	 * so brightness is stable over time instead of re-scaling with scene
	 * content.
	 */
	void BuildParadeImage(FTrueScopeAnalysisResult& InOutResult, const FDensityGains& Gains = FDensityGains());

	/**
	 * Builds the display-mapped luma density from the luma counts (see
	 * FTrueScopeAnalysisResult::LumaIntensity), on the same normalization and
	 * density curve as the parade, so the two waveform modes are comparable.
	 */
	void BuildLumaWaveformImage(FTrueScopeAnalysisResult& InOutResult, const FDensityGains& Gains = FDensityGains());

	/**
	 * Builds the display-mapped vectorscope density from the vectorscope counts
	 * (see FTrueScopeAnalysisResult::VectorscopeIntensity). Normalized against
	 * the total pixel count — the maximum a single (Cb, Cr) cell can reach, as
	 * a flat neutral frame does — so brightness does not re-scale with content.
	 */
	void BuildVectorscopeImage(FTrueScopeAnalysisResult& InOutResult, const FDensityGains& Gains = FDensityGains());

	/**
	 * RGB waveform texels: the parade's three channel densities superimposed in
	 * one full-width trace instead of side-by-side blocks (Resolve's default
	 * waveform). Texel = (R density, G density, B density), so equal densities
	 * read white and a channel excursion shows in its own color.
	 *
	 * Mapped from the parade counts with the WAVEFORM's own Trace Brightness
	 * gain, not from the parade's intensity images — the two scopes carry
	 * independent gains since M6.3. OutTexels must hold exactly
	 * ParadeImageSize.X * ParadeImageSize.Y entries, laid out like
	 * ParadeChannelIntensity. Kept out of FTrueScopeAnalysisResult so the widget
	 * can fill its upload buffer directly.
	 */
	void BuildRgbWaveformTexels(const FTrueScopeAnalysisResult& Result, float WaveformGain,
		TArrayView<FColor> OutTexels);

	/**
	 * Luma waveform texels: the BT.709 luma density as a neutral trace — the
	 * classic exposure waveform. Requires BuildLumaWaveformImage to have run;
	 * same buffer contract as BuildRgbWaveformTexels.
	 */
	void BuildLumaWaveformTexels(const FTrueScopeAnalysisResult& Result, TArrayView<FColor> OutTexels);
}
