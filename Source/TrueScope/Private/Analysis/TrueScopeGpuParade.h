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
	 * Adds the parade binning compute pass (M5 GPU path, pass 1): one thread
	 * per pixel of InputTexture, uint InterlockedAdds into a transient count
	 * buffer laid out [Channel * (Width * NumBins) + Column * NumBins + Bin] —
	 * the per-channel layout of FTrueScopeAnalysisResult::Parade. Exposed
	 * separately so the equivalence test can run exactly the runtime binning
	 * and read the returned buffer back (test-only; the runtime never does).
	 */
	FRDGBufferRef AddParadeBinningPass(FRDGBuilder& GraphBuilder, FRDGTextureRef InputTexture, FIntPoint InputSize);
}

/**
 * GPU parade producer (M5): runs the two-pass compute analysis on the capture
 * texture inside the capture pass's own render graph and publishes a
 * GPU-resident FTrueScopeAnalysisResult — the pixel data and the bins never
 * leave the GPU (no readback of any kind on this path).
 *
 * Owns the three per-channel density render targets the parade widget draws
 * directly (PF_R8G8B8A8 + UAV, written by the intensity pass — the Niagara
 * render-target pattern). The targets are created once at the fixed capture
 * width x NumBins size and updated in place; each capture publishes a fresh
 * result through the analyzer so widgets repaint on their usual serial poll.
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

	/** Receives the published GPU results (same channel as the CPU analysis). */
	TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe> Analyzer;
};
