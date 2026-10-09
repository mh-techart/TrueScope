// Copyright 2026 Michael Herber
// SPDX-License-Identifier: Apache-2.0

#include "Analysis/TrueScopeAnalysis.h"
#include "Analysis/TrueScopeAnalysisResult.h"
#include "Analysis/TrueScopeAnalyzer.h"
#include "Analysis/TrueScopeDisplayGains.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "TrueScopeModule.h"
#include "TrueScopeRedrawRequest.h"
#include "TrueScopeSettings.h"
#include "UI/STrueScopeWaveform.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * M6.4.1 Task 1: a gain change alone makes the scope re-render, from the frame
 * that was already measured — no new capture, no readback.
 *
 * Three layers, because the fix spans three and a break in any one of them
 * restores exactly the regression this phase exists to remove:
 *
 *   1. the widget raises a redraw request when (and only when) the gain
 *      actually moved                              — GainRedrawRequest
 *   2. the CPU producer can re-derive the display images from the counts it
 *      still holds, without touching capture       — GainRemapWithoutCapture
 *   2b. ...and declines when the gains it controls have not moved, which is
 *      what a histogram gain change looks like from there
 *                                                  — HistogramGainSkipsCpuRemap
 *   3. requests arriving faster than frames collapse to one
 *                                                  — RedrawRequestCoalescing
 *
 * The GPU half of layer 2 is FTrueScopeViewExtension::RedrawFromLastCapture,
 * which re-runs the compute analysis on the resident capture render target.
 * It is not unit-testable here (it needs a real RHI and a captured frame) and
 * is covered by the human/MCP check recorded in docs/phase-6.4.md.
 */

namespace
{
	using FResult = FTrueScopeAnalysisResult;

	/**
	 * A vertical value ramp: every column carries all Height distinct values,
	 * so every populated bin holds exactly ONE pixel.
	 *
	 * Chosen so the density transfer stays off its clip: intensity is
	 * Count * Gain / Height (TrueScope::CountToIntensity, normalized against
	 * the source height), so one pixel per bin at the parade's calibrated
	 * 31.25 reaches 31.25/256 = 12.2 % of white and has room to move in both
	 * directions. A flat image would clip at every gain and prove nothing.
	 */
	void MakeRampResult(FResult& OutResult, TArray<FColor>& OutPixels)
	{
		constexpr int32 Width = 16;
		constexpr int32 Height = FResult::NumBins;   // one row per value bin

		OutPixels.SetNumUninitialized(Width * Height);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			// Slightly different per channel so the chroma varies too and the
			// vectorscope is not a single cell.
			const FColor Value(
				static_cast<uint8>(Y),
				static_cast<uint8>((Y * 3) / 4),
				static_cast<uint8>(Y / 2),
				255);
			for (int32 X = 0; X < Width; ++X)
			{
				OutPixels[Y * Width + X] = Value;
			}
		}

		TrueScope::AnalyzePixels(OutPixels, Width, Height, OutResult);
	}

	/** Total of every parade/luma/vectorscope count, i.e. what binning produced. */
	uint64 SumCounts(const FResult& Result)
	{
		uint64 Total = 0;
		for (int32 Channel = 0; Channel < FResult::NumChannels; ++Channel)
		{
			for (uint32 Count : Result.Parade[Channel])
			{
				Total += Count;
			}
			for (uint32 Count : Result.Histogram[Channel])
			{
				Total += Count;
			}
		}
		for (uint32 Count : Result.LumaParade)
		{
			Total += Count;
		}
		for (uint32 Count : Result.Vectorscope)
		{
			Total += Count;
		}
		return Total;
	}
}

// ---------------------------------------------------------------------------
// 1. The widget raises the request
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeGainRedrawRequestTest,
	"TrueScope.UI.GainRedrawRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeGainRedrawRequestTest::RunTest(const FString&)
{
	constexpr int32 Panel = 0;
	UTrueScopeSettings& Settings = UTrueScopeSettings::Get();
	const float SavedMultiplier =
		Settings.GetPanelGainMultiplier(Panel, TrueScope::TraceBrightnessGainKey);

	FTrueScopeModule& Module = FTrueScopeModule::Get();
	TSharedRef<STrueScopeWaveform> Scope = SNew(STrueScopeWaveform, nullptr).PanelIndex(Panel);

	// Construction restores the persisted gain and may itself have raised a
	// request; start from a known-clear latch either way.
	Module.ConsumeScopeRedrawRequestForTesting();
	TestFalse(TEXT("Latch starts clear"), Module.IsScopeRedrawPending());

	// A real change asks for a re-render. Without this the slider only takes
	// visible effect at the next capture — which, while the menu that holds
	// the slider is open, never comes.
	const float Moved = (SavedMultiplier == 2.0f) ? 4.0f : 2.0f;
	Scope->ApplyGainMultiplier(Moved);
	TestTrue(TEXT("Moving the gain raises a redraw request"), Module.IsScopeRedrawPending());
	TestEqual(TEXT("The widget took the new multiplier"), Scope->GetGainMultiplier(), Moved);
	TestTrue(TEXT("Consuming clears it"), Module.ConsumeScopeRedrawRequestForTesting());

	// Setting the value it already has must NOT: a drag reports every mouse
	// move, including the ones that land on the current value and every move
	// once the slider sits at an end stop.
	Scope->ApplyGainMultiplier(Moved);
	TestFalse(TEXT("Re-setting the same value raises nothing"), Module.IsScopeRedrawPending());

	// Double-click reset goes through the same route (acceptance: "reset is
	// immediate too").
	Scope->ResetGainMultiplier();
	TestEqual(TEXT("Reset returns to the neutral multiplier"),
		Scope->GetGainMultiplier(), TrueScope::DefaultGainMultiplier);
	TestTrue(TEXT("Reset raises a redraw request"),
		Module.ConsumeScopeRedrawRequestForTesting());

	Settings.SetPanelGainMultiplier(Panel, TrueScope::TraceBrightnessGainKey, SavedMultiplier);
	Module.ConsumeScopeRedrawRequestForTesting();
	return true;
}

// ---------------------------------------------------------------------------
// 2. The CPU producer re-derives from the counts it kept
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeGainRemapWithoutCaptureTest,
	"TrueScope.Analysis.GainRemapWithoutCapture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeGainRemapWithoutCaptureTest::RunTest(const FString&)
{
	// A standalone analyzer with NO capture, NO readback and no RHI work of
	// any kind: it never sees EnqueueReadback_RenderThread, so the readback
	// buffer is never even created. That is what makes this test the evidence
	// for "the re-render introduces no readback" rather than an assertion —
	// everything below runs with the capture side of the analyzer untouched.
	TSharedRef<FTrueScopeAnalyzer, ESPMode::ThreadSafe> Analyzer =
		MakeShared<FTrueScopeAnalyzer, ESPMode::ThreadSafe>();
	TSharedPtr<FTrueScopeDisplayGains, ESPMode::ThreadSafe> Gains =
		MakeShared<FTrueScopeDisplayGains, ESPMode::ThreadSafe>();
	Analyzer->SetDisplayGains(Gains);

	TestEqual(TEXT("Nothing measured yet, so there is nothing to re-map"),
		static_cast<int32>(Analyzer->RemapLastResultWithCurrentGains()),
		static_cast<int32>(TrueScope::ERedrawOutcome::NoData));

	// One analysis, at the neutral multiplier, published the normal way.
	TArray<FColor> Pixels;
	TSharedRef<FResult, ESPMode::ThreadSafe> First = MakeShared<FResult, ESPMode::ThreadSafe>();
	MakeRampResult(*First, Pixels);
	TrueScope::BuildParadeImage(*First);
	TrueScope::BuildLumaWaveformImage(*First);
	TrueScope::BuildVectorscopeImage(*First);
	Analyzer->PublishResult(First);

	const uint64 SerialBefore = Analyzer->GetResultSerial();
	const uint64 CountsBefore = SumCounts(*First);

	// What a full re-analysis at half brightness WOULD produce, computed here
	// from the pixels — the reference the re-map has to reproduce exactly.
	constexpr float HalfBrightness = 0.5f;
	FResult Reference;
	TrueScope::AnalyzePixels(Pixels, First->SourceSize.X, First->SourceSize.Y, Reference);
	TrueScope::FDensityGains ReferenceGains;
	ReferenceGains.Parade =
		TrueScope::GetDefaultDensityGain(TrueScope::EDensityScope::Parade) * HalfBrightness;
	ReferenceGains.Waveform =
		TrueScope::GetDefaultDensityGain(TrueScope::EDensityScope::Waveform) * HalfBrightness;
	ReferenceGains.Vectorscope =
		TrueScope::GetDefaultDensityGain(TrueScope::EDensityScope::Vectorscope) * HalfBrightness;
	TrueScope::BuildParadeImage(Reference, ReferenceGains);
	TrueScope::BuildLumaWaveformImage(Reference, ReferenceGains);
	TrueScope::BuildVectorscopeImage(Reference, ReferenceGains);

	// The gain moves; only the display mapping is re-run.
	Gains->SetDensityGain(TrueScope::EDensityScope::Parade, HalfBrightness);
	Gains->SetDensityGain(TrueScope::EDensityScope::Waveform, HalfBrightness);
	Gains->SetDensityGain(TrueScope::EDensityScope::Vectorscope, HalfBrightness);

	TestEqual(TEXT("A re-map starts"),
		static_cast<int32>(Analyzer->RemapLastResultWithCurrentGains()),
		static_cast<int32>(TrueScope::ERedrawOutcome::Started));

	// It runs on a task, so the caller is never blocked; wait for the publish.
	const double Deadline = FPlatformTime::Seconds() + 5.0;
	while (Analyzer->GetResultSerial() == SerialBefore && FPlatformTime::Seconds() < Deadline)
	{
		FPlatformProcess::Sleep(0.001f);
	}

	TestEqual(TEXT("Exactly one new result was published"),
		static_cast<int64>(Analyzer->GetResultSerial()), static_cast<int64>(SerialBefore + 1));

	TSharedPtr<const FResult, ESPMode::ThreadSafe> Remapped = Analyzer->GetLatestResult();
	if (!Remapped.IsValid() || Remapped.Get() == &First.Get())
	{
		AddError(TEXT("The re-map published nothing within the timeout"));
		return false;
	}

	// The parade and luma images now match a full re-analysis at the new gain,
	// bit for bit.
	for (int32 Channel = 0; Channel < FResult::NumChannels; ++Channel)
	{
		TestTrue(*FString::Printf(
			TEXT("Parade channel %d re-derived exactly as a fresh analysis would"), Channel),
			Remapped->ParadeChannelIntensity[Channel] == Reference.ParadeChannelIntensity[Channel]);
	}
	TestTrue(TEXT("Luma trace re-derived exactly as a fresh analysis would"),
		Remapped->LumaIntensity == Reference.LumaIntensity);
	TestTrue(TEXT("Vectorscope re-derived exactly as a fresh analysis would"),
		Remapped->VectorscopeIntensity == Reference.VectorscopeIntensity);

	// ...and it is a genuinely different picture from the one before, so the
	// comparison above cannot pass by nothing having happened. (The parade and
	// luma carry this: at any gain the vectorscope's cells sit far above their
	// clip on an image this small, so its texels would match either way.)
	TestTrue(TEXT("The parade actually changed"),
		Remapped->ParadeChannelIntensity[FResult::ChannelR] != First->ParadeChannelIntensity[FResult::ChannelR]);
	TestTrue(TEXT("The luma trace actually changed"),
		Remapped->LumaIntensity != First->LumaIntensity);

	// Nothing was re-binned and nothing was re-captured: the counts and the
	// analysed frame are carried over from the published result untouched.
	TestEqual(TEXT("Bin counts are the retained ones, not re-measured"),
		static_cast<int64>(SumCounts(*Remapped)), static_cast<int64>(CountsBefore));
	for (int32 Channel = 0; Channel < FResult::NumChannels; ++Channel)
	{
		TestTrue(*FString::Printf(TEXT("Parade counts of channel %d untouched"), Channel),
			Remapped->Parade[Channel] == First->Parade[Channel]);
	}
	TestTrue(TEXT("Still the same analysed frame"), Remapped->SourceSize == First->SourceSize);

	// A GPU result carries no CPU bins to re-map (they are RDG-scoped and gone
	// by the time a slider moves), so the analyzer must decline rather than
	// republish an empty picture — the GPU path re-renders through the view
	// extension instead.
	TSharedRef<FResult, ESPMode::ThreadSafe> GpuResult = MakeShared<FResult, ESPMode::ThreadSafe>();
	GpuResult->SourceSize = First->SourceSize;
	GpuResult->bHasGpuParade = true;
	Analyzer->PublishResult(GpuResult);
	TestEqual(TEXT("A GPU result is declined, not re-mapped on the CPU"),
		static_cast<int32>(Analyzer->RemapLastResultWithCurrentGains()),
		static_cast<int32>(TrueScope::ERedrawOutcome::NoData));

	return true;
}

// ---------------------------------------------------------------------------
// 2b. A histogram gain change costs the CPU path nothing
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeHistogramGainSkipsCpuRemapTest,
	"TrueScope.Analysis.HistogramGainSkipsCpuRemap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeHistogramGainSkipsCpuRemapTest::RunTest(const FString&)
{
	// The redraw request is ONE global latch for all four panels, so the CPU
	// producer is asked to re-map whenever any gain moves — including the
	// histogram's, which on this path needs nothing: it maps counts to curve
	// height at paint time and the widget's own Invalidate has already redrawn
	// it. Re-mapping anyway would rebuild the three density images at unchanged
	// gains, i.e. repaint the identical picture, every frame of a drag.
	TSharedRef<FTrueScopeAnalyzer, ESPMode::ThreadSafe> Analyzer =
		MakeShared<FTrueScopeAnalyzer, ESPMode::ThreadSafe>();
	TSharedPtr<FTrueScopeDisplayGains, ESPMode::ThreadSafe> Gains =
		MakeShared<FTrueScopeDisplayGains, ESPMode::ThreadSafe>();
	Analyzer->SetDisplayGains(Gains);

	TArray<FColor> Pixels;
	TSharedRef<FResult, ESPMode::ThreadSafe> Measured = MakeShared<FResult, ESPMode::ThreadSafe>();
	MakeRampResult(*Measured, Pixels);
	const TrueScope::FDensityGains AtDefaults = TrueScope::FDensityGains::Defaults();
	TrueScope::BuildParadeImage(*Measured, AtDefaults);
	TrueScope::BuildLumaWaveformImage(*Measured, AtDefaults);
	TrueScope::BuildVectorscopeImage(*Measured, AtDefaults);
	Measured->AppliedDensityGains = AtDefaults;
	Analyzer->PublishResult(Measured);

	const uint64 SerialBefore = Analyzer->GetResultSerial();

	// Well clear of 1.0 in both directions, so this is not a rounding question.
	for (const float HistogramGain : {4.0f, 0.25f})
	{
		Gains->SetHistogramGain(HistogramGain);
		TestEqual(*FString::Printf(
			TEXT("Histogram gain %.2f leaves the CPU re-map nothing to do"), HistogramGain),
			static_cast<int32>(Analyzer->RemapLastResultWithCurrentGains()),
			static_cast<int32>(TrueScope::ERedrawOutcome::NoData));
	}

	TestEqual(TEXT("...and therefore publishes no new result"),
		static_cast<int64>(Analyzer->GetResultSerial()), static_cast<int64>(SerialBefore));

	// The guard must not swallow the case it exists beside: one density gain
	// moving still starts a re-map. Without this the test would pass just as
	// well against an analyzer that declined everything.
	Gains->SetDensityGain(TrueScope::EDensityScope::Waveform, 0.5f);
	TestEqual(TEXT("A Trace Brightness change still starts a re-map"),
		static_cast<int32>(Analyzer->RemapLastResultWithCurrentGains()),
		static_cast<int32>(TrueScope::ERedrawOutcome::Started));

	// Let the task finish before the analyzer goes out of scope.
	const double Deadline = FPlatformTime::Seconds() + 5.0;
	while (Analyzer->GetResultSerial() == SerialBefore && FPlatformTime::Seconds() < Deadline)
	{
		FPlatformProcess::Sleep(0.001f);
	}
	TestEqual(TEXT("The Trace Brightness re-map did publish"),
		static_cast<int64>(Analyzer->GetResultSerial()), static_cast<int64>(SerialBefore + 1));

	return true;
}

// ---------------------------------------------------------------------------
// 3. Requests coalesce
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeRedrawRequestCoalescingTest,
	"TrueScope.UI.RedrawRequestCoalescing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeRedrawRequestCoalescingTest::RunTest(const FString&)
{
	// A drag reports every mouse move — several per frame on a fast pointer.
	// The latch is a BIT, not a queue, precisely so those collapse: a
	// superseded gain has no value, and the producers read the gain when they
	// run, so the one re-render that happens always uses the newest value.
	TrueScope::FRedrawRequest Request;
	TestFalse(TEXT("A fresh latch is clear"), Request.IsPending());
	TestFalse(TEXT("Consuming a clear latch yields nothing"), Request.Consume());

	constexpr int32 EventsInOneFrame = 20;
	for (int32 Event = 0; Event < EventsInOneFrame; ++Event)
	{
		Request.Request();
	}
	TestTrue(TEXT("Twenty events in one frame leave one request"), Request.Consume());
	TestFalse(TEXT("...and only one"), Request.Consume());

	// The same through the module's own latch, which is what the per-frame
	// ticker services: N calls in a frame, one re-render.
	FTrueScopeModule& Module = FTrueScopeModule::Get();
	Module.ConsumeScopeRedrawRequestForTesting();
	for (int32 Event = 0; Event < EventsInOneFrame; ++Event)
	{
		Module.RequestScopeRedraw();
	}
	TestTrue(TEXT("The module coalesces the same way"),
		Module.ConsumeScopeRedrawRequestForTesting());
	TestFalse(TEXT("Nothing is left over for the next frame"),
		Module.IsScopeRedrawPending());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
