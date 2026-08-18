// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "Analysis/TrueScopeAnalysis.h"
#include "Analysis/TrueScopeAnalysisResult.h"
#include "Analysis/TrueScopeDisplayGains.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * M6.3: the density count -> brightness transfer is LINEAR in the count and
 * hard-clips at white — Intensity = saturate(Count * Gain / MaxCount).
 *
 * This file began as the D1 diagnostic that measured the old log transfer;
 * since the calibration it pins the shipping one instead. Three layers:
 *  1. TrueScope::CountToIntensity itself: proportionality (double the count,
 *     double the brightness), the exact clip point, and the gain's effect.
 *  2. End-to-end through AnalyzePixels + BuildParadeImage, so the parade and
 *     waveform density images are proven to apply this transfer and not some
 *     other curve after it (the layer both M4 bugs lived in).
 *  3. Resolution independence: the same fraction of a column draws the same
 *     brightness whatever the capture height is — the transfer normalises by
 *     the column height, so a scope does not change appearance when the
 *     capture size does.
 *
 * The GPU shader repeats the same formula (TrueScopeParade.usf,
 * TrueScopeVectorscope.usf); the GPU-equivalence test covers that path.
 */

namespace
{
	using FResult = FTrueScopeAnalysisResult;

	/** Counts spanning 1 .. full column at 960x540 capture. */
	constexpr uint32 SweepCounts[] = {1, 2, 3, 4, 6, 8, 11, 16, 24, 32, 48, 64, 96, 128, 192, 270, 540};
	constexpr uint32 SweepMaxCount = 540;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeDensityTransferDiagnosticTest,
	"TrueScope.Diagnostics.DensityTransfer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeDensityTransferDiagnosticTest::RunTest(const FString&)
{
	constexpr float UnityGain = 1.0f;

	// --- Layer 1: the mapping helper ---------------------------------------
	for (uint32 Count : SweepCounts)
	{
		const float Intensity = TrueScope::CountToIntensity(Count, SweepMaxCount, UnityGain);
		const float Expected = static_cast<float>(Count) / static_cast<float>(SweepMaxCount);
		TestEqual(*FString::Printf(TEXT("Count %u maps linearly"), Count), Intensity, Expected, 1e-6f);
	}

	// Proportionality stated as a relation, so it fails if the curve bends even
	// where the table above happens to be sparse.
	TestEqual(TEXT("Doubling the count doubles the brightness"),
		TrueScope::CountToIntensity(2 * 37, SweepMaxCount, UnityGain),
		2.0f * TrueScope::CountToIntensity(37, SweepMaxCount, UnityGain), 1e-6f);

	TestEqual(TEXT("Zero count is black"), TrueScope::CountToIntensity(0, SweepMaxCount, UnityGain), 0.0f);
	TestEqual(TEXT("A full column is white"),
		TrueScope::CountToIntensity(SweepMaxCount, SweepMaxCount, UnityGain), 1.0f);

	// The clip is HARD: past white nothing gets brighter (Resolve's behaviour).
	TestEqual(TEXT("Past the clip point the trace stays white"),
		TrueScope::CountToIntensity(SweepMaxCount + 1, SweepMaxCount, UnityGain), 1.0f);
	TestEqual(TEXT("Far past the clip point it is still just white"),
		TrueScope::CountToIntensity(10 * SweepMaxCount, SweepMaxCount, UnityGain), 1.0f);

	// Gain multiplies the count, so it moves the clip point by its reciprocal.
	TestEqual(TEXT("Gain 2 doubles the brightness of a mid count"),
		TrueScope::CountToIntensity(100, SweepMaxCount, 2.0f),
		2.0f * TrueScope::CountToIntensity(100, SweepMaxCount, UnityGain), 1e-6f);
	TestEqual(TEXT("Gain 2 clips at half a column"),
		TrueScope::CountToIntensity(SweepMaxCount / 2, SweepMaxCount, 2.0f), 1.0f);
	TestEqual(TEXT("Gain 0 draws nothing"), TrueScope::CountToIntensity(100, SweepMaxCount, 0.0f), 0.0f);

	// --- Layer 2: end-to-end through the parade density image --------------
	// One column per sweep count; column i holds Counts[i] pixels of value 128
	// (bin 128), the rest value 0, at source height 540 so the normalization
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
		const float DefaultGain = TrueScope::GetDefaultDensityGain(TrueScope::EDensityScope::Parade);
		const uint8 Expected = static_cast<uint8>(FMath::RoundToInt(
			255.0f * TrueScope::CountToIntensity(SweepCounts[Column], SweepMaxCount, DefaultGain)));
		TestEqual(*FString::Printf(TEXT("Column %d (count %u) parade texel equals CountToIntensity"),
			Column, SweepCounts[Column]),
			RedImage[Bin128Row * Width + Column], Expected);
	}

	// The image really is linear in the count end-to-end, not merely equal to a
	// helper that could itself have bent: a column holding twice the pixels of
	// another draws twice as bright (both well under the clip point).
	// Counts are chosen large enough that 8-bit quantisation (+/-1 code value,
	// asserted as the tolerance) cannot hide a bent curve: the retired log
	// transfer, log(1+16C)/log(1+16*MaxCount), would have drawn these two
	// columns at 206 and 226 rather than 45 and 91 — nowhere near a doubling.
	// static_assert, not check(): a failed check() aborts the whole automation
	// run rather than failing this one test, which is how a mistyped index here
	// took the suite down with it.
	constexpr int32 ColumnOf96 = 12;
	constexpr int32 ColumnOf192 = 14;
	static_assert(SweepCounts[ColumnOf96] == 96, "ColumnOf96 must index the count 96");
	static_assert(SweepCounts[ColumnOf192] == 192, "ColumnOf192 must index the count 192");
	const int32 Bright96 = RedImage[Bin128Row * Width + ColumnOf96];
	const int32 Bright192 = RedImage[Bin128Row * Width + ColumnOf192];
	TestTrue(*FString::Printf(
			TEXT("Twice the pixels draw twice as bright, end to end (%d vs %d)"), Bright192, Bright96),
		FMath::Abs(Bright192 - 2 * Bright96) <= 1);

	// --- Layer 3: resolution independence ----------------------------------
	// The same FRACTION of a column must draw the same brightness at any
	// capture height, because the transfer normalises by the column height.
	TestEqual(TEXT("A tenth of a column is the same brightness at any height"),
		TrueScope::CountToIntensity(54, 540, UnityGain),
		TrueScope::CountToIntensity(108, 1080, UnityGain), 1e-6f);
	TestEqual(TEXT("...and at a third height again"),
		TrueScope::CountToIntensity(27, 270, UnityGain),
		TrueScope::CountToIntensity(54, 540, UnityGain), 1e-6f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
