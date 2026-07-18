// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "Analysis/TrueScopeColor.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Vectorscope graticule geometry (phase-6.2 Task 1) — pure colour math, so it
 * runs headless. Pins the angular reference frame (BT.709 Cb/Cr, CCW from
 * +Cb), the skin tone convention, the per-hue 100 % target radii and the hue
 * ring's register with the targets, against the values measured from Resolve
 * in the phase spec.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeVectorscopeGraticuleTest,
	"TrueScope.UI.VectorscopeGraticule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeVectorscopeGraticuleTest::RunTest(const FString&)
{
	using namespace TrueScope;

	// The spec's angle table is a self-check, not a source of constants: the
	// targets are computed from the BT.709 matrix, and their angles must come
	// out at the known values (phase-6.2; tolerance covers the table rounding).
	const TArrayView<const FVectorscopeTarget> Targets75 = GetVectorscopeTargets();
	const TArrayView<const FVectorscopeTarget> Targets100 = GetVectorscopeTargets100();
	TestEqual(TEXT("Six 75% targets"), Targets75.Num(), 6);
	TestEqual(TEXT("Six 100% targets"), Targets100.Num(), 6);

	struct FExpectedAngle
	{
		const TCHAR* Label;
		float Degrees;
	};
	const FExpectedAngle ExpectedAngles[] = {
		{TEXT("R"), 102.9f}, {TEXT("Mg"), 49.7f}, {TEXT("B"), 354.8f},
		{TEXT("Cy"), 282.9f}, {TEXT("G"), 229.7f}, {TEXT("Yl"), 174.8f},
	};
	for (const FExpectedAngle& Expected : ExpectedAngles)
	{
		for (const FVectorscopeTarget& Target : Targets75)
		{
			if (FCString::Strcmp(Target.Label, Expected.Label) != 0)
			{
				continue;
			}
			TestEqual(*FString::Printf(TEXT("75%% target %s sits at %.1f deg"), Expected.Label, Expected.Degrees),
				ChromaAngleDegrees(Target.Chroma), Expected.Degrees, 0.1f);
		}
		for (const FVectorscopeTarget& Target : Targets100)
		{
			if (FCString::Strcmp(Target.Label, Expected.Label) != 0)
			{
				continue;
			}
			// Chroma is linear in amplitude, so 100 % shares the 75 % angle.
			TestEqual(*FString::Printf(TEXT("100%% target %s shares the angle"), Expected.Label),
				ChromaAngleDegrees(Target.Chroma), Expected.Degrees, 0.1f);
		}
	}

	// Skin tone line: the recorded convention, plus its confirmation No. 2 —
	// a representative skin colour (sRGB 0.80/0.60/0.50) lands ON the line.
	TestEqual(TEXT("Skin tone line at the conventional 123.0 deg"), SkinToneLineAngleDegrees, 123.0f);
	TestEqual(TEXT("Representative skin colour computes to ~124.9 deg (on the line)"),
		ChromaAngleDegrees(RgbToChroma(0.80f, 0.60f, 0.50f)), 124.9f, 0.1f);

	// The 100 % targets do NOT share one radius (phase-6.2, measured from
	// Resolve): Mg/G furthest out, R/Cy nearer, Yl/B nearest. The magnitudes
	// are pinned to the BT.709-derived values (chroma units; circle = 0.5).
	auto Magnitude = [](const FChroma& Chroma)
	{
		return FMath::Sqrt(Chroma.Cb * Chroma.Cb + Chroma.Cr * Chroma.Cr);
	};
	auto TargetMagnitude = [&](const TCHAR* Label)
	{
		for (const FVectorscopeTarget& Target : Targets100)
		{
			if (FCString::Strcmp(Target.Label, Label) == 0)
			{
				return Magnitude(Target.Chroma);
			}
		}
		return 0.0f;
	};
	TestEqual(TEXT("100% magenta radius"), TargetMagnitude(TEXT("Mg")), 0.5956f, 0.001f);
	TestEqual(TEXT("100% green radius"), TargetMagnitude(TEXT("G")), 0.5956f, 0.001f);
	TestEqual(TEXT("100% red radius"), TargetMagnitude(TEXT("R")), 0.5133f, 0.001f);
	TestEqual(TEXT("100% cyan radius"), TargetMagnitude(TEXT("Cy")), 0.5133f, 0.001f);
	TestEqual(TEXT("100% yellow radius"), TargetMagnitude(TEXT("Yl")), 0.5021f, 0.001f);
	TestEqual(TEXT("100% blue radius"), TargetMagnitude(TEXT("B")), 0.5021f, 0.001f);
	TestTrue(TEXT("Magenta/green sit further out than red/cyan, which sit further out than yellow/blue"),
		TargetMagnitude(TEXT("Mg")) > TargetMagnitude(TEXT("R"))
			&& TargetMagnitude(TEXT("R")) > TargetMagnitude(TEXT("Yl")));

	// Radial calibration (work order 16): the circle IS the maximum 100 %
	// chroma magnitude — magenta/green, 0.5957 — DERIVED from the matrix, so
	// no 100 % marker can ever fall outside, by construction. A guessed 0.5
	// pushed Mg/G outside; a guessed 0.8 shrank everything (it came from
	// misreading the measured Resolve radii, which were the 75 % boxes).
	const float CircleChroma = GetVectorscopeCircleChroma();
	TestEqual(TEXT("The circle is the derived max 100% chroma (magenta/green)"),
		CircleChroma, 0.5957f, 0.0005f);
	TestEqual(TEXT("The circle equals the magenta 100% magnitude exactly"),
		CircleChroma, TargetMagnitude(TEXT("Mg")), 1.0e-6f);

	auto ScreenRadius = [&](const TCHAR* Label, TArrayView<const FVectorscopeTarget> Targets)
	{
		for (const FVectorscopeTarget& Target : Targets)
		{
			if (FCString::Strcmp(Target.Label, Label) == 0)
			{
				return Magnitude(Target.Chroma) / CircleChroma;
			}
		}
		return 0.0f;
	};

	// The work order's derived r/R table, pinned pairwise (Mg/G, R/Cy, Yl/B
	// share radii by symmetry). Magenta/green 100 % sit EXACTLY ON the circle
	// (r/R = 1.0) — correct and matching Resolve; do not "pull them inward".
	struct FExpectedRadius
	{
		const TCHAR* Label;
		float R100;
		float R75;
	};
	const FExpectedRadius ExpectedRadii[] = {
		{TEXT("Mg"), 1.0000f, 0.7500f},
		{TEXT("G"), 1.0000f, 0.7500f},
		{TEXT("R"), 0.8612f, 0.6459f},
		{TEXT("Cy"), 0.8612f, 0.6459f},
		{TEXT("Yl"), 0.8429f, 0.6322f},
		{TEXT("B"), 0.8429f, 0.6322f},
	};
	for (const FExpectedRadius& Expected : ExpectedRadii)
	{
		TestEqual(*FString::Printf(TEXT("100%% %s draws at %.4f R"), Expected.Label, Expected.R100),
			ScreenRadius(Expected.Label, Targets100), Expected.R100, 0.0005f);
		TestEqual(*FString::Printf(TEXT("75%% %s draws at %.4f R"), Expected.Label, Expected.R75),
			ScreenRadius(Expected.Label, Targets75), Expected.R75, 0.0005f);
	}
	for (const FVectorscopeTarget& Target : Targets100)
	{
		// On the rim is legal (Mg/G); beyond it is not — no real colour can
		// exceed the max 100 % chroma, so neither may a marker.
		TestTrue(*FString::Printf(TEXT("100%% %s never exceeds the rim"), Target.Label),
			Magnitude(Target.Chroma) / CircleChroma <= 1.0f + 1.0e-4f);
	}

	// Hue ring register (phase-6.2 Task 1b): stops are generated from the same
	// Cb/Cr formula as the targets, so the stop nearest each 100 % target's
	// angle must carry (approximately) that target's own hue — the ring cannot
	// be mis-rotated. A hand-placed HSV wheel (uniform angular spacing) fails
	// this: its red would sit at hue fraction 0 mapped to a uniform angle, not
	// at 102.9 deg.
	TArray<FHueRingStop> Stops;
	BuildHueRingStops(180, Stops);
	TestEqual(TEXT("Hue ring carries the requested number of stops"), Stops.Num(), 180);

	struct FExpectedHue
	{
		const TCHAR* Label;
		FLinearColor Rgb;
	};
	const FExpectedHue ExpectedHues[] = {
		{TEXT("R"), FLinearColor(1.0f, 0.0f, 0.0f)},
		{TEXT("Mg"), FLinearColor(1.0f, 0.0f, 1.0f)},
		{TEXT("B"), FLinearColor(0.0f, 0.0f, 1.0f)},
		{TEXT("Cy"), FLinearColor(0.0f, 1.0f, 1.0f)},
		{TEXT("G"), FLinearColor(0.0f, 1.0f, 0.0f)},
		{TEXT("Yl"), FLinearColor(1.0f, 1.0f, 0.0f)},
	};
	for (const FExpectedHue& Expected : ExpectedHues)
	{
		const float TargetAngle = ChromaAngleDegrees(
			RgbToChroma(Expected.Rgb.R, Expected.Rgb.G, Expected.Rgb.B));

		float BestDelta = 360.0f;
		FLinearColor BestColor = FLinearColor::Black;
		for (const FHueRingStop& Stop : Stops)
		{
			float StopDegrees = FMath::RadiansToDegrees(Stop.AngleRadians);
			StopDegrees = StopDegrees < 0.0f ? StopDegrees + 360.0f : StopDegrees;
			const float Delta = FMath::Abs(FMath::FindDeltaAngleDegrees(StopDegrees, TargetAngle));
			if (Delta < BestDelta)
			{
				BestDelta = Delta;
				BestColor = Stop.Color;
			}
		}

		// 180 stops in hue -> at most ~2.4 deg between adjacent stop angles
		// around the primaries, so the nearest stop sits well within 3 deg and
		// its colour is within one hue step (1/180 hue -> component delta 6/180).
		TestTrue(*FString::Printf(TEXT("Ring stop within 3 deg of the %s target"), Expected.Label),
			BestDelta < 3.0f);
		TestTrue(*FString::Printf(TEXT("Ring colour at the %s target is the target's hue"), Expected.Label),
			FMath::Abs(BestColor.R - Expected.Rgb.R) < 0.05f
				&& FMath::Abs(BestColor.G - Expected.Rgb.G) < 0.05f
				&& FMath::Abs(BestColor.B - Expected.Rgb.B) < 0.05f);
	}

	// The non-uniform spacing the spec calls out falls out of the formula:
	// red->yellow spans ~72 deg, yellow->green only ~55 deg. An HSV wheel
	// would make both 60 deg.
	const float RedToYellow = FMath::Abs(FMath::FindDeltaAngleDegrees(
		ChromaAngleDegrees(RgbToChroma(1.0f, 0.0f, 0.0f)), ChromaAngleDegrees(RgbToChroma(1.0f, 1.0f, 0.0f))));
	const float YellowToGreen = FMath::Abs(FMath::FindDeltaAngleDegrees(
		ChromaAngleDegrees(RgbToChroma(1.0f, 1.0f, 0.0f)), ChromaAngleDegrees(RgbToChroma(0.0f, 1.0f, 0.0f))));
	TestEqual(TEXT("Red->yellow spans ~72 deg"), RedToYellow, 71.9f, 0.2f);
	TestEqual(TEXT("Yellow->green spans ~55 deg"), YellowToGreen, 54.9f, 0.2f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
