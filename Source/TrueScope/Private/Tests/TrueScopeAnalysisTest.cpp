// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "Analysis/TrueScopeAnalysis.h"
#include "Analysis/TrueScopeAnalysisResult.h"
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

#endif // WITH_DEV_AUTOMATION_TESTS
