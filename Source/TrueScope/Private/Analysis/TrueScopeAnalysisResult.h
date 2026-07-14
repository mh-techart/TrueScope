// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UTextureRenderTarget2D;

/**
 * Result of one analysis pass over a captured frame — the data interface
 * between analysis producers and scope widgets. Widgets draw exclusively from
 * this struct and never know which producer ran: the CPU path fills the bin
 * count and intensity arrays; the M5 GPU path publishes GPU-resident parade
 * textures instead (bHasGpuParade) and leaves the arrays empty — its data
 * never exists in CPU memory, by design (no-readback constraint).
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

	/**
	 * GPU-resident parade (M5 GPU path): the per-channel density render
	 * targets written by the intensity compute pass, sized ParadeImageSize and
	 * carrying the same single-channel-per-texture layout as
	 * ParadeChannelIntensity — the parade widget points its brushes at them
	 * and draws with no CPU round-trip. Weak: the producer owns the targets;
	 * a stale result after producer teardown resolves to null and is skipped.
	 */
	TWeakObjectPtr<UTextureRenderTarget2D> ParadeChannelTarget[NumChannels];

	/** True when this result was produced by the GPU path (targets set, arrays empty). */
	bool bHasGpuParade = false;

	/**
	 * Region of each GPU raster target that this result actually rasterized, in
	 * texels (phase-6.1 Task 1). The targets are ALLOCATED in size buckets so a
	 * splitter drag does not reallocate them, but the raster fills exactly the
	 * widget's plot rect in device pixels — so the widget samples only this
	 * region (brush UV region) and one texel lands on one device pixel. Zero
	 * while the producing path has no raster of that kind.
	 */
	FIntPoint ParadeRasterSize = FIntPoint::ZeroValue;
	FIntPoint WaveformRasterSize = FIntPoint::ZeroValue;
	FIntPoint VectorscopeRasterSize = FIntPoint::ZeroValue;
	FIntPoint HistogramStackedRasterSize = FIntPoint::ZeroValue;
	FIntPoint HistogramOverlayRasterSize = FIntPoint::ZeroValue;

	/**
	 * GPU-resident vectorscope (phase-5.1 GPU path): the NumVectorBins^2
	 * density render target written by the vectorscope intensity pass — white
	 * texels carrying the density in alpha, the same convention as the CPU
	 * widget's upload of VectorscopeIntensity. Same weak-ownership contract as
	 * ParadeChannelTarget.
	 */
	TWeakObjectPtr<UTextureRenderTarget2D> VectorscopeTarget;

	/** True when the GPU path produced the vectorscope target above. */
	bool bHasGpuVectorscope = false;

	/**
	 * GPU-resident waveform (phase-5.2 GPU path): the two full-width trace
	 * render targets written by the column intensity pass, sized
	 * ParadeImageSize — RGB (the three channel densities superimposed, the
	 * CPU path's BuildRgbWaveformTexels) and Luma (neutral trace,
	 * BuildLumaWaveformTexels). The waveform widget points its brush at the
	 * one matching its display mode. Same weak-ownership contract as
	 * ParadeChannelTarget.
	 */
	TWeakObjectPtr<UTextureRenderTarget2D> WaveformRgbTarget;
	TWeakObjectPtr<UTextureRenderTarget2D> WaveformLumaTarget;

	/** True when the GPU path produced the waveform targets above. */
	bool bHasGpuWaveform = false;

	/**
	 * GPU-resident histogram (phase-5.2 GPU path): curve rasterizations
	 * written by the histogram draw passes, carrying the widget's exact fill
	 * and outline colours with coverage-scaled alpha. One texture per channel
	 * for the Separate Channels mode (the bands are disjoint, so per-channel
	 * compositing is exact) and one combined texture for the RGB Overlay mode
	 * (composited in the CPU vector draw's exact layer order). The curve
	 * HEIGHT is linear against the shared strongest-channel maximum
	 * (CountToCurveHeight in TrueScopeHistogram.usf) — NOT the log density
	 * brightness mapping (architecture, "Two distinct count mappings"). Same
	 * weak-ownership contract as ParadeChannelTarget.
	 */
	TWeakObjectPtr<UTextureRenderTarget2D> HistogramStackedTarget[NumChannels];
	TWeakObjectPtr<UTextureRenderTarget2D> HistogramOverlayTarget;

	/** True when the GPU path produced the histogram targets above. */
	bool bHasGpuHistogram = false;
};
