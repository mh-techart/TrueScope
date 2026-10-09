// Copyright 2026 Michael Herber
// SPDX-License-Identifier: Apache-2.0

#include "Analysis/TrueScopeAnalysis.h"

#include "Analysis/TrueScopeColor.h"

namespace TrueScope
{

// The transfer and its gains live in the header (FDensityGains) so the M5 GPU
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

			// BT.709 luma for the luma waveform, binned integer-exactly so the
			// CPU and GPU paths agree bit-for-bit (see PixelToLumaBin).
			++LumaParade[ColumnBase + PixelToLumaBin(*Pixel)];

			// Vectorscope bins integer-exactly so the CPU and GPU paths agree
			// bit-for-bit (see PixelToChromaBins).
			const FIntPoint ChromaBins = PixelToChromaBins(*Pixel);
			++Vector[ChromaBins.Y * FTrueScopeAnalysisResult::NumVectorBins + ChromaBins.X];
		}
	}
}

float CountToIntensity(uint32 Count, uint32 MaxCount, float Gain)
{
	if (Count == 0 || MaxCount == 0 || Gain <= 0.0f)
	{
		return 0.0f;
	}
	// Linear, hard-clipped at white (M6.3). The clip is hard because Resolve's
	// is: above trace max it simply stops getting brighter.
	return FMath::Min(static_cast<float>(Count) * Gain / static_cast<float>(MaxCount), 1.0f);
}

namespace
{
	/**
	 * Maps one column-x-bin count array to a display-mapped intensity image
	 * (row 0 = top = highest bin). Shared by the parade's three channel images
	 * and the luma waveform, so all four traces carry the same density curve.
	 */
	void BuildColumnIntensityImage(const TArray<uint32>& Counts, int32 Width, uint32 MaxCount, float Gain,
		TArray<uint8>& OutImage)
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
				*Out = static_cast<uint8>(FMath::RoundToInt(255.0f * CountToIntensity(Cells[CellIndex], MaxCount, Gain)));
			}
		}
	}
}

void BuildParadeImage(FTrueScopeAnalysisResult& InOutResult, const FDensityGains& Gains)
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
		BuildColumnIntensityImage(InOutResult.Parade[Channel], Width, MaxCount, Gains.Parade,
			InOutResult.ParadeChannelIntensity[Channel]);
	}
}

void BuildLumaWaveformImage(FTrueScopeAnalysisResult& InOutResult, const FDensityGains& Gains)
{
	const int32 Width = InOutResult.SourceSize.X;
	if (Width <= 0 || InOutResult.LumaParade.Num() != Width * FTrueScopeAnalysisResult::NumBins)
	{
		InOutResult.LumaIntensity.Reset();
		return;
	}

	const uint32 MaxCount = static_cast<uint32>(InOutResult.SourceSize.Y);
	BuildColumnIntensityImage(InOutResult.LumaParade, Width, MaxCount, Gains.Waveform, InOutResult.LumaIntensity);
}

void BuildVectorscopeImage(FTrueScopeAnalysisResult& InOutResult, const FDensityGains& Gains)
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
				FMath::RoundToInt(255.0f * CountToIntensity(Counts[CrBin * NumVectorBins + CbBin], MaxCount,
					Gains.Vectorscope)));
		}
	}
}

void BuildRgbWaveformTexels(const FTrueScopeAnalysisResult& Result, float WaveformGain,
	TArrayView<FColor> OutTexels)
{
	const int32 Width = Result.ParadeImageSize.X;
	const int32 Height = Result.ParadeImageSize.Y;
	const int32 NumTexels = Width * Height;
	check(OutTexels.Num() == NumTexels);

	// Mapped from the parade COUNTS rather than from ParadeChannelIntensity,
	// which the parade scope owns: since M6.3 the two scopes carry independent
	// Trace Brightness gains, so the waveform must apply its own instead of
	// inheriting whatever the parade's slider happens to be set to.
	const uint32* Counts[FTrueScopeAnalysisResult::NumChannels] = {
		Result.Parade[FTrueScopeAnalysisResult::ChannelR].GetData(),
		Result.Parade[FTrueScopeAnalysisResult::ChannelG].GetData(),
		Result.Parade[FTrueScopeAnalysisResult::ChannelB].GetData(),
	};
	const uint32 MaxCount = static_cast<uint32>(Result.SourceSize.Y);

	for (int32 Row = 0; Row < Height; ++Row)
	{
		const int32 Bin = (Height - 1) - Row; // row 0 = top = highest value bin
		for (int32 Column = 0; Column < Width; ++Column)
		{
			const int32 CellIndex = Column * FTrueScopeAnalysisResult::NumBins + Bin;
			uint8 Rgb[FTrueScopeAnalysisResult::NumChannels];
			for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
			{
				Rgb[Channel] = static_cast<uint8>(FMath::RoundToInt(
					255.0f * CountToIntensity(Counts[Channel][CellIndex], MaxCount, WaveformGain)));
			}
			OutTexels[Row * Width + Column] = FColor(
				Rgb[FTrueScopeAnalysisResult::ChannelR],
				Rgb[FTrueScopeAnalysisResult::ChannelG],
				Rgb[FTrueScopeAnalysisResult::ChannelB], 255);
		}
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
