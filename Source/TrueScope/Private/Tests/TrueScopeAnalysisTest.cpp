// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "Analysis/TrueScopeAnalysis.h"
#include "Analysis/TrueScopeAnalysisResult.h"
#include "Analysis/TrueScopeColor.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using FResult = FTrueScopeAnalysisResult;

	/** Sum of a channel histogram — must always equal the pixel count. */
	uint32 SumHistogram(const FResult& Result, int32 Channel)
	{
		uint32 Sum = 0;
		for (uint32 Count : Result.Histogram[Channel])
		{
			Sum += Count;
		}
		return Sum;
	}

	/** Bin holding the single non-zero count, or INDEX_NONE if not a single peak. */
	int32 SinglePeakBin(const FResult& Result, int32 Channel)
	{
		int32 PeakBin = INDEX_NONE;
		for (int32 Bin = 0; Bin < FResult::NumBins; ++Bin)
		{
			if (Result.Histogram[Channel][Bin] > 0)
			{
				if (PeakBin != INDEX_NONE)
				{
					return INDEX_NONE; // more than one occupied bin
				}
				PeakBin = Bin;
			}
		}
		return PeakBin;
	}

	/** The single occupied bin of one parade column, or INDEX_NONE otherwise. */
	int32 ColumnSinglePeakBin(const FResult& Result, int32 Channel, int32 Column)
	{
		const int32 Base = Column * FResult::NumBins;
		int32 PeakBin = INDEX_NONE;
		for (int32 Bin = 0; Bin < FResult::NumBins; ++Bin)
		{
			if (Result.Parade[Channel][Base + Bin] > 0)
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

	/** Number of occupied bins in one parade column. */
	int32 ColumnOccupiedBins(const FResult& Result, int32 Channel, int32 Column)
	{
		const int32 Base = Column * FResult::NumBins;
		int32 Occupied = 0;
		for (int32 Bin = 0; Bin < FResult::NumBins; ++Bin)
		{
			Occupied += (Result.Parade[Channel][Base + Bin] > 0) ? 1 : 0;
		}
		return Occupied;
	}

	/**
	 * True iff the channel's parade intensity image has content on ExpectRow
	 * and *only* there — i.e. every other row sums to zero. Catches the parade
	 * channel-bleed bug: a block that showed another channel's trace would have
	 * a non-zero row where that other channel's values land.
	 */
	bool TraceOnlyOnRow(const FResult& Result, int32 Channel, int32 ExpectRow)
	{
		const int32 Width = Result.ParadeImageSize.X;
		const int32 Height = Result.ParadeImageSize.Y;
		const TArray<uint8>& Image = Result.ParadeChannelIntensity[Channel];
		if (Image.Num() != Width * Height)
		{
			return false;
		}
		for (int32 Row = 0; Row < Height; ++Row)
		{
			uint32 RowSum = 0;
			for (int32 Column = 0; Column < Width; ++Column)
			{
				RowSum += Image[Row * Width + Column];
			}
			if (Row == ExpectRow)
			{
				if (RowSum == 0) { return false; } // expected trace missing
			}
			else if (RowSum != 0)
			{
				return false; // stray content on a row that must be empty
			}
		}
		return true;
	}

	/**
	 * Runs the pure-channel no-bleed assertions for a solid image of one
	 * primary. The "on" channel is full (value 255 -> top row 0); the two
	 * "off" channels are zero (value 0 -> bottom row Height-1). Each block must
	 * carry a trace on exactly its own row and nowhere else.
	 */
	void CheckPureChannelNoBleed(FAutomationTestBase& Test, int32 OnChannel, const FColor& Solid)
	{
		constexpr int32 Width = 40;
		constexpr int32 Height = 24;
		TArray<FColor> Pixels;
		Pixels.Init(Solid, Width * Height);

		FResult Result;
		TrueScope::AnalyzePixels(Pixels, Width, Height, Result);
		TrueScope::BuildParadeImage(Result);

		const int32 TopRow = 0;                          // value 255 -> highest bin
		const int32 BottomRow = FResult::NumBins - 1;    // value 0 -> lowest bin
		for (int32 Channel = 0; Channel < FResult::NumChannels; ++Channel)
		{
			const int32 ExpectRow = (Channel == OnChannel) ? TopRow : BottomRow;
			Test.TestTrue(
				*FString::Printf(TEXT("Channel %d trace only on row %d (no bleed)"), Channel, ExpectRow),
				TraceOnlyOnRow(Result, Channel, ExpectRow));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeSolidRedTest,
	"TrueScope.Analysis.SolidRed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeSolidRedTest::RunTest(const FString&)
{
	// Solid red (255,0,0): histogram = single peak in R at the top bin, G/B empty.
	constexpr int32 Width = 64;
	constexpr int32 Height = 32;
	TArray<FColor> Pixels;
	Pixels.Init(FColor(255, 0, 0, 255), Width * Height);

	FResult Result;
	TrueScope::AnalyzePixels(Pixels, Width, Height, Result);

	TestEqual(TEXT("R histogram sums to pixel count"), SumHistogram(Result, FResult::ChannelR), (uint32)(Width * Height));
	TestEqual(TEXT("R peak at top bin"), SinglePeakBin(Result, FResult::ChannelR), FResult::NumBins - 1);
	TestEqual(TEXT("G peak at bottom bin"), SinglePeakBin(Result, FResult::ChannelG), 0);
	TestEqual(TEXT("B peak at bottom bin"), SinglePeakBin(Result, FResult::ChannelB), 0);
	// G and B are entirely zero except bin 0 (which holds every pixel).
	TestEqual(TEXT("G bin 0 holds all pixels"), Result.Histogram[FResult::ChannelG][0], (uint32)(Width * Height));
	TestEqual(TEXT("B bin 0 holds all pixels"), Result.Histogram[FResult::ChannelB][0], (uint32)(Width * Height));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeHorizontalGradientTest,
	"TrueScope.Analysis.HorizontalGradient",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeHorizontalGradientTest::RunTest(const FString&)
{
	// Black left -> white right: within a column every pixel shares one value,
	// so each column's parade is a single peak whose bin rises with the column
	// (diagonal bottom-left -> top-right). Width 256 makes column == value.
	constexpr int32 Width = 256;
	constexpr int32 Height = 32;
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
		if (ColumnSinglePeakBin(Result, FResult::ChannelR, Column) != Column)
		{
			bDiagonal = false;
			break;
		}
	}
	TestTrue(TEXT("Each column is a single peak at bin == column (diagonal)"), bDiagonal);
	// Each column's peak holds the whole column height.
	TestEqual(TEXT("Column 128 count equals height"),
		Result.Parade[FResult::ChannelR][128 * FResult::NumBins + 128], (uint32)Height);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeVerticalGradientTest,
	"TrueScope.Analysis.VerticalGradient",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeVerticalGradientTest::RunTest(const FString&)
{
	// Black top -> white bottom: within a column the value spans the full
	// range across rows, so every column occupies every distinct value bin —
	// full-height bars across the width. Proves column aggregation sums all
	// rows, not one scanline. Height 256 gives one distinct value per row.
	constexpr int32 Width = 48;
	constexpr int32 Height = 256;
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Width * Height);
	for (int32 Y = 0; Y < Height; ++Y)
	{
		const uint8 V = (uint8)Y;
		for (int32 X = 0; X < Width; ++X)
		{
			Pixels[Y * Width + X] = FColor(V, V, V, 255);
		}
	}

	FResult Result;
	TrueScope::AnalyzePixels(Pixels, Width, Height, Result);

	bool bAllColumnsFullRange = true;
	for (int32 Column = 0; Column < Width; ++Column)
	{
		// 256 distinct row values -> all 256 bins occupied, one pixel each.
		if (ColumnOccupiedBins(Result, FResult::ChannelR, Column) != FResult::NumBins)
		{
			bAllColumnsFullRange = false;
			break;
		}
	}
	TestTrue(TEXT("Every column spans the full value range"), bAllColumnsFullRange);
	TestEqual(TEXT("Column 0 bin 200 has exactly one pixel"),
		Result.Parade[FResult::ChannelR][0 * FResult::NumBins + 200], (uint32)1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeFlatMidGrayTest,
	"TrueScope.Analysis.FlatMidGray",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeFlatMidGrayTest::RunTest(const FString&)
{
	// Flat mid-gray (~50%): histogram = single centered peak all channels;
	// parade = flat horizontal line at mid-height (every column single peak at
	// the same bin). Proves even values concentrate, no smearing.
	constexpr int32 Width = 64;
	constexpr int32 Height = 64;
	constexpr uint8 Mid = 128;
	TArray<FColor> Pixels;
	Pixels.Init(FColor(Mid, Mid, Mid, 255), Width * Height);

	FResult Result;
	TrueScope::AnalyzePixels(Pixels, Width, Height, Result);

	for (int32 Channel = 0; Channel < FResult::NumChannels; ++Channel)
	{
		TestEqual(*FString::Printf(TEXT("Channel %d single centered peak"), Channel),
			SinglePeakBin(Result, Channel), (int32)Mid);
	}
	// Parade: every column is a single peak at the mid bin (flat line).
	bool bFlatLine = true;
	for (int32 Column = 0; Column < Width; ++Column)
	{
		if (ColumnSinglePeakBin(Result, FResult::ChannelG, Column) != (int32)Mid)
		{
			bFlatLine = false;
			break;
		}
	}
	TestTrue(TEXT("Parade is a flat horizontal line at mid-height"), bFlatLine);

	return true;
}

// Regression tests for the parade channel-bleed bug: each parade block must
// show only its own channel. A solid primary gives the other two channels no
// content to bleed except at value 0 (their own legitimate baseline row), so
// any non-empty row elsewhere in an "off" block is a bleed. The earlier
// solid-red test alone missed this — a pure-red image has no green/blue to
// bleed anywhere.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeParadePureRedNoBleedTest,
	"TrueScope.Analysis.ParadePureRedNoBleed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeParadePureRedNoBleedTest::RunTest(const FString&)
{
	// Pure red: R block trace at top, G and B blocks empty (only their value-0
	// baseline), no red bleeding into them.
	CheckPureChannelNoBleed(*this, FResult::ChannelR, FColor(255, 0, 0, 255));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeParadePureGreenNoBleedTest,
	"TrueScope.Analysis.ParadePureGreenNoBleed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeParadePureGreenNoBleedTest::RunTest(const FString&)
{
	CheckPureChannelNoBleed(*this, FResult::ChannelG, FColor(0, 255, 0, 255));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeParadePureBlueNoBleedTest,
	"TrueScope.Analysis.ParadePureBlueNoBleed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeParadePureBlueNoBleedTest::RunTest(const FString&)
{
	// The block that showed the worst bleed in the reported bug: pure blue must
	// leave the R and G blocks empty (no red/green traces behind the blue).
	CheckPureChannelNoBleed(*this, FResult::ChannelB, FColor(0, 0, 255, 255));
	return true;
}

// ---------------------------------------------------------------------------
// M4: BT.709 chroma math, vectorscope, waveform, histogram channels.
// ---------------------------------------------------------------------------

namespace
{
	/** 75 % of full scale in 8-bit — the amplitude of the standard color bars. */
	constexpr uint8 Bar75 = 191;

	/** Number of bars in the standard pattern; the image width must divide by it. */
	constexpr int32 NumBars = 8;

	/**
	 * Standard 75 % color bars, left to right. The middle six are the chroma
	 * bars the vectorscope graticule targets; white and black are neutral and
	 * must land dead center.
	 */
	const FColor BarColors[NumBars] = {
		FColor(Bar75, Bar75, Bar75, 255), // white 75 %
		FColor(Bar75, Bar75, 0, 255),     // yellow
		FColor(0, Bar75, Bar75, 255),     // cyan
		FColor(0, Bar75, 0, 255),         // green
		FColor(Bar75, 0, Bar75, 255),     // magenta
		FColor(Bar75, 0, 0, 255),         // red
		FColor(0, 0, Bar75, 255),         // blue
		FColor(0, 0, 0, 255),             // black
	};

	/** Renders the 75 % bars; Width must be a multiple of NumBars. */
	TArray<FColor> MakeColorBars(int32 Width, int32 Height)
	{
		const int32 BarWidth = Width / NumBars;
		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Width * Height);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				Pixels[Y * Width + X] = BarColors[FMath::Min(X / BarWidth, NumBars - 1)];
			}
		}
		return Pixels;
	}

	/** Total vectorscope count in the (2*Radius+1)^2 bin box around a target. */
	uint32 SumVectorscopeBox(const FResult& Result, int32 CbBin, int32 CrBin, int32 Radius)
	{
		uint32 Sum = 0;
		for (int32 Cr = CrBin - Radius; Cr <= CrBin + Radius; ++Cr)
		{
			for (int32 Cb = CbBin - Radius; Cb <= CbBin + Radius; ++Cb)
			{
				if (Cr >= 0 && Cr < FResult::NumVectorBins && Cb >= 0 && Cb < FResult::NumVectorBins)
				{
					Sum += Result.Vectorscope[Cr * FResult::NumVectorBins + Cb];
				}
			}
		}
		return Sum;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeChroma709Test,
	"TrueScope.Analysis.Chroma709",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeChroma709Test::RunTest(const FString&)
{
	// Ground truth straight from the published BT.709 matrix — an independent
	// check on RgbToChroma that the graticule/analysis tests cannot provide,
	// since those derive their expectations from the same function.
	constexpr float Tolerance = 1.0e-4f;

	auto CheckChroma = [this](const TCHAR* What, TrueScope::FChroma Actual, float ExpectedCb, float ExpectedCr)
	{
		TestEqual(*FString::Printf(TEXT("%s Cb"), What), Actual.Cb, ExpectedCb, Tolerance);
		TestEqual(*FString::Printf(TEXT("%s Cr"), What), Actual.Cr, ExpectedCr, Tolerance);
	};

	// Neutrals carry no chroma at any brightness.
	CheckChroma(TEXT("Black"), TrueScope::RgbToChroma(0.0f, 0.0f, 0.0f), 0.0f, 0.0f);
	CheckChroma(TEXT("Mid gray"), TrueScope::RgbToChroma(0.5f, 0.5f, 0.5f), 0.0f, 0.0f);
	CheckChroma(TEXT("White"), TrueScope::RgbToChroma(1.0f, 1.0f, 1.0f), 0.0f, 0.0f);

	// Primaries at full amplitude: Cb and Cr reach exactly +/-0.5 on blue/red,
	// which is what makes [-0.5, 0.5]^2 the tight bound of the gamut.
	CheckChroma(TEXT("Red"), TrueScope::RgbToChroma(1.0f, 0.0f, 0.0f), -0.1145721f, 0.5f);
	CheckChroma(TEXT("Green"), TrueScope::RgbToChroma(0.0f, 1.0f, 0.0f), -0.3854279f, -0.4541529f);
	CheckChroma(TEXT("Blue"), TrueScope::RgbToChroma(0.0f, 0.0f, 1.0f), 0.5f, -0.0458471f);
	CheckChroma(TEXT("Cyan"), TrueScope::RgbToChroma(0.0f, 1.0f, 1.0f), 0.1145721f, -0.5f);
	CheckChroma(TEXT("Yellow"), TrueScope::RgbToChroma(1.0f, 1.0f, 0.0f), -0.5f, 0.0458471f);

	// Standard vectorscope orientation: Cb right, Cr up. Red sits above center
	// and slightly left, blue right of center and slightly below.
	const TArrayView<const TrueScope::FVectorscopeTarget> Targets = TrueScope::GetVectorscopeTargets();
	TestEqual(TEXT("Six graticule targets"), Targets.Num(), 6);
	for (const TrueScope::FVectorscopeTarget& Target : Targets)
	{
		if (FString(Target.Label) == TEXT("R"))
		{
			TestTrue(TEXT("R target above center"), Target.Chroma.Cr > 0.0f);
			TestTrue(TEXT("R target left of center"), Target.Chroma.Cb < 0.0f);
		}
		else if (FString(Target.Label) == TEXT("B"))
		{
			TestTrue(TEXT("B target below center"), Target.Chroma.Cr < 0.0f);
			TestTrue(TEXT("B target right of center"), Target.Chroma.Cb > 0.0f);
		}
	}

	// Neutral maps to the same bin regardless of brightness.
	TestEqual(TEXT("Neutral Cb bin"), TrueScope::ChromaToBin(0.0f), FResult::NumVectorBins / 2);
	TestEqual(TEXT("Lowest bin"), TrueScope::ChromaToBin(-0.5f), 0);
	TestEqual(TEXT("Highest bin"), TrueScope::ChromaToBin(0.5f), FResult::NumVectorBins - 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeColorBarsVectorscopeTest,
	"TrueScope.Analysis.ColorBarsVectorscope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeColorBarsVectorscopeTest::RunTest(const FString&)
{
	// The key vectorscope test: each of the six chroma bars of a 75 % color-bar
	// frame must put its whole density into its graticule target box. A wrong
	// matrix, a swapped or flipped axis all break this.
	constexpr int32 Width = 256;
	constexpr int32 Height = 32;
	constexpr int32 BarPixels = (Width / NumBars) * Height;

	const TArray<FColor> Pixels = MakeColorBars(Width, Height);

	FResult Result;
	TrueScope::AnalyzePixels(Pixels, Width, Height, Result);

	TestEqual(TEXT("Vectorscope has one cell per (Cb, Cr) bin pair"),
		Result.Vectorscope.Num(), FResult::NumVectorBins * FResult::NumVectorBins);

	// Each bar is one uniform colour, so its pixels occupy a single bin. Allow
	// one bin of slack: 8-bit 191 is a hair off the exact 75 % amplitude the
	// targets are computed from, which can straddle a bin boundary.
	for (const TrueScope::FVectorscopeTarget& Target : TrueScope::GetVectorscopeTargets())
	{
		const int32 CbBin = TrueScope::ChromaToBin(Target.Chroma.Cb);
		const int32 CrBin = TrueScope::ChromaToBin(Target.Chroma.Cr);
		TestEqual(*FString::Printf(TEXT("Bar %s lands in its graticule target"), Target.Label),
			SumVectorscopeBox(Result, CbBin, CrBin, /*Radius*/ 1), (uint32)BarPixels);
	}

	// White 75 % and black are neutral: both bars land in the center bin.
	const int32 CenterBin = TrueScope::ChromaToBin(0.0f);
	TestEqual(TEXT("Neutral bars land dead center"),
		Result.Vectorscope[CenterBin * FResult::NumVectorBins + CenterBin], (uint32)(2 * BarPixels));

	// Nothing lost or double-counted.
	uint32 Total = 0;
	for (uint32 Count : Result.Vectorscope)
	{
		Total += Count;
	}
	TestEqual(TEXT("Vectorscope counts every pixel exactly once"), Total, (uint32)(Width * Height));

	// The display mapping keeps the targets lit and the empty plane dark.
	TrueScope::BuildVectorscopeImage(Result);
	TestEqual(TEXT("Vectorscope intensity image is complete"),
		Result.VectorscopeIntensity.Num(), FResult::NumVectorBins * FResult::NumVectorBins);
	const int32 CenterRow = (FResult::NumVectorBins - 1) - CenterBin;
	TestTrue(TEXT("Center is lit"),
		Result.VectorscopeIntensity[CenterRow * FResult::NumVectorBins + CenterBin] > 0);
	TestEqual(TEXT("Empty chroma coordinate stays black"),
		(int32)Result.VectorscopeIntensity[0], 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeColorBarsParadeTest,
	"TrueScope.Analysis.ColorBarsParade",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeColorBarsParadeTest::RunTest(const FString&)
{
	// Parade and waveform must show the bar levels: within a bar every column is
	// uniform, so each channel is a single peak at that bar's channel value.
	constexpr int32 Width = 256;
	constexpr int32 Height = 32;
	constexpr int32 BarWidth = Width / NumBars;

	const TArray<FColor> Pixels = MakeColorBars(Width, Height);

	FResult Result;
	TrueScope::AnalyzePixels(Pixels, Width, Height, Result);

	for (int32 Bar = 0; Bar < NumBars; ++Bar)
	{
		const int32 Column = Bar * BarWidth + BarWidth / 2;
		const FColor& Expected = BarColors[Bar];
		TestEqual(*FString::Printf(TEXT("Bar %d R level"), Bar),
			ColumnSinglePeakBin(Result, FResult::ChannelR, Column), (int32)Expected.R);
		TestEqual(*FString::Printf(TEXT("Bar %d G level"), Bar),
			ColumnSinglePeakBin(Result, FResult::ChannelG, Column), (int32)Expected.G);
		TestEqual(*FString::Printf(TEXT("Bar %d B level"), Bar),
			ColumnSinglePeakBin(Result, FResult::ChannelB, Column), (int32)Expected.B);
	}

	// The staircase: the luminance of the bars falls monotonically left to
	// right, which is what makes the pattern readable on a waveform.
	TestEqual(TEXT("White bar is the brightest"),
		ColumnSinglePeakBin(Result, FResult::ChannelG, BarWidth / 2), (int32)Bar75);
	TestEqual(TEXT("Black bar is at zero"),
		ColumnSinglePeakBin(Result, FResult::ChannelG, Width - BarWidth / 2), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeWaveformHorizontalGradientTest,
	"TrueScope.Analysis.WaveformHorizontalGradient",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeWaveformHorizontalGradientTest::RunTest(const FString&)
{
	// The waveform is the parade unseparated: one full-width trace. A black ->
	// white gradient must draw the same diagonal, in a single image, and neutral
	// pixels must produce equal R/G/B density (a white trace, not a coloured one).
	constexpr int32 Width = 256;
	constexpr int32 Height = 32;
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
	TrueScope::BuildParadeImage(Result);

	TArray<FColor> Texels;
	Texels.SetNumUninitialized(Result.ParadeImageSize.X * Result.ParadeImageSize.Y);
	TrueScope::BuildRgbWaveformTexels(Result,
		TrueScope::GetDefaultDensityGain(TrueScope::EDensityScope::Waveform), Texels);

	const int32 ImageWidth = Result.ParadeImageSize.X;
	const int32 ImageHeight = Result.ParadeImageSize.Y;
	TestEqual(TEXT("Waveform image is as wide as the source"), ImageWidth, Width);

	bool bDiagonal = true;
	bool bNeutralTrace = true;
	for (int32 Column = 0; Column < ImageWidth && bDiagonal && bNeutralTrace; ++Column)
	{
		int32 LitRow = INDEX_NONE;
		for (int32 Row = 0; Row < ImageHeight; ++Row)
		{
			const FColor& Texel = Texels[Row * ImageWidth + Column];
			if (Texel.R != 0 || Texel.G != 0 || Texel.B != 0)
			{
				if (LitRow != INDEX_NONE)
				{
					bDiagonal = false; // more than one lit row in this column
					break;
				}
				LitRow = Row;
				// Gray source: the three channel densities must be identical.
				bNeutralTrace &= (Texel.R == Texel.G) && (Texel.G == Texel.B);
			}
		}
		// Value X sits in bin X, and row 0 is the highest bin.
		bDiagonal &= (LitRow == (FResult::NumBins - 1) - Column);
	}

	TestTrue(TEXT("Waveform draws one diagonal trace, one lit row per column"), bDiagonal);
	TestTrue(TEXT("Neutral pixels produce an unseparated white trace"), bNeutralTrace);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeHistogramChannelsTest,
	"TrueScope.Analysis.HistogramChannels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeHistogramChannelsTest::RunTest(const FString&)
{
	// Three distinct channel levels: the separate-channel histogram must show
	// three distributions at three different positions on the shared value axis.
	// Both display modes read exactly these arrays — only the layout differs
	// (see TrueScope.UI.HistogramChannelLayout).
	constexpr int32 Width = 32;
	constexpr int32 Height = 16;
	constexpr uint8 Levels[FResult::NumChannels] = {64, 128, 192};

	TArray<FColor> Pixels;
	Pixels.Init(FColor(Levels[0], Levels[1], Levels[2], 255), Width * Height);

	FResult Result;
	TrueScope::AnalyzePixels(Pixels, Width, Height, Result);

	for (int32 Channel = 0; Channel < FResult::NumChannels; ++Channel)
	{
		TestEqual(*FString::Printf(TEXT("Channel %d peaks at its own level"), Channel),
			SinglePeakBin(Result, Channel), (int32)Levels[Channel]);
		TestEqual(*FString::Printf(TEXT("Channel %d histogram sums to pixel count"), Channel),
			SumHistogram(Result, Channel), (uint32)(Width * Height));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
