// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Analysis/TrueScopeAnalysisResult.h"
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
	 * same contract as AddParadeBinningPass.
	 */
	FRDGBufferRef AddVectorscopeBinningPass(FRDGBuilder& GraphBuilder, FRDGTextureRef InputTexture,
		FIntPoint InputSize);
}

/**
 * GPU analysis producer (M5 parade, phase-5.1 vectorscope, phase-5.2
 * waveform): runs the two-pass compute analyses on the capture texture inside
 * the capture pass's own render graph and publishes a GPU-resident
 * FTrueScopeAnalysisResult — the pixel data and the bins never leave the GPU
 * (no readback of any kind on this path).
 *
 * Owns the density render targets the widgets draw directly (PF_R8G8B8A8 +
 * UAV, written by the intensity passes — the Niagara render-target pattern):
 * three per-channel parade targets and two waveform trace targets (capture
 * width x NumBins) and one vectorscope target (NumVectorBins^2, white with
 * density in alpha). The targets are created once and updated in place; each
 * capture publishes a fresh result through the analyzer so widgets repaint on
 * their usual serial poll.
 */
class FTrueScopeGpuParade : public TSharedFromThis<FTrueScopeGpuParade, ESPMode::ThreadSafe>, public FGCObject
{
public:
	explicit FTrueScopeGpuParade(const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& InAnalyzer);

	//~ Begin FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FTrueScopeGpuParade"); }
	//~ End FGCObject interface

	/**
	 * Re-resolves the render-target resources and hands them to the render
	 * thread. Called from the capture extension's BeginRenderViewFamily each
	 * frame (same robustness pattern as the capture RT's resource cache).
	 */
	void CacheResources_GameThread();

	/**
	 * Adds binning + intensity passes consuming the capture texture, then
	 * publishes the GPU-resident result. Same FRDGBuilder as the capture
	 * downscale, so the image never leaves the GPU.
	 */
	void AddAnalysisPasses_RenderThread(FRDGBuilder& GraphBuilder, FRDGTextureRef CaptureTexture);

private:
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

	/** Receives the published GPU results (same channel as the CPU analysis). */
	TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe> Analyzer;
};
