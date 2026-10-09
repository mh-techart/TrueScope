// Copyright 2026 Michael Herber
// SPDX-License-Identifier: Apache-2.0

#include "Analysis/TrueScopeAnalysis.h"
#include "Analysis/TrueScopeAnalysisResult.h"
#include "Analysis/TrueScopeColor.h"
#include "Misc/AutomationTest.h"
#include "UI/STrueScopeHistogram.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Histogram YRGB mode (phase-6.2 Task 2), headless: the slot layout (luma band
 * on top of R, G, B), the luma curve's provenance — its bins must be exactly
 * the column sums of the waveform's integer-exact luma bins, never a second
 * luma computation — and the shared-scale rule with luma participating.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeHistogramYrgbTest,
	"TrueScope.UI.HistogramYrgb",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeHistogramYrgbTest::RunTest(const FString&)
{
	using FResult = FTrueScopeAnalysisResult;

	// --- Slot layout -------------------------------------------------------
	TestEqual(TEXT("RGB separate draws 3 curves"),
		STrueScopeHistogram::GetNumCurveSlots(ETrueScopeHistogramMode::SeparateChannels), 3);
	TestEqual(TEXT("Overlay draws 3 curves"),
		STrueScopeHistogram::GetNumCurveSlots(ETrueScopeHistogramMode::RgbOverlay), 3);
	TestEqual(TEXT("YRGB draws 4 curves"),
		STrueScopeHistogram::GetNumCurveSlots(ETrueScopeHistogramMode::SeparateChannelsYrgb), 4);

	TestEqual(TEXT("YRGB slot 0 is the luma curve"),
		STrueScopeHistogram::SlotToChannel(ETrueScopeHistogramMode::SeparateChannelsYrgb, 0), (int32)INDEX_NONE);
	for (int32 Channel = 0; Channel < FResult::NumChannels; ++Channel)
	{
		TestEqual(*FString::Printf(TEXT("YRGB slot %d is channel %d"), Channel + 1, Channel),
			STrueScopeHistogram::SlotToChannel(ETrueScopeHistogramMode::SeparateChannelsYrgb, Channel + 1), Channel);
		TestEqual(*FString::Printf(TEXT("RGB slot %d is its own channel"), Channel),
			STrueScopeHistogram::SlotToChannel(ETrueScopeHistogramMode::SeparateChannels, Channel), Channel);
	}

	// Four equal, disjoint bands: luma from the top of the plot, blue to its
	// bottom — the four-way analogue of the RGB layout rule.
	const FSlateRect Plot(10.0f, 20.0f, 210.0f, 320.0f);
	FSlateRect Bands[FResult::NumYrgbCurves];
	for (int32 Slot = 0; Slot < FResult::NumYrgbCurves; ++Slot)
	{
		Bands[Slot] = STrueScopeHistogram::GetChannelPlotRect(Plot,
			ETrueScopeHistogramMode::SeparateChannelsYrgb, Slot);
		TestEqual(*FString::Printf(TEXT("YRGB band %d spans the plot width (left)"), Slot),
			Bands[Slot].Left, Plot.Left);
		TestEqual(*FString::Printf(TEXT("YRGB band %d spans the plot width (right)"), Slot),
			Bands[Slot].Right, Plot.Right);
		if (Slot > 0)
		{
			TestTrue(*FString::Printf(TEXT("YRGB band %d sits below band %d"), Slot, Slot - 1),
				Bands[Slot - 1].Bottom < Bands[Slot].Top);
			TestEqual(*FString::Printf(TEXT("YRGB band %d has the shared band height"), Slot),
				Bands[Slot].GetSize().Y, Bands[0].GetSize().Y, 0.01f);
		}
	}
	TestEqual(TEXT("Luma band starts at the top of the plot"), Bands[0].Top, Plot.Top);
	TestEqual(TEXT("Blue band ends at the bottom of the plot"),
		Bands[FResult::NumYrgbCurves - 1].Bottom, Plot.Bottom);
	TestTrue(TEXT("YRGB bands are shorter than RGB bands (four-way split)"),
		Bands[0].GetSize().Y
			< STrueScopeHistogram::GetChannelPlotRect(Plot, ETrueScopeHistogramMode::SeparateChannels, 0)
				.GetSize().Y);

	// --- Luma bin provenance ----------------------------------------------
	// A gradient with DISTINCT channel values per column, so any wiring slip
	// (a colour channel drawn as luma, a transposed sum) shifts peaks and
	// fails. The reference is computed straight from PixelToLumaBin — the same
	// integer-exact function the waveform's analysis bins through.
	constexpr int32 Width = 64;
	constexpr int32 Height = 32;
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Width * Height);
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const uint8 Value = static_cast<uint8>((X * 255) / (Width - 1));
			Pixels[Y * Width + X] = FColor(Value, 255 - Value, Value / 2, 255);
		}
	}

	FResult Result;
	TrueScope::AnalyzePixels(Pixels, Width, Height, Result);

	const TArray<uint32> LumaBins = STrueScopeHistogram::BuildLumaHistogramBins(Result);
	TestEqual(TEXT("Luma histogram carries one count per value bin"), LumaBins.Num(), FResult::NumBins);

	TArray<uint32> ExpectedLumaBins;
	ExpectedLumaBins.SetNumZeroed(FResult::NumBins);
	for (const FColor& Pixel : Pixels)
	{
		++ExpectedLumaBins[TrueScope::PixelToLumaBin(Pixel)];
	}
	bool bBinsEqual = LumaBins.Num() == ExpectedLumaBins.Num();
	for (int32 Bin = 0; bBinsEqual && Bin < FResult::NumBins; ++Bin)
	{
		bBinsEqual = LumaBins[Bin] == ExpectedLumaBins[Bin];
	}
	TestTrue(TEXT("Luma histogram equals the per-pixel integer-exact luma binning"), bBinsEqual);

	// ...and, by the same token, the column sums of the waveform's LumaParade:
	// the provenance the phase-6.2 acceptance names explicitly.
	TArray<uint32> WaveformColumnSums;
	WaveformColumnSums.SetNumZeroed(FResult::NumBins);
	for (int32 Column = 0; Column < Width; ++Column)
	{
		for (int32 Bin = 0; Bin < FResult::NumBins; ++Bin)
		{
			WaveformColumnSums[Bin] += Result.LumaParade[Column * FResult::NumBins + Bin];
		}
	}
	bool bSumsEqual = true;
	for (int32 Bin = 0; bSumsEqual && Bin < FResult::NumBins; ++Bin)
	{
		bSumsEqual = LumaBins[Bin] == WaveformColumnSums[Bin];
	}
	TestTrue(TEXT("Luma histogram equals the waveform's luma bins, summed over columns"), bSumsEqual);

	// --- Shared scale with luma participating (M6.3) -----------------------
	// Since the clip threshold is FIXED, luma and the colour channels are on one
	// scale by construction: the scale is a property of the frame size and the
	// gain, not of any curve's peak. Both halves of that are asserted, because
	// the pre-M6.3 bug class was exactly a mode ending up on a scale of its own.
	const float Threshold =
		STrueScopeHistogram::GetClipThresholdCount(Result, TrueScope::DefaultGainMultiplier);
	TestTrue(TEXT("The clip threshold is positive for a non-empty frame"), Threshold > 0.0f);

	// A curve that suddenly towers must NOT rescale the others — the whole
	// point of a fixed threshold. Inflate the strongest colour bin far past the
	// threshold in a copy and check both the scale and a quiet bin's height.
	const uint32 RgbMax = STrueScopeHistogram::GetSharedMaxCount(Result);
	constexpr int32 QuietBin = 3;
	const uint32 QuietCount = Result.Histogram[FResult::ChannelR][QuietBin];
	const float QuietHeight = STrueScopeHistogram::GetCurveHeight(QuietCount, Threshold);

	FResult TowerResult = Result;
	TowerResult.Histogram[FResult::ChannelR][10] = RgbMax + 100000;
	const float TowerThreshold =
		STrueScopeHistogram::GetClipThresholdCount(TowerResult, TrueScope::DefaultGainMultiplier);
	TestEqual(TEXT("A towering bin does not move the scale"), TowerThreshold, Threshold);
	TestEqual(TEXT("A quiet bin keeps its height when another curve towers"),
		STrueScopeHistogram::GetCurveHeight(QuietCount, TowerThreshold), QuietHeight);
	TestEqual(TEXT("The towering bin clips at the band top instead"),
		STrueScopeHistogram::GetCurveHeight(TowerResult.Histogram[FResult::ChannelR][10], TowerThreshold), 1.0f);

	// Luma is on that same one scale: an equal count draws an equal height, and
	// nothing about the luma curve enters the threshold.
	// Stated against the formula rather than against another call of the same
	// helper: comparing GetCurveHeight to itself would pass no matter what the
	// YRGB mode did.
	uint32 BusiestLumaBin = 0;
	for (int32 Bin = 0; Bin < FResult::NumBins; ++Bin)
	{
		BusiestLumaBin = FMath::Max(BusiestLumaBin, LumaBins[Bin]);
	}
	TestTrue(TEXT("The synthetic frame has a luma bin worth measuring"), BusiestLumaBin > 0);
	TestEqual(TEXT("A luma bin draws at count / threshold, the same law as a colour bin"),
		STrueScopeHistogram::GetCurveHeight(BusiestLumaBin, Threshold),
		FMath::Min(static_cast<float>(BusiestLumaBin) / Threshold, 1.0f), 1e-6f);

	TArray<uint32> TallLumaBins = LumaBins;
	TallLumaBins[100] = RgbMax + 100000;
	TestEqual(TEXT("A towering luma bin clips at the band top like a colour bin"),
		STrueScopeHistogram::GetCurveHeight(TallLumaBins[100], Threshold), 1.0f);

	// The scale depends only on the analysed frame's pixel count and the gain.
	FResult HalfFrame = Result;
	HalfFrame.SourceSize.Y = Result.SourceSize.Y / 2;
	TestEqual(TEXT("Half the frame halves the clip threshold"),
		STrueScopeHistogram::GetClipThresholdCount(HalfFrame, TrueScope::DefaultGainMultiplier),
		0.5f * Threshold, 0.001f * Threshold);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
