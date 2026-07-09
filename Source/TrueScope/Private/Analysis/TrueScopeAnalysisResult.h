// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"

/**
 * Result of one CPU analysis pass over a captured frame — the data interface
 * between analysis producers and scope widgets. Widgets draw exclusively from
 * this struct; the M5 GPU path will produce the same data (bin arrays and/or
 * textures) without any UI change.
 *
 * Published as TSharedPtr<const FTrueScopeAnalysisResult> and treated as
 * immutable after publication, so any thread may read it without locking.
 */
struct FTrueScopeAnalysisResult
{
	/** Channel indices into the per-channel arrays. */
	static constexpr int32 ChannelR = 0;
	static constexpr int32 ChannelG = 1;
	static constexpr int32 ChannelB = 2;
	static constexpr int32 NumChannels = 3;

	/** Bins per channel; 256 matches the 8-bit display-encoded capture. */
	static constexpr int32 NumBins = 256;

	/** Vectorscope bins per chroma axis; both axes span [-0.5, 0.5]. */
	static constexpr int32 NumVectorBins = 256;

	/** Dimensions of the analyzed capture (960x540 in M3). */
	FIntPoint SourceSize = FIntPoint::ZeroValue;

	/**
	 * Histogram: per-channel pixel counts over value bins, size NumBins.
	 * Exact counts (ground truth for the M5 GPU path), display mapping is
	 * applied at draw time.
	 */
	TArray<uint32> Histogram[NumChannels];

	/**
	 * Parade: per-channel 2D counts, image column x value bin, indexed
	 * [Column * NumBins + Bin], size SourceSize.X * NumBins. Exact counts.
	 */
	TArray<uint32> Parade[NumChannels];

	/**
	 * Display-mapped parade density, one intensity array PER channel (log
	 * intensity, see TrueScopeAnalysis.h), size SourceSize.X * NumBins, indexed
	 * [Row * SourceSize.X + Column] with Row 0 = top = highest value bin.
	 *
	 * Each array holds ONLY its own channel's density: the parade's R/G/B
	 * blocks are single-channel by construction, so a block can never show
	 * another channel's trace (no cross-channel bleed). Derived convenience
	 * data, not ground truth.
	 */
	TArray<uint8> ParadeChannelIntensity[NumChannels];
	FIntPoint ParadeImageSize = FIntPoint::ZeroValue;

	/**
	 * Luma waveform: per-column counts over BT.709 luma bins, indexed
	 * [Column * NumBins + Bin], size SourceSize.X * NumBins. Exact counts; the
	 * luma is the vectorscope's own intermediate, not a second colour pass.
	 */
	TArray<uint32> LumaParade;

	/**
	 * Display-mapped luma density, laid out exactly like ParadeChannelIntensity
	 * (size ParadeImageSize, row 0 = top = highest value bin). Derived
	 * convenience data, not ground truth.
	 */
	TArray<uint8> LumaIntensity;

	/**
	 * Vectorscope: pixel counts over the (Cb, Cr) plane, indexed
	 * [CrBin * NumVectorBins + CbBin]; bin 0 = -0.5, last bin = +0.5 on both
	 * axes. Exact counts (ground truth for the M5 GPU path).
	 */
	TArray<uint32> Vectorscope;

	/**
	 * Display-mapped vectorscope density (log intensity), size
	 * NumVectorBins^2, indexed [Row * NumVectorBins + CbBin] with Row 0 = top =
	 * highest Cr. Derived convenience data, not ground truth.
	 */
	TArray<uint8> VectorscopeIntensity;

	/** Cost of the CPU analysis (background task), for the M3 measurement. */
	double AnalysisDurationMs = 0.0;
};
