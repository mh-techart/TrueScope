// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "Analysis/TrueScopeAnalysisResult.h"
#include "Analysis/TrueScopeDisplayGains.h"
#include "Misc/AutomationTest.h"
#include "TrueScopeSettings.h"
#include "UI/STrueScopeGainSlider.h"
#include "UI/STrueScopeHistogram.h"
#include "UI/STrueScopeWaveform.h"
#include "UI/TrueScopeType.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * M6.3: the shared gain-slider law, the histogram's fixed clip threshold, and
 * the persistence contract (what gets stored is the MULTIPLIER, not the slider
 * position and not the absolute gain).
 *
 * These are pinned rather than left to the UI because all three are numbers a
 * later change could plausibly "improve" by eye: the law's centre is the one
 * position at which the scopes agree with the reference, the threshold is the
 * measured 1.67 % of the frame, and storing the multiplier is what lets a
 * default be recalibrated without moving an existing user's setting.
 */

namespace
{
	using FResult = FTrueScopeAnalysisResult;
}

// ---------------------------------------------------------------------------
// The slider law
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeGainSliderLawTest,
	"TrueScope.UI.GainSliderLaw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeGainSliderLawTest::RunTest(const FString&)
{
	// The centre is EXACTLY unity. Not "about 1.0": it is the calibrated
	// default, the one setting the Resolve cross-check was fitted at, so a
	// user who returns to the centre must return to the calibration.
	TestTrue(TEXT("The centre is exactly 1.0x (bit-exact, not merely close)"),
		TrueScope::SliderPositionToGain(0.5f) == 1.0f);

	// Endpoints: +/-3 doublings.
	TestEqual(TEXT("Far left is 1/8x"), TrueScope::SliderPositionToGain(0.0f), 0.125f, 1e-6f);
	TestEqual(TEXT("Far right is 8x"), TrueScope::SliderPositionToGain(1.0f), 8.0f, 1e-6f);
	TestEqual(TEXT("MinGainMultiplier agrees with the left end"),
		TrueScope::MinGainMultiplier(), 0.125f, 1e-6f);
	TestEqual(TEXT("MaxGainMultiplier agrees with the right end"),
		TrueScope::MaxGainMultiplier(), 8.0f, 1e-6f);

	// Logarithmic: EQUAL TRAVEL is equal MULTIPLICATION, everywhere on the
	// track. A linear law would fail this — that is the point of the test.
	constexpr float Travel = 1.0f / 6.0f;   // one doubling of the six
	for (float Start = 0.0f; Start <= 1.0f - Travel + 1e-4f; Start += 0.1f)
	{
		const float Ratio =
			TrueScope::SliderPositionToGain(Start + Travel) / TrueScope::SliderPositionToGain(Start);
		TestEqual(*FString::Printf(TEXT("A sixth of the track doubles the gain (from %.2f)"), Start),
			Ratio, 2.0f, 1e-4f);
	}

	// The inverse round-trips, so the toolbar can place the handle from a
	// stored gain without a second copy of the law.
	for (float Position = 0.0f; Position <= 1.0f + 1e-4f; Position += 0.125f)
	{
		const float Clamped = FMath::Min(Position, 1.0f);
		TestEqual(*FString::Printf(TEXT("Position %.3f round-trips"), Clamped),
			TrueScope::GainToSliderPosition(TrueScope::SliderPositionToGain(Clamped)), Clamped, 1e-5f);
	}
	TestEqual(TEXT("Unity maps back to the centre"), TrueScope::GainToSliderPosition(1.0f), 0.5f, 1e-6f);

	// Out-of-range input is clamped rather than extrapolated (a stored value
	// from a future build with a wider range must not brighten past white).
	TestEqual(TEXT("Below the range clamps to the left end"),
		TrueScope::SliderPositionToGain(-1.0f), TrueScope::MinGainMultiplier(), 1e-6f);
	TestEqual(TEXT("Above the range clamps to the right end"),
		TrueScope::SliderPositionToGain(2.0f), TrueScope::MaxGainMultiplier(), 1e-6f);
	TestEqual(TEXT("A gain past the range maps to the right end"),
		TrueScope::GainToSliderPosition(1000.0f), 1.0f, 1e-6f);

	// The centre detent: near the middle snaps to the exact middle, so the
	// calibrated default is reachable by hand and not merely by double-click.
	TestTrue(TEXT("Just left of centre snaps to the exact centre"),
		STrueScopeGainSlider::SnapToCentre(0.5f - 0.5f * STrueScopeGainSlider::CentreSnapRadius)
			== STrueScopeGainSlider::CentrePosition);
	TestTrue(TEXT("Just right of centre snaps to the exact centre"),
		STrueScopeGainSlider::SnapToCentre(0.5f + 0.5f * STrueScopeGainSlider::CentreSnapRadius)
			== STrueScopeGainSlider::CentrePosition);
	const float OutsideDetent = 0.5f + 2.0f * STrueScopeGainSlider::CentreSnapRadius;
	TestTrue(TEXT("Outside the detent the position is untouched"),
		STrueScopeGainSlider::SnapToCentre(OutsideDetent) == OutsideDetent);

	return true;
}

// ---------------------------------------------------------------------------
// The histogram's fixed clip threshold
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeHistogramClipThresholdTest,
	"TrueScope.UI.HistogramClipThreshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeHistogramClipThresholdTest::RunTest(const FString&)
{
	// The shipping capture: 960x540 = 518400 analysed pixels.
	constexpr int64 FramePixels = 960 * 540;
	const float Threshold =
		TrueScope::HistogramClipThresholdCount(FramePixels, TrueScope::DefaultGainMultiplier);

	// THE ONE PLACE the documented literal is repeated. This assertion exists to
	// guard architecture.md ("Histogram vertical scaling"): 1.67 % of the frame's
	// pixels in one bin fills the band, measured against DaVinci Resolve on
	// 2026-08-17. Changing HistogramClipThresholdFraction without changing the
	// documentation must fail here, which is only possible if the number is
	// spelled out rather than derived.
	//
	// Every OTHER test reads the threshold from the production constant instead
	// (TrueScope.UI.HistogramCurveScale, .HistogramYrgb, the GPU equivalence
	// tests) — a second hand-written copy of the same number would only drift.
	TestEqual(TEXT("The default threshold is the documented 1.67 % of the frame (guards architecture.md)"),
		Threshold, 0.0167f * static_cast<float>(FramePixels), 0.5f);

	const uint32 ThresholdCount = static_cast<uint32>(FMath::RoundToInt(Threshold));
	TestEqual(TEXT("A bin at the threshold fills the band"),
		TrueScope::CountToCurveHeight(ThresholdCount, Threshold), 1.0f, 1e-4f);
	TestTrue(TEXT("One pixel more clips at the band top"),
		TrueScope::CountToCurveHeight(ThresholdCount + 1, Threshold) == 1.0f);
	TestEqual(TEXT("Half the threshold is half the band"),
		TrueScope::CountToCurveHeight(ThresholdCount / 2, Threshold), 0.5f, 1e-3f);
	TestTrue(TEXT("An empty bin has no height"),
		TrueScope::CountToCurveHeight(0, Threshold) == 0.0f);

	// RESOLUTION INDEPENDENCE: the same fraction of the frame draws the same
	// height at any capture size, because the threshold is a fraction of the
	// frame rather than an absolute count.
	constexpr int64 BigFramePixels = 1920 * 1080;
	const float BigThreshold =
		TrueScope::HistogramClipThresholdCount(BigFramePixels, TrueScope::DefaultGainMultiplier);
	TestEqual(TEXT("Four times the pixels, four times the threshold"),
		BigThreshold, 4.0f * Threshold, 0.5f);
	TestEqual(TEXT("The same frame fraction draws the same height at either size"),
		TrueScope::CountToCurveHeight(static_cast<uint32>(0.01f * FramePixels), Threshold),
		TrueScope::CountToCurveHeight(static_cast<uint32>(0.01f * BigFramePixels), BigThreshold), 1e-3f);

	// The gain moves the threshold by its RECIPROCAL: more gain means a
	// smaller count already fills the band (the curves grow).
	TestEqual(TEXT("Gain 2 halves the threshold"),
		TrueScope::HistogramClipThresholdCount(FramePixels, 2.0f), 0.5f * Threshold, 0.5f);
	TestEqual(TEXT("Gain 1/2 doubles the threshold"),
		TrueScope::HistogramClipThresholdCount(FramePixels, 0.5f), 2.0f * Threshold, 0.5f);

	// The slider's documented span, stated as the frame fractions the phase
	// spec names: 13.3 % far left, 1.67 % centre, 0.21 % far right.
	auto FractionAtPosition = [FramePixels](float Position)
	{
		const float Gain = TrueScope::SliderPositionToGain(Position);
		return TrueScope::HistogramClipThresholdCount(FramePixels, Gain) / static_cast<float>(FramePixels);
	};
	TestEqual(TEXT("Far left fills the band at 13.3 % of the frame"), FractionAtPosition(0.0f), 0.1336f, 1e-4f);
	TestEqual(TEXT("The centre fills the band at 1.67 % of the frame"), FractionAtPosition(0.5f), 0.0167f, 1e-5f);
	TestEqual(TEXT("Far right fills the band at 0.21 % of the frame"), FractionAtPosition(1.0f), 0.0020875f, 1e-6f);

	// The widget derives the same number from a result's SourceSize — the
	// analysed capture, never the panel size.
	FResult Result;
	Result.SourceSize = FIntPoint(960, 540);
	TestTrue(TEXT("The widget's threshold comes from the analysed frame size"),
		STrueScopeHistogram::GetClipThresholdCount(Result, TrueScope::DefaultGainMultiplier) == Threshold);

	FResult Empty;
	Empty.SourceSize = FIntPoint::ZeroValue;
	TestTrue(TEXT("An unanalysed frame has no threshold"),
		STrueScopeHistogram::GetClipThresholdCount(Empty, TrueScope::DefaultGainMultiplier) == 0.0f);

	return true;
}

// ---------------------------------------------------------------------------
// Transport and persistence
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeGainPersistenceTest,
	"TrueScope.UI.GainPersistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeGainPersistenceTest::RunTest(const FString&)
{
	// The transport: what the widget publishes is the multiplier, and what the
	// analysis reads is default x multiplier. Keeping the two apart is what
	// makes a default recalibratable without touching a stored setting.
	FTrueScopeDisplayGains Gains;
	TestTrue(TEXT("The multiplier starts at exactly unity"),
		Gains.GetDensityGain(TrueScope::EDensityScope::Parade) == TrueScope::DefaultGainMultiplier);
	TestTrue(TEXT("The effective gain starts at the scope's default"),
		Gains.GetEffectiveDensityGain(TrueScope::EDensityScope::Parade)
			== TrueScope::GetDefaultDensityGain(TrueScope::EDensityScope::Parade));

	Gains.SetDensityGain(TrueScope::EDensityScope::Parade, 4.0f);
	TestTrue(TEXT("The stored value is the multiplier itself"),
		Gains.GetDensityGain(TrueScope::EDensityScope::Parade) == 4.0f);
	TestEqual(TEXT("The effective gain is default x multiplier"),
		Gains.GetEffectiveDensityGain(TrueScope::EDensityScope::Parade),
		4.0f * TrueScope::GetDefaultDensityGain(TrueScope::EDensityScope::Parade), 1e-6f);
	TestTrue(TEXT("The other scopes are untouched"),
		Gains.GetDensityGain(TrueScope::EDensityScope::Waveform) == TrueScope::DefaultGainMultiplier);

	Gains.SetHistogramGain(0.25f);
	TestTrue(TEXT("The histogram gain is stored as given"), Gains.GetHistogramGain() == 0.25f);

	// Persistence: the settings object round-trips the MULTIPLIER. The stored
	// number must be the slider's multiplier, not its position (0.5 would then
	// come back as a gain) and not the absolute gain (which would freeze the
	// calibration of the build it was saved on).
	UTrueScopeSettings& Settings = UTrueScopeSettings::Get();
	float Saved = 0.0f;
	const bool bHadValue =
		Settings.GetScopeGainMultiplier(ETrueScopeType::Parade, TrueScope::TraceBrightnessGainKey, Saved);

	constexpr float StoredMultiplier = 2.0f;
	Settings.SetScopeGainMultiplier(ETrueScopeType::Parade, TrueScope::TraceBrightnessGainKey, StoredMultiplier);

	float Restored = 0.0f;
	TestTrue(TEXT("The stored gain is found again"),
		Settings.GetScopeGainMultiplier(ETrueScopeType::Parade, TrueScope::TraceBrightnessGainKey, Restored));
	TestTrue(TEXT("What comes back is the multiplier"), Restored == StoredMultiplier);
	TestTrue(TEXT("...and not the slider position that produced it"),
		!FMath::IsNearlyEqual(Restored, TrueScope::GainToSliderPosition(StoredMultiplier), 1e-4f));

	// ...and the number the WIDGET writes is that same multiplier. Asserted
	// through a real scope widget rather than the settings API alone, because
	// the two plausible wrong things to persist — the slider position, or the
	// effective gain (default x multiplier) — both round-trip through the
	// settings object perfectly well. Only the widget layer can tell them
	// apart, and only the multiplier survives a Task 5 recalibration of the
	// scope's default.
	float SavedWaveform = 0.0f;
	const bool bWaveformStored =
		Settings.GetScopeGainMultiplier(ETrueScopeType::Waveform, TrueScope::TraceBrightnessGainKey, SavedWaveform);
	{
		TSharedRef<STrueScopeWaveform> Scope = SNew(STrueScopeWaveform, nullptr);
		Scope->ApplyGainMultiplier(StoredMultiplier);

		float WidgetStored = 0.0f;
		TestTrue(TEXT("The widget persists its gain"),
			Settings.GetScopeGainMultiplier(ETrueScopeType::Waveform, TrueScope::TraceBrightnessGainKey, WidgetStored));
		TestEqual(TEXT("The widget stores the multiplier, not the slider position"),
			WidgetStored, StoredMultiplier, 1e-6f);
		// The other wrong thing to persist — the EFFECTIVE gain, default x
		// multiplier — is indistinguishable from the multiplier while every
		// scope default is still 1.0 (Task 5 sets them). So it is not asserted
		// here under a condition that cannot fail; the guard is the assertion
		// below that the widget's own multiplier is what was stored, together
		// with the separation of the two in GetEffectiveDensityGain, covered
		// above.
		TestEqual(TEXT("The widget reports back the multiplier it stored"),
			Scope->GetGainMultiplier(), StoredMultiplier, 1e-6f);
	}
	if (bWaveformStored)
	{
		Settings.SetScopeGainMultiplier(ETrueScopeType::Waveform, TrueScope::TraceBrightnessGainKey, SavedWaveform);
	}
	else
	{
		Settings.SetScopeGainMultiplier(ETrueScopeType::Waveform, TrueScope::TraceBrightnessGainKey,
			TrueScope::DefaultGainMultiplier);
	}

	// Per-scope keys are distinct: setting the parade must not move the
	// waveform's slider (the per-scope independence Task 2 requires).
	float WaveformValue = 0.0f;
	const bool bWaveformHadValue =
		Settings.GetScopeGainMultiplier(ETrueScopeType::Waveform, TrueScope::TraceBrightnessGainKey, WaveformValue);
	TestTrue(TEXT("The waveform's stored gain is independent of the parade's"),
		!bWaveformHadValue || !FMath::IsNearlyEqual(WaveformValue, StoredMultiplier));

	// Leave the user's configuration as it was found.
	if (bHadValue)
	{
		Settings.SetScopeGainMultiplier(ETrueScopeType::Parade, TrueScope::TraceBrightnessGainKey, Saved);
	}
	else
	{
		Settings.SetScopeGainMultiplier(ETrueScopeType::Parade, TrueScope::TraceBrightnessGainKey,
			TrueScope::DefaultGainMultiplier);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
