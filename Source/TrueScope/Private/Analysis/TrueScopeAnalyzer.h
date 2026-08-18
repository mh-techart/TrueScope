// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Analysis/TrueScopeAnalysisResult.h"
#include "Analysis/TrueScopeDisplayGains.h"
#include "CoreMinimal.h"
#include "RenderGraphFwd.h"

#include <atomic>

class FRHIGPUTextureReadback;

/**
 * Owns the asynchronous GPU->CPU pipeline of the capture render target:
 *
 *   capture pass (render thread, ~2.5 Hz)
 *     -> EnqueueReadback_RenderThread: FRHIGPUTextureReadback copy enqueued
 *        in the same render graph (AddEnqueueCopyPass); single buffer,
 *        skipped entirely while a previous readback/analysis is in flight
 *   every frame (render thread, FCoreDelegates::OnEndFrameRT)
 *     -> poll IsReady(); when ready Lock / pitch-aware copy / Unlock —
 *        never waits on the fence, so the render thread is never stalled
 *   background task (UE::Tasks)
 *     -> TrueScope::AnalyzePixels + BuildParadeImage + BuildLumaWaveformImage
 *        + BuildVectorscopeImage
 *     -> publishes an immutable TSharedPtr<const FTrueScopeAnalysisResult>
 *
 * Widgets read the latest result from any thread via GetLatestResult() and
 * detect changes cheaply via GetResultSerial().
 *
 * Pattern follows the engine's FLandscapeAsyncTextureReadback
 * (Runtime/Landscape/Private/LandscapeAsyncTextureReadback.cpp).
 */
class FTrueScopeAnalyzer : public TSharedFromThis<FTrueScopeAnalyzer, ESPMode::ThreadSafe>
{
public:
	/**
	 * Unhooks the render-thread poll delegate and releases the readback on
	 * the render thread, then flushes rendering commands. Call from the game
	 * thread before dropping the last shared reference.
	 */
	void Shutdown();

	/**
	 * Called by the capture pass right after the downscale draw, on the same
	 * FRDGBuilder. CaptureTexture is the registered external capture RT.
	 */
	void EnqueueReadback_RenderThread(FRDGBuilder& GraphBuilder, FRDGTextureRef CaptureTexture);

	/** Latest published result; null until the first analysis completes. Any thread. */
	TSharedPtr<const FTrueScopeAnalysisResult, ESPMode::ThreadSafe> GetLatestResult() const;

	/** Monotonic counter bumped on every published result. Any thread. */
	uint64 GetResultSerial() const { return ResultSerial.load(std::memory_order_acquire); }

	/**
	 * Publishes a result to the widgets. Any thread. Called by the CPU
	 * analysis task and by the M5 GPU producer — one publication channel, so
	 * widgets never know which producer ran.
	 */
	void PublishResult(TSharedPtr<const FTrueScopeAnalysisResult, ESPMode::ThreadSafe> Result);

	/**
	 * The scopes' Trace Brightness gains, read by the background analysis task
	 * when it maps counts to density brightness (M6.3). Set once at start-up;
	 * the object itself is atomic inside, so the task may read it freely.
	 */
	void SetDisplayGains(const TSharedPtr<FTrueScopeDisplayGains, ESPMode::ThreadSafe>& InDisplayGains)
	{
		DisplayGains = InDisplayGains;
	}

private:
	void PollReadback_RenderThread();
	void RunAnalysisTask(TArray<FColor>&& Pixels, FIntPoint Size, double InCaptureStartSeconds, double InCopyMs);
	void LogCycleCost(const FTrueScopeAnalysisResult& Result, double TotalCycleMs, double CopyMs);

	// --- Render thread state ---
	TUniquePtr<FRHIGPUTextureReadback> Readback;
	bool bReadbackInFlight = false;
	FIntPoint PendingReadbackSize = FIntPoint::ZeroValue;
	double CaptureStartSeconds = 0.0;
	FDelegateHandle EndFrameRTHandle;

	/** Set while the background analysis task runs; readbacks are skipped. */
	std::atomic<bool> bAnalysisInFlight{false};

	// --- Published result (any thread, guarded) ---
	mutable FCriticalSection ResultLock;
	TSharedPtr<const FTrueScopeAnalysisResult, ESPMode::ThreadSafe> LatestResult;
	std::atomic<uint64> ResultSerial{0};

	/** Per-scope Trace Brightness gains; null in tests, which pass gains directly. */
	TSharedPtr<FTrueScopeDisplayGains, ESPMode::ThreadSafe> DisplayGains;

	// --- Cost measurement (background task only) ---
	uint64 CompletedCycles = 0;
	double AccumulatedCycleMs = 0.0;
	double AccumulatedAnalysisMs = 0.0;
	double AccumulatedCopyMs = 0.0;
};
