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

	/**
	 * Full BT.709 Y'CbCr of one pixel: luma in [0,1], chroma in [-0.5, 0.5].
	 * Float reference conversion (tests, graticule geometry); the pixel
	 * analysis bins through the integer-exact PixelTo*Bin functions instead.
	 */
	struct FYCbCr
	{
		float Luma = 0.0f;
		FChroma Chroma;
	};

	/** R'G'B' in [0,1] -> BT.709 Y'CbCr. */
	FYCbCr RgbToYCbCr(float R, float G, float B);

	/** 8-bit display-encoded pixel -> BT.709 Y'CbCr. */
	FYCbCr RgbToYCbCr(const FColor& Color);

	/** Chroma only — the vectorscope graticule needs nothing else. */
	inline FChroma RgbToChroma(float R, float G, float B)
	{
		return RgbToYCbCr(R, G, B).Chroma;
	}

	inline FChroma RgbToChroma(const FColor& Color)
	{
		return RgbToYCbCr(Color).Chroma;
	}

	/**
	 * Bin index of a chroma component on the vectorscope's
	 * FTrueScopeAnalysisResult::NumVectorBins axis; bin 0 = -0.5, last bin =
	 * +0.5. Neutral (0.0) lands on the first bin above center.
	 *
	 * Float reference mapping (tests, graticule geometry). The pixel analysis
	 * bins through PixelToChromaBins instead — see there.
	 */
	int32 ChromaToBin(float Chroma);

	/**
	 * Integer-exact vectorscope bins of one 8-bit pixel (X = Cb bin, Y = Cr
	 * bin), the same BT.709 coefficients as RgbToYCbCr expressed as exact
	 * rationals (0.2126 = 2126/10000, ...) and evaluated in pure integer
	 * arithmetic. This makes the CPU and the GPU vectorscope bin *identically
	 * by construction* (the parity rule's "identical values", phase-5.1):
	 * float evaluation is unstable exactly where it matters most — every
	 * neutral pixel's chroma is 0, which sits ON the boundary between the two
	 * center bins, so FMA/ordering differences between compilers flip whole
	 * gray-value populations between bins 127 and 128 in bulk. Mirrored by
	 * TrueScopeVectorscope.usf; the GPU equivalence test pins the agreement.
	 */
	FIntPoint PixelToChromaBins(const FColor& Color);

	/**
	 * Integer-exact luma waveform bin of one 8-bit pixel on the same NumBins
	 * axis the R/G/B channels use, so a neutral pixel bins on its own code
	 * value and the luma waveform reads on the shared 0-1023 scale. Same
	 * technique and rationale as PixelToChromaBins (phase-5.2: the waveform's
	 * GPU port must bin identically by construction — float luma is unstable
	 * exactly at bin boundaries, which every neutral pixel sits on). Mirrored
	 * by TrueScopeParade.usf; the GPU equivalence test pins the agreement.
	 */
	int32 PixelToLumaBin(const FColor& Color);

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

	/**
	 * The six 100 % targets (fully saturated primaries/secondaries), derived the
	 * same way as the 75 % set. They do NOT share one radius either — each sits
	 * at its own chroma magnitude (Mg/G furthest out, R/Cy nearer, Yl/B nearest;
	 * phase-6.2 spec, measured against Resolve) — and must never be placed on a
	 * common circle.
	 */
	TArrayView<const FVectorscopeTarget> GetVectorscopeTargets100();

	/**
	 * Chroma -> graticule angle in degrees: counter-clockwise from +Cb (Cb on x
	 * positive right, Cr on y positive up), normalized to [0, 360). The one
	 * angular reference frame of the vectorscope (phase-6.2) — the frame the
	 * density is plotted in and the frame Resolve uses (no NTSC/YIQ rotation).
	 */
	float ChromaAngleDegrees(const FChroma& Chroma);

	/**
	 * Skin tone line angle in the Cb/Cr frame above (phase-6.2 Task 1a).
	 *
	 * THIS ANGLE IS A CONVENTION, NOT A STANDARD — do not "correct" it.
	 * Blackmagic publishes no number and Resolve's documentation describes the
	 * indicator only qualitatively. 123.0 degrees is the historical NTSC I axis
	 * expressed in the Cb/Cr plane, the value SMPTE flesh-tone practice and
	 * Resolve, FCP, Premiere and Media Composer all converge on. Three
	 * independent confirmations, recorded here so nobody replaces the constant
	 * with a plausible-looking recomputation (phase-6.2 spec):
	 *   (1) the NTSC I-axis derivation;
	 *   (2) a representative skin colour, sRGB 0.80/0.60/0.50, computes to
	 *       124.9 degrees — on the line;
	 *   (3) direct measurement of Resolve's own white line: 123.08 degrees,
	 *       sigma 0.09 degrees over 1546 pixels.
	 */
	inline constexpr float SkinToneLineAngleDegrees = 123.0f;

	/** One colour stop of the vectorscope hue ring, at its Cb/Cr angle. */
	struct FHueRingStop
	{
		/** Graticule angle (ChromaAngleDegrees frame), in RADIANS. */
		float AngleRadians = 0.0f;

		/** Display-encoded fully saturated hue at that angle. */
		FLinearColor Color = FLinearColor::Black;
	};

	/**
	 * Builds the vectorscope hue ring (phase-6.2 Task 1b): NumSteps fully
	 * saturated RGB hues swept in hue order, each converted through the BT.709
	 * matrix and placed at atan2(Cr, Cb). The ring is deliberately NOT an
	 * HSV/RGB colour wheel: generating the angles from the same formula as the
	 * six targets makes the non-uniform angular spacing (red->yellow 72 deg,
	 * yellow->green 55 deg) fall out by itself and keeps the ring in register
	 * with the targets by construction — it cannot be mis-rotated. Stops come
	 * out in monotonically increasing angle order along the hue sweep, wrapping
	 * once through 360.
	 */
	void BuildHueRingStops(int32 NumSteps, TArray<FHueRingStop>& OutStops);
}
