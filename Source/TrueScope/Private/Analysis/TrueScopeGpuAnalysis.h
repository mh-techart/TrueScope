// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Analysis/TrueScopeAnalysisResult.h"
#include "Analysis/TrueScopeRasterSizes.h"
#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "UObject/GCObject.h"
#include "UObject/ObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

class FTextureRenderTargetResource;
class FTrueScopeAnalyzer;
class UTextureRenderTarget2D;

namespace TrueScope
{
	/**
	 * Channels of the column x value-bin count buffer: R, G, B (the parade's
	 * layout, FTrueScopeAnalysisResult::Parade) plus the BT.709 luma
	 * (::LumaParade) as the fourth channel, added in phase-5.2 for the
	 * waveform. Mirrored by the hardcoded channel indices in
	 * TrueScopeParade.usf's ColumnBinCS.
	 */
	inline constexpr int32 ColumnBinChannels = FTrueScopeAnalysisResult::NumChannels + 1;
	inline constexpr int32 LumaBinChannel = FTrueScopeAnalysisResult::NumChannels;

	/**
	 * Adds the column binning compute pass (M5 GPU path, pass 1): one thread
	 * per pixel of InputTexture, uint InterlockedAdds into a transient count
	 * buffer laid out [Channel * (Width * NumBins) + Column * NumBins + Bin]
	 * with ColumnBinChannels channels — R/G/B for the parade, integer-exact
	 * BT.709 luma for the waveform (phase-5.2). Exposed separately so the
	 * equivalence test can run exactly the runtime binning and read the
	 * returned buffer back (test-only; the runtime never does).
	 */
	FRDGBufferRef AddColumnBinningPass(FRDGBuilder& GraphBuilder, FRDGTextureRef InputTexture, FIntPoint InputSize);

	/**
	 * Adds the vectorscope binning compute pass (phase-5.1 GPU path, pass 1):
	 * one thread per pixel, BT.709 R'G'B' -> Y'CbCr, one uint InterlockedAdd
	 * into a transient NumVectorBins^2 count grid laid out
	 * [CrBin * NumVectorBins + CbBin] — the layout of
	 * FTrueScopeAnalysisResult::Vectorscope. Exposed for the equivalence test,
	 * same contract as AddColumnBinningPass.
	 */
	FRDGBufferRef AddVectorscopeBinningPass(FRDGBuilder& GraphBuilder, FRDGTextureRef InputTexture,
		FIntPoint InputSize);

	/** Outputs of the histogram reduce pass (phase-5.2). */
	struct FHistogramReduceOutputs
	{
		/** [Channel * NumBins + Bin] over the three colour channels. */
		FRDGBufferRef Bins = nullptr;

		/** One uint: the strongest channel's peak — the shared curve scale. */
		FRDGBufferRef SharedMax = nullptr;
	};

	/**
	 * Adds the histogram reduce compute pass (phase-5.2 GPU path): sums the
	 * column bins over all columns into the per-channel 1D histogram — by
	 * definition the CPU histogram's exact numbers — and folds the shared
	 * three-channel maximum. Exposed for the equivalence test, same contract
	 * as AddColumnBinningPass.
	 */
	FHistogramReduceOutputs AddHistogramReducePass(FRDGBuilder& GraphBuilder, FRDGBufferRef ColumnBins,
		int32 SourceWidth);
}

/**
 * GPU analysis producer (M5 parade, phase-5.1 vectorscope, phase-5.2
 * waveform): runs the two-pass compute analyses on the capture texture inside
 * the capture pass's own render graph and publishes a GPU-resident
 * FTrueScopeAnalysisResult — the pixel data and the bins never leave the GPU
 * (no readback of any kind on this path).
 *
 * Owns the raster render targets the widgets draw directly (PF_R8G8B8A8 + UAV,
 * written by the intensity/draw passes — the Niagara render-target pattern):
 * three per-channel parade targets, two waveform trace targets, one vectorscope
 * target and the histogram's curve rasters. The targets are updated in place;
 * each capture publishes a fresh result through the analyzer so widgets repaint
 * on their usual serial poll.
 *
 * Every target is sized to the rect its widget draws it into, in DEVICE PIXELS
 * (phase-6.1 Task 1, FTrueScopeRasterSizes): one raster texel is one device
 * pixel, which is the anti-pixelation rule ("scope textures at display
 * resolution or higher") and what makes the GPU stroke as thin as the
 * CPU/Slate line — a raster with fewer texels than the panel has pixels cannot
 * carry a crisp 1-px line, and its upscale is anisotropic when the two axes are
 * magnified differently. Allocation happens in size buckets so dragging a
 * splitter cannot reallocate the targets every mouse move; the raster fills the
 * exact requested region of the bucketed target and the widget samples only
 * that region.
 */
class FTrueScopeGpuAnalysis : public TSharedFromThis<FTrueScopeGpuAnalysis, ESPMode::ThreadSafe>, public FGCObject
{
public:
	FTrueScopeGpuAnalysis(const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& InAnalyzer,
		const TSharedPtr<FTrueScopeRasterSizes, ESPMode::ThreadSafe>& InRasterSizes);

	//~ Begin FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FTrueScopeGpuAnalysis"); }
	//~ End FGCObject interface

	/**
	 * Applies the widgets' current plot rects to the render targets (resizing
	 * one only when its request crosses a bucket), then re-resolves the
	 * render-target resources and hands them, with the exact raster sizes, to
	 * the render thread. Called from the capture extension's
	 * BeginRenderViewFamily each frame (same robustness pattern as the capture
	 * RT's resource cache).
	 */
	void CacheResources_GameThread();

	/**
	 * Adds binning + intensity passes consuming the capture texture, then
	 * publishes the GPU-resident result. Same FRDGBuilder as the capture
	 * downscale, so the image never leaves the GPU.
	 */
	void AddAnalysisPasses_RenderThread(FRDGBuilder& GraphBuilder, FRDGTextureRef CaptureTexture);

private:
	/**
	 * Sizes Target to the widget's current request (bucketed allocation) and
	 * returns the exact region to rasterize, in texels. Game thread.
	 */
	FIntPoint ApplyRasterSize(UTextureRenderTarget2D* Target, TrueScope::ERasterTarget Which) const;

	/** Per-channel density targets the parade widget draws. Game thread owned. */
	TObjectPtr<UTextureRenderTarget2D> ChannelTarget[FTrueScopeAnalysisResult::NumChannels];

	/** Prebuilt weak pointers, copied into published results on the render thread. */
	TWeakObjectPtr<UTextureRenderTarget2D> ChannelTargetWeak[FTrueScopeAnalysisResult::NumChannels];

	/** Render-target resources, cached for the render thread. Render thread only. */
	FTextureRenderTargetResource* ChannelResourceRenderThread[FTrueScopeAnalysisResult::NumChannels] = {};

	/** Vectorscope density target (phase-5.1), same ownership pattern. */
	TObjectPtr<UTextureRenderTarget2D> VectorscopeTarget;
	TWeakObjectPtr<UTextureRenderTarget2D> VectorscopeTargetWeak;
	FTextureRenderTargetResource* VectorscopeResourceRenderThread = nullptr;

	/** Waveform trace targets (phase-5.2): RGB overlay and neutral luma. */
	TObjectPtr<UTextureRenderTarget2D> WaveformRgbTarget;
	TObjectPtr<UTextureRenderTarget2D> WaveformLumaTarget;
	TWeakObjectPtr<UTextureRenderTarget2D> WaveformRgbTargetWeak;
	TWeakObjectPtr<UTextureRenderTarget2D> WaveformLumaTargetWeak;
	FTextureRenderTargetResource* WaveformRgbResourceRenderThread = nullptr;
	FTextureRenderTargetResource* WaveformLumaResourceRenderThread = nullptr;

	/**
	 * Histogram curve targets (phase-5.2): one per channel for the stacked
	 * Separate Channels mode plus one combined RGB Overlay texture. The two
	 * sets are separate targets because the modes draw into rects of very
	 * different heights (a band vs. the whole plot), and each is rasterized at
	 * its own rect's device resolution — so the outline is exactly the CPU
	 * draw's 1-px Slate line in either mode, and switching modes needs no new
	 * capture.
	 */
	TObjectPtr<UTextureRenderTarget2D> HistogramStackedTarget[FTrueScopeAnalysisResult::NumChannels];
	TObjectPtr<UTextureRenderTarget2D> HistogramOverlayTarget;
	TWeakObjectPtr<UTextureRenderTarget2D> HistogramStackedTargetWeak[FTrueScopeAnalysisResult::NumChannels];
	TWeakObjectPtr<UTextureRenderTarget2D> HistogramOverlayTargetWeak;
	FTextureRenderTargetResource* HistogramStackedResourceRenderThread[FTrueScopeAnalysisResult::NumChannels] = {};
	FTextureRenderTargetResource* HistogramOverlayResourceRenderThread = nullptr;

	/** Receives the published GPU results (same channel as the CPU analysis). */
	TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe> Analyzer;

	/** The widgets' plot rects in device pixels — what the targets are sized to. */
	TSharedPtr<FTrueScopeRasterSizes, ESPMode::ThreadSafe> RasterSizes;

	/**
	 * The regions the passes rasterize this capture, snapshotted on the game
	 * thread with the resources above (so size and resource can never disagree)
	 * and read on the render thread only.
	 */
	FIntPoint ParadeRasterSizeRenderThread = FIntPoint::ZeroValue;
	FIntPoint WaveformRasterSizeRenderThread = FIntPoint::ZeroValue;
	FIntPoint VectorscopeRasterSizeRenderThread = FIntPoint::ZeroValue;
	FIntPoint HistogramStackedRasterSizeRenderThread = FIntPoint::ZeroValue;
	FIntPoint HistogramOverlayRasterSizeRenderThread = FIntPoint::ZeroValue;
};
