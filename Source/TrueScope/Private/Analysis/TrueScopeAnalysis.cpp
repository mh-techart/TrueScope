// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "Analysis/TrueScopeAnalysis.h"

namespace TrueScope
{

/**
 * Gain applied inside the log mapping; lifts low densities (a single pixel in
 * a 540-row column) into clear visibility. Tuning constant, revisited in M4/M6.
 */
static constexpr float IntensityGain = 16.0f;

void AnalyzePixels(TArrayView<const FColor> Pixels, int32 Width, int32 Height, FTrueScopeAnalysisResult& OutResult)
{
	check(Pixels.Num() == Width * Height);

	OutResult.SourceSize = FIntPoint(Width, Height);
	for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
	{
		OutResult.Histogram[Channel].SetNumZeroed(FTrueScopeAnalysisResult::NumBins);
		OutResult.Parade[Channel].SetNumZeroed(Width * FTrueScopeAnalysisResult::NumBins);
	}

	uint32* HistR = OutResult.Histogram[FTrueScopeAnalysisResult::ChannelR].GetData();
	uint32* HistG = OutResult.Histogram[FTrueScopeAnalysisResult::ChannelG].GetData();
	uint32* HistB = OutResult.Histogram[FTrueScopeAnalysisResult::ChannelB].GetData();
	uint32* ParadeR = OutResult.Parade[FTrueScopeAnalysisResult::ChannelR].GetData();
	uint32* ParadeG = OutResult.Parade[FTrueScopeAnalysisResult::ChannelG].GetData();
	uint32* ParadeB = OutResult.Parade[FTrueScopeAnalysisResult::ChannelB].GetData();

	const FColor* Pixel = Pixels.GetData();
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X, ++Pixel)
		{
			// 8-bit channel value == bin index (NumBins is 256 by definition).
			++HistR[Pixel->R];
			++HistG[Pixel->G];
			++HistB[Pixel->B];

			const int32 ColumnBase = X * FTrueScopeAnalysisResult::NumBins;
			++ParadeR[ColumnBase + Pixel->R];
			++ParadeG[ColumnBase + Pixel->G];
			++ParadeB[ColumnBase + Pixel->B];
		}
	}
}

float CountToIntensity(uint32 Count, uint32 MaxCount)
{
	if (Count == 0 || MaxCount == 0)
	{
		return 0.0f;
	}
	return FMath::Loge(1.0f + IntensityGain * static_cast<float>(Count))
		/ FMath::Loge(1.0f + IntensityGain * static_cast<float>(MaxCount));
}

void BuildParadeImage(FTrueScopeAnalysisResult& InOutResult)
{
	const int32 Width = InOutResult.SourceSize.X;
	const int32 Height = FTrueScopeAnalysisResult::NumBins;
	if (Width <= 0)
	{
		for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
		{
			InOutResult.ParadeChannelIntensity[Channel].Reset();
		}
		InOutResult.ParadeImageSize = FIntPoint::ZeroValue;
		return;
	}

	InOutResult.ParadeImageSize = FIntPoint(Width, Height);

	// Maximum possible count per (column, bin) cell: every source row of the
	// column holding the same value.
	const uint32 MaxCount = static_cast<uint32>(InOutResult.SourceSize.Y);

	// One independent intensity image per channel — the block for channel C is
	// fed only channel C, so nothing from the other channels can appear in it.
	for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
	{
		TArray<uint8>& Image = InOutResult.ParadeChannelIntensity[Channel];
		Image.SetNumUninitialized(Width * Height);
		const uint32* Counts = InOutResult.Parade[Channel].GetData();

		uint8* Out = Image.GetData();
		for (int32 Row = 0; Row < Height; ++Row)
		{
			const int32 Bin = (Height - 1) - Row; // row 0 = top = brightest bin
			for (int32 Column = 0; Column < Width; ++Column, ++Out)
			{
				const int32 CellIndex = Column * FTrueScopeAnalysisResult::NumBins + Bin;
				*Out = static_cast<uint8>(FMath::RoundToInt(255.0f * CountToIntensity(Counts[CellIndex], MaxCount)));
			}
		}
	}
}

} // namespace TrueScope
