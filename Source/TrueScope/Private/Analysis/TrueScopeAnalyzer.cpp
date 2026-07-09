// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "Analysis/TrueScopeAnalyzer.h"

#include "Analysis/TrueScopeAnalysis.h"
#include "Misc/CoreDelegates.h"
#include "RHIGPUReadback.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "Tasks/Task.h"

DEFINE_LOG_CATEGORY_STATIC(LogTrueScopeAnalysis, Log, All);

namespace
{
	/** Every Nth completed cycle, log the average cycle cost at Log verbosity. */
	constexpr uint64 CostLogInterval = 50;
}

void FTrueScopeAnalyzer::Shutdown()
{
	check(IsInGameThread());

	// The poll delegate and the readback are render-thread state: unhook and
	// release them there, then wait so no render command can touch this
	// object afterwards. A still-running background analysis task keeps the
	// analyzer alive via its shared pointer and only touches CPU data.
	ENQUEUE_RENDER_COMMAND(TrueScopeAnalyzerShutdown)(
		[this](FRHICommandListImmediate&)
		{
			if (EndFrameRTHandle.IsValid())
			{
				FCoreDelegates::OnEndFrameRT.Remove(EndFrameRTHandle);
				EndFrameRTHandle.Reset();
			}
			// Safe even if a copy is still in flight: the staging texture and
			// fence are refcounted RHI resources, and the enqueued GPU copy
			// holds its own references.
			Readback.Reset();
			bReadbackInFlight = false;
		});
	FlushRenderingCommands();
}

void FTrueScopeAnalyzer::EnqueueReadback_RenderThread(FRDGBuilder& GraphBuilder, FRDGTextureRef CaptureTexture)
{
	check(IsInRenderingThread());

	// Single readback buffer: while the previous readback or its analysis is
	// still in flight, this capture is simply not measured (spec: skip, don't
	// queue).
	if (bReadbackInFlight || bAnalysisInFlight.load(std::memory_order_acquire))
	{
		return;
	}

	if (!Readback)
	{
		Readback = MakeUnique<FRHIGPUTextureReadback>(TEXT("TrueScope.CaptureReadback"));
	}

	// Poll from the per-frame render-thread tick: the capture extension only
	// runs on throttled capture frames, so it cannot poll readiness itself.
	// Registered lazily here (and removed in Shutdown) so all delegate
	// mutation happens on the render thread, serialized with the broadcast.
	if (!EndFrameRTHandle.IsValid())
	{
		EndFrameRTHandle = FCoreDelegates::OnEndFrameRT.AddSP(
			AsShared(), &FTrueScopeAnalyzer::PollReadback_RenderThread);
	}

	AddEnqueueCopyPass(GraphBuilder, Readback.Get(), CaptureTexture);

	PendingReadbackSize = FIntPoint(CaptureTexture->Desc.Extent.X, CaptureTexture->Desc.Extent.Y);
	CaptureStartSeconds = FPlatformTime::Seconds();
	bReadbackInFlight = true;
}

void FTrueScopeAnalyzer::PollReadback_RenderThread()
{
	if (!bReadbackInFlight || !Readback || !Readback->IsReady())
	{
		return;
	}

	// Ready: move the staging data off the render thread. Lock cannot block
	// here since the fence has signaled. The staging buffer's row pitch may
	// exceed the texture width (GPU row alignment) — copy row by row.
	const double CopyStartSeconds = FPlatformTime::Seconds();

	int32 RowPitchInPixels = 0;
	int32 BufferHeight = 0;
	const FColor* SrcData = static_cast<const FColor*>(Readback->Lock(RowPitchInPixels, &BufferHeight));

	const FIntPoint Size = PendingReadbackSize;
	TArray<FColor> Pixels;
	if (SrcData && RowPitchInPixels >= Size.X && BufferHeight >= Size.Y)
	{
		Pixels.SetNumUninitialized(Size.X * Size.Y);
		FColor* Dst = Pixels.GetData();
		const FColor* Src = SrcData;
		for (int32 Y = 0; Y < Size.Y; ++Y)
		{
			FMemory::Memcpy(Dst, Src, Size.X * sizeof(FColor));
			Dst += Size.X;
			Src += RowPitchInPixels;
		}
	}
	else
	{
		UE_LOG(LogTrueScopeAnalysis, Warning,
			TEXT("Readback lock returned unusable staging data (%p, pitch %d, height %d for %dx%d) — dropping cycle"),
			SrcData, RowPitchInPixels, BufferHeight, Size.X, Size.Y);
	}
	Readback->Unlock();
	bReadbackInFlight = false;

	if (Pixels.Num() == 0)
	{
		return;
	}

	const double CopyMs = (FPlatformTime::Seconds() - CopyStartSeconds) * 1000.0;

	// Hand off to a background task; the render thread is done with this cycle.
	bAnalysisInFlight.store(true, std::memory_order_release);
	UE::Tasks::Launch(UE_SOURCE_LOCATION,
		[Self = AsShared(), Pixels = MoveTemp(Pixels), Size, StartSeconds = CaptureStartSeconds, CopyMs]() mutable
		{
			Self->RunAnalysisTask(MoveTemp(Pixels), Size, StartSeconds, CopyMs);
		});
}

void FTrueScopeAnalyzer::RunAnalysisTask(TArray<FColor>&& Pixels, FIntPoint Size, double InCaptureStartSeconds,
	double InCopyMs)
{
	const double AnalysisStartSeconds = FPlatformTime::Seconds();

	TSharedRef<FTrueScopeAnalysisResult, ESPMode::ThreadSafe> Result =
		MakeShared<FTrueScopeAnalysisResult, ESPMode::ThreadSafe>();
	TrueScope::AnalyzePixels(Pixels, Size.X, Size.Y, *Result);
	TrueScope::BuildParadeImage(*Result);
	TrueScope::BuildVectorscopeImage(*Result);

	const double Now = FPlatformTime::Seconds();
	Result->AnalysisDurationMs = (Now - AnalysisStartSeconds) * 1000.0;

	PublishResult(Result);
	LogCycleCost(*Result, (Now - InCaptureStartSeconds) * 1000.0, InCopyMs);

	bAnalysisInFlight.store(false, std::memory_order_release);
}

void FTrueScopeAnalyzer::PublishResult(TSharedPtr<const FTrueScopeAnalysisResult, ESPMode::ThreadSafe> Result)
{
	{
		FScopeLock Lock(&ResultLock);
		LatestResult = MoveTemp(Result);
	}
	ResultSerial.fetch_add(1, std::memory_order_release);
}

TSharedPtr<const FTrueScopeAnalysisResult, ESPMode::ThreadSafe> FTrueScopeAnalyzer::GetLatestResult() const
{
	FScopeLock Lock(&ResultLock);
	return LatestResult;
}

void FTrueScopeAnalyzer::LogCycleCost(const FTrueScopeAnalysisResult& Result, double TotalCycleMs, double CopyMs)
{
	// Runs on the background task only — no synchronization needed.
	++CompletedCycles;
	AccumulatedCycleMs += TotalCycleMs;
	AccumulatedAnalysisMs += Result.AnalysisDurationMs;
	AccumulatedCopyMs += CopyMs;

	UE_LOG(LogTrueScopeAnalysis, Verbose,
		TEXT("Cycle %llu: capture->result %.2f ms (staging copy %.2f ms, analysis %.2f ms)"),
		CompletedCycles, TotalCycleMs, CopyMs, Result.AnalysisDurationMs);

	if (CompletedCycles == 1 || CompletedCycles % CostLogInterval == 0)
	{
		UE_LOG(LogTrueScopeAnalysis, Log,
			TEXT("Per-cycle cost after %llu cycles (avg): capture->result %.2f ms, staging copy %.2f ms, analysis %.2f ms"),
			CompletedCycles,
			AccumulatedCycleMs / CompletedCycles,
			AccumulatedCopyMs / CompletedCycles,
			AccumulatedAnalysisMs / CompletedCycles);
	}
}
