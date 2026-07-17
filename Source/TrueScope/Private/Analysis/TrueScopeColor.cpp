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

	/** The same six colours at full amplitude (phase-6.2: the 100 % targets). */
	constexpr FTargetDefinition TargetDefinitions100[] = {
		{TEXT("R"),  1.0f, 0.0f, 0.0f},
		{TEXT("Mg"), 1.0f, 0.0f, 1.0f},
		{TEXT("B"),  0.0f, 0.0f, 1.0f},
		{TEXT("Cy"), 0.0f, 1.0f, 1.0f},
		{TEXT("G"),  0.0f, 1.0f, 0.0f},
		{TEXT("Yl"), 1.0f, 1.0f, 0.0f},
	};

	TArray<FVectorscopeTarget> BuildTargets(TArrayView<const FTargetDefinition> Definitions)
	{
		TArray<FVectorscopeTarget> Built;
		Built.Reserve(Definitions.Num());
		for (const FTargetDefinition& Definition : Definitions)
		{
			Built.Add(FVectorscopeTarget{Definition.Label,
				RgbToChroma(Definition.R, Definition.G, Definition.B)});
		}
		return Built;
	}

	/**
	 * Fully saturated RGB hue (HSV with S = V = 1) for a hue fraction in [0,1).
	 * Textbook piecewise conversion, written from the definition.
	 */
	FLinearColor HueToRgb(float HueFraction)
	{
		const float H = FMath::Frac(HueFraction) * 6.0f;
		const float X = 1.0f - FMath::Abs(FMath::Fmod(H, 2.0f) - 1.0f);
		switch (FMath::Min(static_cast<int32>(H), 5))
		{
		case 0:  return FLinearColor(1.0f, X, 0.0f);
		case 1:  return FLinearColor(X, 1.0f, 0.0f);
		case 2:  return FLinearColor(0.0f, 1.0f, X);
		case 3:  return FLinearColor(0.0f, X, 1.0f);
		case 4:  return FLinearColor(X, 0.0f, 1.0f);
		default: return FLinearColor(1.0f, 0.0f, X);
		}
	}
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

FIntPoint PixelToChromaBins(const FColor& Color)
{
	// The BT.709 coefficients as exact rationals over 10000. With byte values
	// r,g,b, putting Cb = (b/255 - Y')/(2(1-Kb)) over a common denominator:
	//
	//   Cb = ((10000-722)b - 2126r - 7152g) / (255 * 2 * (10000-722))
	//
	// and bin = floor((Cb + 0.5) * NumVectorBins) becomes a single integer
	// division (numerator max NumVectorBins * CbDen < 2^31, non-negative, so
	// truncation is floor). Cr identically over (10000-2126).
	constexpr int32 Scale = 10000;
	constexpr int32 KR = 2126;
	constexpr int32 KG = 7152;
	constexpr int32 KB = 722;
	static_assert(KR + KG + KB == Scale, "BT.709 luma coefficients must sum to 1");
	static_assert(LumaR == static_cast<float>(KR) / Scale && LumaG == static_cast<float>(KG) / Scale
			&& LumaB == static_cast<float>(KB) / Scale,
		"integer and float BT.709 coefficients must agree");

	constexpr int32 NumBins = FTrueScopeAnalysisResult::NumVectorBins;
	constexpr int32 CbDen = 255 * 2 * (Scale - KB);
	constexpr int32 CrDen = 255 * 2 * (Scale - KR);
	static_assert(NumBins <= INT32_MAX / CbDen && NumBins <= INT32_MAX / CrDen,
		"bin computation must fit 32-bit integer arithmetic");

	const int32 NCb = (Scale - KB) * Color.B - KR * Color.R - KG * Color.G;
	const int32 NCr = (Scale - KR) * Color.R - KG * Color.G - KB * Color.B;

	// +CbDen/2 shifts [-0.5, 0.5] to [0, 1]; the +0.5 extreme lands on
	// NumBins and clamps into the last bin, exactly like ChromaToBin.
	const int32 CbBin = FMath::Min(NumBins * (NCb + CbDen / 2) / CbDen, NumBins - 1);
	const int32 CrBin = FMath::Min(NumBins * (NCr + CrDen / 2) / CrDen, NumBins - 1);
	return FIntPoint(CbBin, CrBin);
}

int32 PixelToLumaBin(const FColor& Color)
{
	// The BT.709 luma as an exact rational over 10000 (the PixelToChromaBins
	// coefficients), so CPU and GPU bin identically by construction. With byte
	// values, luma' = N / (10000 * 255) and the rounded bin
	// round(luma' * (NumBins-1)) becomes one non-negative integer division —
	// rounded, not floored: a neutral pixel of code value v must land in bin v,
	// exactly where the R/G/B channels put it.
	constexpr int32 Scale = 10000;
	constexpr int32 KR = 2126;
	constexpr int32 KG = 7152;
	constexpr int32 KB = 722;
	static_assert(LumaR == static_cast<float>(KR) / Scale && LumaG == static_cast<float>(KG) / Scale
			&& LumaB == static_cast<float>(KB) / Scale,
		"integer and float BT.709 coefficients must agree");

	constexpr int32 NumBins = FTrueScopeAnalysisResult::NumBins;
	constexpr int32 Den = Scale * 255;
	static_assert(Scale * 255LL * (NumBins - 1) + Den / 2 <= INT32_MAX,
		"luma bin computation must fit 32-bit integer arithmetic");

	const int32 N = KR * Color.R + KG * Color.G + KB * Color.B;
	return FMath::Min((N * (NumBins - 1) + Den / 2) / Den, NumBins - 1);
}

TArrayView<const FVectorscopeTarget> GetVectorscopeTargets()
{
	static const TArray<FVectorscopeTarget> Targets = BuildTargets(TargetDefinitions);
	return Targets;
}

TArrayView<const FVectorscopeTarget> GetVectorscopeTargets100()
{
	static const TArray<FVectorscopeTarget> Targets = BuildTargets(TargetDefinitions100);
	return Targets;
}

float ChromaAngleDegrees(const FChroma& Chroma)
{
	const float Degrees = FMath::RadiansToDegrees(FMath::Atan2(Chroma.Cr, Chroma.Cb));
	return Degrees < 0.0f ? Degrees + 360.0f : Degrees;
}

void BuildHueRingStops(int32 NumSteps, TArray<FHueRingStop>& OutStops)
{
	OutStops.Reset(NumSteps);
	for (int32 Step = 0; Step < NumSteps; ++Step)
	{
		// Sweep in HUE order and derive each stop's ANGLE from the BT.709 matrix
		// — never place hues at uniform angles (see the header: the non-uniform
		// spacing and the register with the six targets both come from this).
		const FLinearColor Rgb = HueToRgb(static_cast<float>(Step) / NumSteps);
		const FChroma Chroma = RgbToChroma(Rgb.R, Rgb.G, Rgb.B);
		OutStops.Add(FHueRingStop{FMath::Atan2(Chroma.Cr, Chroma.Cb), Rgb});
	}
}

} // namespace TrueScope
