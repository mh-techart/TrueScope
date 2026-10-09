// Copyright 2026 Michael Herber
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Math/UnrealMathUtility.h"

#include <atomic>

namespace TrueScope
{
	/**
	 * The shared logarithmic gain slider law (architecture.md, "Gain slider
	 * law"), used by BOTH gain controls: the density scopes' Trace Brightness
	 * and the histogram's Histogram Gain.
	 *
	 * gain = 2^(2*D*p - D) for slider position p in [0,1], D = GainDoublings.
	 * Equal travel therefore always means the same MULTIPLICATION, the way
	 * photographic stops do, and 1.0x lands exactly at centre by construction
	 * rather than by being forced there.
	 *
	 * Resolve's own histogram slider is linear in the clip THRESHOLD instead
	 * (3.16 % of frame pixels at the left end, 1.58 % at centre, ~0 at the
	 * right end), which is why its right half is unusable: that half spans a
	 * factor of infinity while the left spans a factor of two. Deliberately
	 * not copied — see the rationale in architecture.md.
	 *
	 * The +/-3 doublings are a CHOSEN WORKING VALUE, not a measured one; M6.3
	 * Task 5 confirms or adjusts them against a real frame.
	 */
	inline constexpr float GainDoublings = 3.0f;

	/** Neutral multiplier: the slider's centre, and what a fresh install stores. */
	inline constexpr float DefaultGainMultiplier = 1.0f;

	/** Slider position -> gain multiplier. p = 0.5 returns exactly 1.0. */
	inline float SliderPositionToGain(float Position)
	{
		const float Clamped = FMath::Clamp(Position, 0.0f, 1.0f);
		return FMath::Pow(2.0f, 2.0f * GainDoublings * Clamped - GainDoublings);
	}

	/** Slider endpoints as multipliers: 0.125x .. 8x at the default +/-3 doublings. */
	inline float MinGainMultiplier() { return FMath::Pow(2.0f, -GainDoublings); }
	inline float MaxGainMultiplier() { return FMath::Pow(2.0f, GainDoublings); }

	/** Gain multiplier -> slider position, the exact inverse. */
	inline float GainToSliderPosition(float Gain)
	{
		const float Clamped = FMath::Clamp(Gain, MinGainMultiplier(), MaxGainMultiplier());
		return (FMath::Log2(Clamped) + GainDoublings) / (2.0f * GainDoublings);
	}

	/**
	 * The histogram's default clip threshold: a bin holding this fraction of
	 * the ANALYSED FRAME's total pixel count fills the band, and anything above
	 * it is hard-clipped at the band top.
	 *
	 * 1.58 % (~8 191 pixels of a 960x540 capture in one bin), measured against
	 * DaVinci Resolve on 2026-08-19 with full-height strips of known pixel
	 * fractions: strips of 0.4167 % and 0.8333 % of the frame drew at 26.3 %
	 * and 52.7 % of band height, giving 1.584 % and 1.581 % independently, and
	 * a 1.6667 % strip clipped. This REFINES the 1.67 % first measured on
	 * 2026-08-17 from narrow one-bin spikes, whose apexes are cut by line
	 * anti-aliasing when read off a screenshot — a bias that reads high. The
	 * underlying constant may well be 1/64 = 1.5625 %, which every reading
	 * brackets, but that is unproven. See architecture.md, "Histogram vertical
	 * scaling". Expressing it as a
	 * FRACTION rather than an absolute count is what makes an HD and a UHD
	 * project of the same content look identical.
	 *
	 * The figure transfers to TrueScope unrescaled even though Resolve resolves
	 * ~1024 bins across 0-1023 and TrueScope uses 256: TrueScope's capture is
	 * 8-bit (RTF_RGBA8), so its 256 bins are one bin PER DISTINCT CODE VALUE,
	 * the finest binning the data supports. Resolve's finer bins gather no
	 * additional pixels for 8-bit content — they leave three empty bins between
	 * each populated one (dev-rules.md, "Resolve cross-check") — so the count
	 * in a populated bin is the same on both sides. A bin-ratio rescale would
	 * apply only if the source carried more precision than the bin count does.
	 * Confirmed in code, M6.3 Task 0 (human decision 2026-08-18).
	 */
	inline constexpr float HistogramClipThresholdFraction = 0.0158f;

	/**
	 * Pixel count at which a histogram bin fills the band, for a frame of
	 * TotalFramePixels analysed pixels at the given user gain multiplier.
	 *
	 * Gain raises SENSITIVITY, so it divides the threshold: 8x (far right)
	 * clips at 0.20 % of frame pixels, 0.125x (far left) at 12.6 %.
	 *
	 * TotalFramePixels must be the analysed capture's pixel count
	 * (FTrueScopeAnalysisResult::SourceSize), never the viewport size and never
	 * the scope texture size — otherwise the histogram would change appearance
	 * when the user resizes the panel, which is exactly the resolution
	 * independence this design exists to provide.
	 */
	inline float HistogramClipThresholdCount(int64 TotalFramePixels, float GainMultiplier)
	{
		if (TotalFramePixels <= 0 || GainMultiplier <= 0.0f)
		{
			return 0.0f;
		}
		return HistogramClipThresholdFraction * static_cast<float>(TotalFramePixels) / GainMultiplier;
	}

	/**
	 * count -> curve HEIGHT in [0,1] for the histogram: linear against the
	 * fixed clip threshold, hard-clipped at the band top.
	 *
	 * Kept separately named from the density fields' count -> BRIGHTNESS
	 * mapping (CountToIntensity) on purpose: confusing the two is what
	 * flat-topped the M4 histogram. They differ in target (height vs
	 * brightness), in gain scope (one shared vs per-scope) and in what the clip
	 * means (band top vs white).
	 */
	inline float CountToCurveHeight(uint32 Count, float ClipThresholdCount)
	{
		if (Count == 0u || ClipThresholdCount <= 0.0f)
		{
			return 0.0f;
		}
		return FMath::Min(static_cast<float>(Count) / ClipThresholdCount, 1.0f);
	}

	/**
	 * Stable settings keys of the two gain controls (never localized, never
	 * renamed — a rename would orphan every user's stored value). Shared by the
	 * widgets that own the sliders and by the module's start-up seeding, so the
	 * two cannot drift apart.
	 */
	inline const FName TraceBrightnessGainKey(TEXT("TraceBrightness"));
	inline const FName HistogramGainKey(TEXT("HistogramGain"));

	/** The density scopes that carry their own Trace Brightness gain. */
	enum class EDensityScope : uint8
	{
		/** The RGB parade's three channel sections. */
		Parade = 0,

		/** The waveform's trace, both its RGB and its Luma mode. */
		Waveform,

		/** The vectorscope's chroma plot. */
		Vectorscope,

		Num
	};

	/**
	 * Per-scope calibrated DEFAULT gain — the absolute multiplier the slider's
	 * centre stands for. The user's stored value is a multiplier ON TOP of
	 * this (1.0 = "this scope's default"), so recalibrating a default here does
	 * not move any existing user's setting.
	 *
	 * Calibrated in M6.3 Task 5 against the measured-linear Resolve reference;
	 * per-scope because the three scopes normalise against different bases (the
	 * parade and waveform against a column's height, the vectorscope against
	 * the whole frame's pixel count) and present their values differently.
	 *
	 * PARADE and WAVEFORM = 31.25, DERIVED, not chosen. The transfer is
	 *
	 *     Intensity = min(Count * Gain / ColumnHeight, 1)          [1]
	 *
	 * so the count is already a FRACTION OF COLUMN HEIGHT by the time the gain
	 * multiplies it: writing Count = f * ColumnHeight in [1], the height
	 * cancels and full white falls at f = 1 / Gain, independent of capture
	 * resolution. Resolve was measured (2026-08-19) to reach full white when
	 * f = 3.2 % of a column holds one value — 34 pixels of 1080, 17 of
	 * TrueScope's 540 — read off a count wedge whose brightness/count ratio was
	 * constant at 6.00-6.13 across seven bands, re-confirming the linear
	 * transfer. Hence Gain = 1 / 0.032 = 31.25.
	 *
	 * ASSUMPTION, stated because it is unproven: Resolve was measured at 1080
	 * rows only, so whether its basis is the fraction of column height or an
	 * absolute pixel count cannot be told apart from that one measurement.
	 * Fraction is far more likely — it is what the histogram was measured to
	 * do, and an absolute count would make UHD material blow out relative to
	 * HD — and it is what [1] implements. The two differ by exactly 2x at our
	 * 540-row capture, so if the visual check ever comes out roughly a factor
	 * of two off, THIS assumption is what failed; do not retune around it.
	 * See backlog.md, "Waveform density basis".
	 *
	 * VECTORSCOPE = 30000, NOT measured against Resolve — derived by analogy
	 * from the parade and then confirmed by eye (M6.3 Task 5). The measured
	 * slope cannot transfer, because the vectorscope normalises against the
	 * WHOLE FRAME's pixel count while the parade normalises against a single
	 * column's height. Those bases differ by exactly the frame WIDTH, so the
	 * identical absolute pixel count draws 960x dimmer in the vectorscope than
	 * in the parade at the same gain. Undoing that gives
	 *
	 *     31.25 * 960 = 30000,
	 *
	 * i.e. one pixel in a chroma cell now reads as bright as one pixel in a
	 * parade column cell, and full white lands at 17 pixels in a cell — the
	 * same absolute count as the parade's 3.2 % of a 540-row column.
	 *
	 * Measured on a real captured frame (the 1920x1080 ColorCalibrator plate,
	 * downscaled to the 960x540 capture and binned through this exact chroma
	 * math): 3 676 of 65 536 chroma cells are populated, and the distribution
	 * is extremely skewed — median cell 3 pixels, p90 54, densest 47 017
	 * (9.1 % of the frame). At the OLD default of 1.0 the brightest cell in the
	 * whole image reached 23/255 and nothing else was visible, which is exactly
	 * the "too dim to read even at the slider's maximum" the human reported:
	 * even at 8x the maximum reached 185/255 with two cells above half
	 * brightness. At 30000 the dense neutral core clips, as it does in Resolve,
	 * and about three quarters of the populated cells are visible.
	 */
	inline float GetDefaultDensityGain(EDensityScope Scope)
	{
		switch (Scope)
		{
		case EDensityScope::Parade:      return 31.25f;
		case EDensityScope::Waveform:    return 31.25f;
		case EDensityScope::Vectorscope: return 30000.0f;
		default:                         return 1.0f;
		}
	}

	/**
	 * The density scopes' per-scope Trace Brightness gains, as published by the
	 * widgets. Passed explicitly into every mapping call rather than read from a
	 * global, so the automation tests can pin the transfer at a chosen gain and
	 * the GPU pass provably applies the same one.
	 *
	 * Lives here rather than in TrueScopeAnalysis.h (where it was until M6.4.1)
	 * so that FTrueScopeAnalysisResult can record the gains its intensity images
	 * were built with; that header cannot include TrueScopeAnalysis.h, which
	 * includes it.
	 */
	struct FDensityGains
	{
		/** Effective gain (scope default x user multiplier) per density scope. */
		float Parade = GetDefaultDensityGain(EDensityScope::Parade);
		float Waveform = GetDefaultDensityGain(EDensityScope::Waveform);
		float Vectorscope = GetDefaultDensityGain(EDensityScope::Vectorscope);

		/** The gains the three scopes carry with no user adjustment. */
		static FDensityGains Defaults() { return FDensityGains(); }

		/**
		 * Exact float equality on purpose: these values are copied, never
		 * recomputed, so equal means "the same slider positions produced both"
		 * and a tolerance could only ever hide a real difference.
		 */
		bool operator==(const FDensityGains& Other) const
		{
			return Parade == Other.Parade
				&& Waveform == Other.Waveform
				&& Vectorscope == Other.Vectorscope;
		}
		bool operator!=(const FDensityGains& Other) const { return !(*this == Other); }
	};
}

/**
 * The user's per-scope Trace Brightness gains and the shared histogram gain,
 * published by the scope widgets and read by BOTH analysis paths — the CPU
 * analysis task (a background thread) and the GPU producer's render-thread
 * pass. Modelled on FTrueScopeRasterSizes, the phase-6.1 transport for the
 * same widget -> both-paths direction.
 *
 * The values are the user's MULTIPLIERS (1.0 = the scope's calibrated
 * default), stored as atomics because the CPU analysis reads them off the game
 * thread. Relaxed ordering is enough: each gain is an independent scalar, a
 * reader only ever needs some recent value, and a gain that lands one capture
 * late is invisible.
 */
class FTrueScopeDisplayGains
{
public:
	/** Publish a density scope's Trace Brightness multiplier (game thread). */
	void SetDensityGain(TrueScope::EDensityScope Scope, float GainMultiplier)
	{
		DensityGains[static_cast<int32>(Scope)].store(GainMultiplier, std::memory_order_relaxed);
	}

	/** The multiplier last published for a density scope. */
	float GetDensityGain(TrueScope::EDensityScope Scope) const
	{
		return DensityGains[static_cast<int32>(Scope)].load(std::memory_order_relaxed);
	}

	/**
	 * The EFFECTIVE density gain: the scope's calibrated default times the
	 * user's multiplier. This is the value the transfer actually applies, and
	 * the single place the two are combined, so the CPU and GPU paths cannot
	 * drift apart on it.
	 */
	float GetEffectiveDensityGain(TrueScope::EDensityScope Scope) const
	{
		return TrueScope::GetDefaultDensityGain(Scope) * GetDensityGain(Scope);
	}

	/** Publish the shared histogram gain multiplier (game thread). */
	void SetHistogramGain(float GainMultiplier)
	{
		HistogramGain.store(GainMultiplier, std::memory_order_relaxed);
	}

	/** The shared histogram gain multiplier. */
	float GetHistogramGain() const
	{
		return HistogramGain.load(std::memory_order_relaxed);
	}

private:
	std::atomic<float> DensityGains[static_cast<int32>(TrueScope::EDensityScope::Num)] = {
		{TrueScope::DefaultGainMultiplier},
		{TrueScope::DefaultGainMultiplier},
		{TrueScope::DefaultGainMultiplier},
	};

	std::atomic<float> HistogramGain{TrueScope::DefaultGainMultiplier};
};
