// Copyright 2026 TrueScope contributors
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

	// --- Shared scale with luma participating ------------------------------
	// The gradient's luma concentrates counts (Height per column) exactly like
	// the channels, but build an explicit case: a luma peak larger than any
	// colour peak must raise the YRGB maximum and leave the RGB maximum alone.
	const uint32 RgbMax = STrueScopeHistogram::GetSharedMaxCount(Result);
	TArray<uint32> TallLumaBins = LumaBins;
	TallLumaBins[100] = RgbMax + 1000;
	TestEqual(TEXT("A dominant luma peak sets the YRGB shared maximum"),
		STrueScopeHistogram::GetYrgbSharedMaxCount(Result, TallLumaBins), RgbMax + 1000);
	TestEqual(TEXT("A weak luma curve leaves the shared maximum at the strongest colour channel"),
		STrueScopeHistogram::GetYrgbSharedMaxCount(Result, TArrayView<const uint32>()), RgbMax);
	TestEqual(TEXT("The RGB modes' shared maximum never includes luma"),
		STrueScopeHistogram::GetSharedMaxCount(Result), RgbMax);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
