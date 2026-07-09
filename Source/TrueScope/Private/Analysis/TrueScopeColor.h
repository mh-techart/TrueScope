// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Containers/ArrayView.h"
#include "CoreMinimal.h"

/**
 * BT.709 color math for the vectorscope, written from the published matrix
 * (Rec. ITU-R BT.709-6, non-constant luminance) — no GPL sources involved.
 *
 * Inputs are the display-encoded R'G'B' of the captured viewport image, which
 * is TrueScope's measurement point (architecture.md, "Core measurement
 * principle"). No linearization: Y'CbCr is defined on the gamma-encoded
 * signal, and that is exactly what a hardware vectorscope sees.
 */
namespace TrueScope
{
	/** BT.709 luma coefficients. */
	inline constexpr float LumaR = 0.2126f;
	inline constexpr float LumaG = 0.7152f;
	inline constexpr float LumaB = 0.0722f;

	/**
	 * Chroma coordinates of one pixel. Both components span exactly [-0.5, 0.5]
	 * over the R'G'B' unit cube, so the square [-0.5,0.5]^2 is the tight
	 * bounding box of the gamut — the vectorscope's plot square.
	 */
	struct FChroma
	{
		float Cb = 0.0f;
		float Cr = 0.0f;
	};

	/** R'G'B' in [0,1] -> BT.709 Cb/Cr. */
	FChroma RgbToChroma(float R, float G, float B);

	/** 8-bit display-encoded pixel -> BT.709 Cb/Cr. */
	FChroma RgbToChroma(const FColor& Color);

	/**
	 * Bin index of a chroma component on the vectorscope's
	 * FTrueScopeAnalysisResult::NumVectorBins axis; bin 0 = -0.5, last bin =
	 * +0.5. Neutral (0.0) lands on the first bin above center.
	 */
	int32 ChromaToBin(float Chroma);

	/** One graticule target of the vectorscope: a 75 % color-bar position. */
	struct FVectorscopeTarget
	{
		/** Standard scope label: R, Mg, B, Cy, G, Yl. */
		const TCHAR* Label = nullptr;
		FChroma Chroma;
	};

	/**
	 * The six standard 75 % targets, derived from the BT.709 matrix rather than
	 * hard-coded, so graticule, analysis and tests can never disagree. Note that
	 * they do not share one radius: in BT.709 the G and Mg targets sit markedly
	 * further from center than R/Cy and Yl/B.
	 */
	TArrayView<const FVectorscopeTarget> GetVectorscopeTargets();
}
