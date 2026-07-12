// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "Analysis/TrueScopeAnalysis.h"

#include "Analysis/TrueScopeColor.h"

namespace TrueScope
{

// The gain constant lives in the header (DensityIntensityGain) so the M5 GPU
// intensity pass provably applies the same density curve.

void AnalyzePixels(TArrayView<const FColor> Pixels, int32 Width, int32 Height, FTrueScopeAnalysisResult& OutResult)
{
	check(Pixels.Num() == Width * Height);

	OutResult.SourceSize = FIntPoint(Width, Height);
	for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
	{
		OutResult.Histogram[Channel].SetNumZeroed(FTrueScopeAnalysisResult::NumBins);
		OutResult.Parade[Channel].SetNumZeroed(Width * FTrueScopeAnalysisResult::NumBins);
	}
	OutResult.LumaParade.SetNumZeroed(Width * FTrueScopeAnalysisResult::NumBins);
	OutResult.Vectorscope.SetNumZeroed(
		FTrueScopeAnalysisResult::NumVectorBins * FTrueScopeAnalysisResult::NumVectorBins);

	uint32* HistR = OutResult.Histogram[FTrueScopeAnalysisResult::ChannelR].GetData();
	uint32* HistG = OutResult.Histogram[FTrueScopeAnalysisResult::ChannelG].GetData();
	uint32* HistB = OutResult.Histogram[FTrueScopeAnalysisResult::ChannelB].GetData();
	uint32* ParadeR = OutResult.Parade[FTrueScopeAnalysisResult::ChannelR].GetData();
	uint32* ParadeG = OutResult.Parade[FTrueScopeAnalysisResult::ChannelG].GetData();
	uint32* ParadeB = OutResult.Parade[FTrueScopeAnalysisResult::ChannelB].GetData();
	uint32* LumaParade = OutResult.LumaParade.GetData();
	uint32* Vector = OutResult.Vectorscope.GetData();

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

			// One BT.709 conversion feeds both the luma waveform and the
			// vectorscope; the luma is the chroma's own intermediate.
			const FYCbCr Ycc = RgbToYCbCr(*Pixel);
			++LumaParade[ColumnBase + LumaToBin(Ycc.Luma)];
			++Vector[ChromaToBin(Ycc.Chroma.Cr) * FTrueScopeAnalysisResult::NumVectorBins
				+ ChromaToBin(Ycc.Chroma.Cb)];
		}
	}
}

float CountToIntensity(uint32 Count, uint32 MaxCount)
{
	if (Count == 0 || MaxCount == 0)
	{
		return 0.0f;
	}
	return FMath::Loge(1.0f + DensityIntensityGain * static_cast<float>(Count))
		/ FMath::Loge(1.0f + DensityIntensityGain * static_cast<float>(MaxCount));
}

namespace
{
	/**
	 * Maps one column-x-bin count array to a display-mapped intensity image
	 * (row 0 = top = highest bin). Shared by the parade's three channel images
	 * and the luma waveform, so all four traces carry the same density curve.
	 */
	void BuildColumnIntensityImage(const TArray<uint32>& Counts, int32 Width, uint32 MaxCount, TArray<uint8>& OutImage)
	{
		const int32 Height = FTrueScopeAnalysisResult::NumBins;
		OutImage.SetNumUninitialized(Width * Height);

		const uint32* Cells = Counts.GetData();
		uint8* Out = OutImage.GetData();
		for (int32 Row = 0; Row < Height; ++Row)
		{
			const int32 Bin = (Height - 1) - Row; // row 0 = top = brightest bin
			for (int32 Column = 0; Column < Width; ++Column, ++Out)
			{
				const int32 CellIndex = Column * FTrueScopeAnalysisResult::NumBins + Bin;
				*Out = static_cast<uint8>(FMath::RoundToInt(255.0f * CountToIntensity(Cells[CellIndex], MaxCount)));
			}
		}
	}
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
		BuildColumnIntensityImage(InOutResult.Parade[Channel], Width, MaxCount,
			InOutResult.ParadeChannelIntensity[Channel]);
	}
}

void BuildLumaWaveformImage(FTrueScopeAnalysisResult& InOutResult)
{
	const int32 Width = InOutResult.SourceSize.X;
	if (Width <= 0 || InOutResult.LumaParade.Num() != Width * FTrueScopeAnalysisResult::NumBins)
	{
		InOutResult.LumaIntensity.Reset();
		return;
	}

	const uint32 MaxCount = static_cast<uint32>(InOutResult.SourceSize.Y);
	BuildColumnIntensityImage(InOutResult.LumaParade, Width, MaxCount, InOutResult.LumaIntensity);
}

void BuildVectorscopeImage(FTrueScopeAnalysisResult& InOutResult)
{
	constexpr int32 NumVectorBins = FTrueScopeAnalysisResult::NumVectorBins;
	if (InOutResult.Vectorscope.Num() != NumVectorBins * NumVectorBins)
	{
		InOutResult.VectorscopeIntensity.Reset();
		return;
	}

	// Maximum possible count in one (Cb, Cr) cell: the whole frame on one
	// chroma coordinate, which a flat neutral image actually produces.
	const uint32 MaxCount = static_cast<uint32>(InOutResult.SourceSize.X * InOutResult.SourceSize.Y);

	InOutResult.VectorscopeIntensity.SetNumUninitialized(NumVectorBins * NumVectorBins);
	const uint32* Counts = InOutResult.Vectorscope.GetData();
	uint8* Out = InOutResult.VectorscopeIntensity.GetData();

	for (int32 Row = 0; Row < NumVectorBins; ++Row)
	{
		const int32 CrBin = (NumVectorBins - 1) - Row; // row 0 = top = highest Cr
		for (int32 CbBin = 0; CbBin < NumVectorBins; ++CbBin, ++Out)
		{
			*Out = static_cast<uint8>(
				FMath::RoundToInt(255.0f * CountToIntensity(Counts[CrBin * NumVectorBins + CbBin], MaxCount)));
		}
	}
}

void BuildRgbWaveformTexels(const FTrueScopeAnalysisResult& Result, TArrayView<FColor> OutTexels)
{
	const int32 NumTexels = Result.ParadeImageSize.X * Result.ParadeImageSize.Y;
	check(OutTexels.Num() == NumTexels);

	const uint8* Red = Result.ParadeChannelIntensity[FTrueScopeAnalysisResult::ChannelR].GetData();
	const uint8* Green = Result.ParadeChannelIntensity[FTrueScopeAnalysisResult::ChannelG].GetData();
	const uint8* Blue = Result.ParadeChannelIntensity[FTrueScopeAnalysisResult::ChannelB].GetData();

	for (int32 Index = 0; Index < NumTexels; ++Index)
	{
		OutTexels[Index] = FColor(Red[Index], Green[Index], Blue[Index], 255);
	}
}

void BuildLumaWaveformTexels(const FTrueScopeAnalysisResult& Result, TArrayView<FColor> OutTexels)
{
	const int32 NumTexels = Result.ParadeImageSize.X * Result.ParadeImageSize.Y;
	check(OutTexels.Num() == NumTexels);
	check(Result.LumaIntensity.Num() == NumTexels);

	const uint8* Luma = Result.LumaIntensity.GetData();
	for (int32 Index = 0; Index < NumTexels; ++Index)
	{
		// Neutral trace: luma carries no colour, so the waveform must not imply one.
		OutTexels[Index] = FColor(Luma[Index], Luma[Index], Luma[Index], 255);
	}
}

} // namespace TrueScope
