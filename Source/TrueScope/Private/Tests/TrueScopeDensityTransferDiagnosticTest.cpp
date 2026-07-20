// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "Analysis/TrueScopeAnalysis.h"
#include "Analysis/TrueScopeAnalysisResult.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * M6.3 diagnosis D1: measure TrueScope's own density count -> brightness
 * transfer by feeding known per-cell counts through the shipping path, and log
 * the table for offline fitting (linear-through-origin vs. power/log family).
 *
 * Two layers are exercised:
 *  1. The mapping helper itself (TrueScope::CountToIntensity) over the D1
 *     count sweep at MaxCount = 540 (a full column of a 960x540 capture).
 *  2. End-to-end through AnalyzePixels + BuildParadeImage: one column per
 *     sweep count, holding exactly that many pixels of value 128, asserting
 *     the intensity image texel equals the helper's value — proving the
 *     parade/waveform density images actually apply this mapping (the GPU
 *     shader repeats the same formula; the GPU-equivalence test covers that
 *     path).
 */

namespace
{
	using FResult = FTrueScopeAnalysisResult;

	/** D1 sweep: counts spanning 1 .. full column at 960x540 capture. */
	constexpr uint32 SweepCounts[] = {1, 2, 3, 4, 6, 8, 11, 16, 24, 32, 48, 64, 96, 128, 192, 270, 360, 540};
	constexpr uint32 SweepMaxCount = 540;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeDensityTransferDiagnosticTest,
	"TrueScope.Diagnostics.DensityTransfer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeDensityTransferDiagnosticTest::RunTest(const FString&)
{
	// Layer 1: the mapping helper, logged as the D1 count -> intensity table.
	UE_LOG(LogTemp, Display, TEXT("[D1] TrueScope density transfer, MaxCount=%u, Gain=%.1f"),
		SweepMaxCount, TrueScope::DensityIntensityGain);
	for (uint32 Count : SweepCounts)
	{
		const float Intensity = TrueScope::CountToIntensity(Count, SweepMaxCount);
		UE_LOG(LogTemp, Display, TEXT("[D1] Count=%3u Intensity=%.6f Byte=%d"),
			Count, Intensity, FMath::RoundToInt(255.0f * Intensity));

		// The mapping must be monotonic and hit exactly 1.0 at full column.
		TestTrue(TEXT("Intensity in (0,1]"), Intensity > 0.0f && Intensity <= 1.0f);
	}
	TestEqual(TEXT("Full column maps to 1.0"),
		TrueScope::CountToIntensity(SweepMaxCount, SweepMaxCount), 1.0f);
	TestEqual(TEXT("Zero count maps to 0.0"), TrueScope::CountToIntensity(0, SweepMaxCount), 0.0f);

	// Layer 2: end-to-end through the parade density image. One column per
	// sweep count; column i holds Counts[i] pixels of value 128 (bin 128),
	// the rest value 0 (bin 0), at source height 540 so the normalization
	// (MaxCount = source height) matches layer 1.
	constexpr int32 Width = UE_ARRAY_COUNT(SweepCounts);
	constexpr int32 Height = SweepMaxCount;
	TArray<FColor> Pixels;
	Pixels.Init(FColor(0, 0, 0, 255), Width * Height);
	for (int32 Column = 0; Column < Width; ++Column)
	{
		for (uint32 Row = 0; Row < SweepCounts[Column]; ++Row)
		{
			Pixels[Row * Width + Column] = FColor(128, 128, 128, 255);
		}
	}

	FResult Result;
	TrueScope::AnalyzePixels(Pixels, Width, Height, Result);
	TrueScope::BuildParadeImage(Result);

	// Value 128 -> bin 128 -> image row NumBins-1-128 (row 0 = top bin).
	const int32 Bin128Row = FResult::NumBins - 1 - 128;
	const TArray<uint8>& RedImage = Result.ParadeChannelIntensity[FResult::ChannelR];
	TestEqual(TEXT("Parade image sized"), RedImage.Num(), Width * FResult::NumBins);
	for (int32 Column = 0; Column < Width; ++Column)
	{
		const uint8 Expected = static_cast<uint8>(
			FMath::RoundToInt(255.0f * TrueScope::CountToIntensity(SweepCounts[Column], SweepMaxCount)));
		TestEqual(*FString::Printf(TEXT("Column %d (count %u) parade texel equals CountToIntensity"),
			Column, SweepCounts[Column]),
			RedImage[Bin128Row * Width + Column], Expected);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
