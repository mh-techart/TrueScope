// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "Analysis/TrueScopeColor.h"

#include "Analysis/TrueScopeAnalysisResult.h"

namespace TrueScope
{

namespace
{
	/** Denominators of the BT.709 Cb/Cr definitions: 2*(1-Kb) and 2*(1-Kr). */
	constexpr float CbScale = 2.0f * (1.0f - LumaB);
	constexpr float CrScale = 2.0f * (1.0f - LumaR);

	/** Amplitude of the standard 75 % color bars. */
	constexpr float Bar75 = 0.75f;

	struct FTargetDefinition
	{
		const TCHAR* Label;
		float R;
		float G;
		float B;
	};

	/** Listed in the conventional graticule order, counter-clockwise from R. */
	constexpr FTargetDefinition TargetDefinitions[] = {
		{TEXT("R"),  Bar75, 0.0f,  0.0f },
		{TEXT("Mg"), Bar75, 0.0f,  Bar75},
		{TEXT("B"),  0.0f,  0.0f,  Bar75},
		{TEXT("Cy"), 0.0f,  Bar75, Bar75},
		{TEXT("G"),  0.0f,  Bar75, 0.0f },
		{TEXT("Yl"), Bar75, Bar75, 0.0f },
	};
}

FYCbCr RgbToYCbCr(float R, float G, float B)
{
	const float Luma = LumaR * R + LumaG * G + LumaB * B;
	return FYCbCr{Luma, FChroma{(B - Luma) / CbScale, (R - Luma) / CrScale}};
}

FYCbCr RgbToYCbCr(const FColor& Color)
{
	constexpr float Inv255 = 1.0f / 255.0f;
	return RgbToYCbCr(Color.R * Inv255, Color.G * Inv255, Color.B * Inv255);
}

int32 ChromaToBin(float Chroma)
{
	const int32 Bin = FMath::FloorToInt32((Chroma + 0.5f) * FTrueScopeAnalysisResult::NumVectorBins);
	return FMath::Clamp(Bin, 0, FTrueScopeAnalysisResult::NumVectorBins - 1);
}

int32 LumaToBin(float Luma)
{
	// Rounded, not floored: a neutral pixel of code value v must land in bin v,
	// exactly where the R/G/B channels put it.
	const int32 Bin = FMath::RoundToInt32(Luma * (FTrueScopeAnalysisResult::NumBins - 1));
	return FMath::Clamp(Bin, 0, FTrueScopeAnalysisResult::NumBins - 1);
}

TArrayView<const FVectorscopeTarget> GetVectorscopeTargets()
{
	static const TArray<FVectorscopeTarget> Targets = []
	{
		TArray<FVectorscopeTarget> Built;
		Built.Reserve(UE_ARRAY_COUNT(TargetDefinitions));
		for (const FTargetDefinition& Definition : TargetDefinitions)
		{
			Built.Add(FVectorscopeTarget{Definition.Label, RgbToChroma(Definition.R, Definition.G, Definition.B)});
		}
		return Built;
	}();
	return Targets;
}

} // namespace TrueScope
