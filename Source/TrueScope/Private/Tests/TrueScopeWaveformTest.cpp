// Copyright 2026 Michael Herber
// SPDX-License-Identifier: Apache-2.0

#include "Analysis/TrueScopeAnalysis.h"
#include "Analysis/TrueScopeAnalysisResult.h"
#include "Misc/AutomationTest.h"
#include "UI/STrueScopeWaveform.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The waveform's two display modes. One test covers the luma maths, the other
 * covers which of the two datasets a mode actually draws — the "data lands in
 * the wrong curve" layer that the M4 histogram bug lived in, and that the
 * analysis-only tests cannot see.
 */

namespace
{
	using FResult = FTrueScopeAnalysisResult;

	/** The single occupied luma bin of one column, or INDEX_NONE otherwise. */
	int32 LumaColumnSinglePeakBin(const FResult& Result, int32 Column)
	{
		const int32 Base = Column * FResult::NumBins;
		int32 PeakBin = INDEX_NONE;
		for (int32 Bin = 0; Bin < FResult::NumBins; ++Bin)
		{
			if (Result.LumaParade[Base + Bin] > 0)
			{
				if (PeakBin != INDEX_NONE)
				{
					return INDEX_NONE;
				}
				PeakBin = Bin;
			}
		}
		return PeakBin;
	}

	/** Luma bin a flat frame of one colour lands in. */
	int32 SolidLumaBin(const FColor& Solid)
	{
		constexpr int32 Width = 8;
		constexpr int32 Height = 4;
		TArray<FColor> Pixels;
		Pixels.Init(Solid, Width * Height);

		FResult Result;
		TrueScope::AnalyzePixels(Pixels, Width, Height, Result);
		return LumaColumnSinglePeakBin(Result, 0);
	}

	/** Row of the intensity/texel image a value bin is drawn on (row 0 = top). */
	int32 RowOfBin(int32 Bin)
	{
		return (FResult::NumBins - 1) - Bin;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeLumaWaveformTest,
	"TrueScope.Analysis.LumaWaveform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeLumaWaveformTest::RunTest(const FString&)
{
	// Expected bins come from the published BT.709 weights, not from the RGB
	// path: round(0.2126*255)=54, round(0.7152*255)=182, round(0.0722*255)=18.
	// A luma that merely copied a colour channel would land on 0 or 255 here.
	TestEqual(TEXT("Black has luma 0"), SolidLumaBin(FColor(0, 0, 0, 255)), 0);
	TestEqual(TEXT("White has full luma"), SolidLumaBin(FColor(255, 255, 255, 255)), FResult::NumBins - 1);
	TestEqual(TEXT("Mid gray bins on its own code value"), SolidLumaBin(FColor(128, 128, 128, 255)), 128);

	TestEqual(TEXT("Pure red luma is 0.2126"), SolidLumaBin(FColor(255, 0, 0, 255)), 54);
	TestEqual(TEXT("Pure green luma is 0.7152"), SolidLumaBin(FColor(0, 255, 0, 255)), 182);
	TestEqual(TEXT("Pure blue luma is 0.0722"), SolidLumaBin(FColor(0, 0, 255, 255)), 18);

	TestEqual(TEXT("Yellow luma is 0.2126+0.7152"), SolidLumaBin(FColor(255, 255, 0, 255)), 237);
	TestEqual(TEXT("Cyan luma is 0.7152+0.0722"), SolidLumaBin(FColor(0, 255, 255, 255)), 201);
	TestEqual(TEXT("Magenta luma is 0.2126+0.0722"), SolidLumaBin(FColor(255, 0, 255, 255)), 73);

	// Green is by far the brightest primary and blue the darkest — the whole
	// reason a luma waveform reads exposure differently from any single channel.
	TestTrue(TEXT("Green outweighs red, red outweighs blue"),
		SolidLumaBin(FColor(0, 255, 0, 255)) > SolidLumaBin(FColor(255, 0, 0, 255))
			&& SolidLumaBin(FColor(255, 0, 0, 255)) > SolidLumaBin(FColor(0, 0, 255, 255)));

	// A neutral horizontal gradient: luma equals the code value, so the luma
	// waveform draws the same diagonal the RGB one does.
	{
		constexpr int32 Width = 256;
		constexpr int32 Height = 8;
		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Width * Height);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const uint8 V = (uint8)X;
				Pixels[Y * Width + X] = FColor(V, V, V, 255);
			}
		}

		FResult Result;
		TrueScope::AnalyzePixels(Pixels, Width, Height, Result);

		bool bDiagonal = true;
		for (int32 Column = 0; Column < Width; ++Column)
		{
			if (LumaColumnSinglePeakBin(Result, Column) != Column)
			{
				bDiagonal = false;
				break;
			}
		}
		TestTrue(TEXT("A neutral gradient traces the diagonal in luma too"), bDiagonal);
		TestEqual(TEXT("Every pixel of a column lands in one luma bin"),
			Result.LumaParade[128 * FResult::NumBins + 128], (uint32)Height);
	}

	// A red ramp: luma follows 0.2126 * value, so the trace is a shallower
	// diagonal than the red channel's own.
	{
		constexpr int32 Width = 256;
		constexpr int32 Height = 4;
		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Width * Height);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				Pixels[Y * Width + X] = FColor((uint8)X, 0, 0, 255);
			}
		}

		FResult Result;
		TrueScope::AnalyzePixels(Pixels, Width, Height, Result);

		bool bWeighted = true;
		for (int32 Column = 0; Column < Width; ++Column)
		{
			const int32 Expected = FMath::RoundToInt32(0.2126f * Column);
			if (LumaColumnSinglePeakBin(Result, Column) != Expected)
			{
				bWeighted = false;
				break;
			}
		}
		TestTrue(TEXT("A red ramp's luma follows the 0.2126 weight"), bWeighted);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeWaveformModeSelectionTest,
	"TrueScope.UI.WaveformModeSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeWaveformModeSelectionTest::RunTest(const FString&)
{
	// Pure green separates the two modes completely: the RGB trace puts green at
	// the top of the plot (bin 255) and red/blue at the bottom (bin 0), while the
	// luma trace sits alone at bin 182. Serving the wrong dataset for a mode
	// therefore lights up a row the other mode leaves black.
	constexpr int32 Width = 32;
	constexpr int32 Height = 16;
	constexpr int32 LumaBin = 182;   // round(0.7152 * 255)

	TArray<FColor> Pixels;
	Pixels.Init(FColor(0, 255, 0, 255), Width * Height);

	FResult Result;
	TrueScope::AnalyzePixels(Pixels, Width, Height, Result);
	TrueScope::BuildParadeImage(Result);
	TrueScope::BuildLumaWaveformImage(Result);

	const int32 NumTexels = STrueScopeWaveform::GetTexelCount(Result);
	TestEqual(TEXT("The trace covers one texel per column and bin"), NumTexels, Width * FResult::NumBins);

	TArray<FColor> RgbTexels;
	RgbTexels.SetNumUninitialized(NumTexels);
	constexpr float WaveformGain = 1.0f;   // the calibrated default, unscaled
	STrueScopeWaveform::BuildTexels(Result, ETrueScopeWaveformMode::Rgb, WaveformGain, RgbTexels);

	TArray<FColor> LumaTexels;
	LumaTexels.SetNumUninitialized(NumTexels);
	STrueScopeWaveform::BuildTexels(Result, ETrueScopeWaveformMode::Luma, WaveformGain, LumaTexels);

	auto TexelAt = [](const TArray<FColor>& Texels, int32 Bin)
	{
		return Texels[RowOfBin(Bin) * Width + Width / 2];
	};

	// RGB mode: green at the top in its own colour, nothing on the luma row.
	const FColor RgbTop = TexelAt(RgbTexels, FResult::NumBins - 1);
	TestTrue(TEXT("RGB mode traces green at bin 255"), RgbTop.G > 0);
	TestTrue(TEXT("RGB mode keeps that trace green, not neutral"), RgbTop.R == 0 && RgbTop.B == 0);
	const FColor RgbAtLumaBin = TexelAt(RgbTexels, LumaBin);
	TestTrue(TEXT("RGB mode draws nothing at the luma bin"),
		RgbAtLumaBin.R == 0 && RgbAtLumaBin.G == 0 && RgbAtLumaBin.B == 0);

	// Luma mode: one neutral trace at bin 182, nothing where the channels sit.
	const FColor LumaTrace = TexelAt(LumaTexels, LumaBin);
	TestTrue(TEXT("Luma mode traces at bin 182"), LumaTrace.G > 0);
	TestTrue(TEXT("Luma mode's trace is neutral"),
		LumaTrace.R == LumaTrace.G && LumaTrace.G == LumaTrace.B);

	const FColor LumaAtGreenBin = TexelAt(LumaTexels, FResult::NumBins - 1);
	TestTrue(TEXT("Luma mode draws nothing at the green channel's bin"),
		LumaAtGreenBin.R == 0 && LumaAtGreenBin.G == 0 && LumaAtGreenBin.B == 0);
	const FColor LumaAtZeroBin = TexelAt(LumaTexels, 0);
	TestTrue(TEXT("Luma mode draws nothing at the red/blue channels' bin"),
		LumaAtZeroBin.R == 0 && LumaAtZeroBin.G == 0 && LumaAtZeroBin.B == 0);

	// ...and the two modes really are different pictures.
	TestTrue(TEXT("The two modes produce different traces"), RgbTexels != LumaTexels);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
