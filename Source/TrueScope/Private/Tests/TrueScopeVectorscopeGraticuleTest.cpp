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

	// Radial calibration (work order 15, Fix 1): the circle is a fixed 0.8
	// chroma radius, so on screen the 100 % targets land at the spec's
	// measured Resolve radii — INSIDE the rim, and further out than the
	// matching 75 % boxes. With the old circle-at-0.5 scale, magenta's 1.19
	// would have put it far outside the rim.
	TestEqual(TEXT("The circle is the fixed 0.8 chroma radius measured from Resolve"),
		VectorscopeCircleChroma, 0.8f);
	auto ScreenRadius = [&](const TCHAR* Label, TArrayView<const FVectorscopeTarget> Targets)
	{
		for (const FVectorscopeTarget& Target : Targets)
		{
			if (FCString::Strcmp(Target.Label, Label) == 0)
			{
				return Magnitude(Target.Chroma) / VectorscopeCircleChroma;
			}
		}
		return 0.0f;
	};
	TestEqual(TEXT("100% Mg/G draw at 0.745 R (Resolve reference)"),
		ScreenRadius(TEXT("Mg"), Targets100), 0.745f, 0.005f);
	TestEqual(TEXT("100% R/Cy draw at ~0.64 R (Resolve reference)"),
		ScreenRadius(TEXT("R"), Targets100), 0.64f, 0.005f);
	TestEqual(TEXT("100% Yl/B draw at ~0.63 R (Resolve reference)"),
		ScreenRadius(TEXT("Yl"), Targets100), 0.63f, 0.005f);
	for (const FVectorscopeTarget& Target : Targets100)
	{
		TestTrue(*FString::Printf(TEXT("100%% %s draws inside the rim"), Target.Label),
			Magnitude(Target.Chroma) / VectorscopeCircleChroma < 1.0f);
		TestTrue(*FString::Printf(TEXT("100%% %s draws further out than its 75%% box"), Target.Label),
			ScreenRadius(Target.Label, Targets100) > ScreenRadius(Target.Label, Targets75));
	}
	// The whole chroma square stays inside the circle: its corner is at
	// 0.5*sqrt(2)/0.8 of the radius.
	TestTrue(TEXT("The chroma square's corners stay inside the rim"),
		0.5f * UE_SQRT_2 / VectorscopeCircleChroma < 1.0f);

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
