// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "Analysis/TrueScopeAnalysis.h"
#include "Analysis/TrueScopeAnalysisResult.h"
#include "Analysis/TrueScopeDisplayGains.h"
#include "Misc/AutomationTest.h"
#include "UI/STrueScopeHistogram.h"
#include "UI/TrueScopeDraw.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The value scale and the histogram's channel layout are pure rules, so they can
 * be asserted headless — no Slate application, no RHI. They pin down two of the
 * M4 acceptance criteria that a screenshot would only show approximately.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeValueScaleTest,
	"TrueScope.UI.ValueScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeValueScaleTest::RunTest(const FString&)
{
	using namespace TrueScope::Draw;

	TestEqual(TEXT("Scopes share the normalized 10-bit full scale"), ScaleMax, 1023);

	// Waveform / parade: 8 divisions, i.e. a line every 128 code values, with
	// the last line on full scale rather than one step beyond it.
	const TArrayView<const int32> WaveformLevels = GetWaveformGraticuleLevels();
	TestEqual(TEXT("Waveform axis has 8 divisions"), WaveformLevels.Num(), 9);
	for (int32 Index = 0; Index < WaveformLevels.Num() - 1; ++Index)
	{
		TestEqual(*FString::Printf(TEXT("Waveform gridline %d sits at %d"), Index, 128 * Index),
			WaveformLevels[Index], 128 * Index);
	}
	TestEqual(TEXT("Waveform axis ends on full scale"), WaveformLevels.Last(), ScaleMax);

	// Histogram: the SAME axis since M6.4 Task 7, not a coarser one — three
	// scopes labelling every 128 and the fourth every 256 made the user read
	// two different scales for one set of code values.
	const TArrayView<const int32> HistogramLevels = GetHistogramGraticuleLevels();
	TestEqual(TEXT("Histogram axis has the same number of divisions as the waveform"),
		HistogramLevels.Num(), WaveformLevels.Num());
	for (int32 Index = 0; Index < HistogramLevels.Num(); ++Index)
	{
		TestEqual(*FString::Printf(TEXT("Histogram level %d matches the waveform's"), Index),
			HistogramLevels[Index], WaveformLevels[Index]);
	}
	TestEqual(TEXT("Histogram axis starts at zero"), HistogramLevels[0], 0);
	TestEqual(TEXT("Histogram axis ends on full scale"), HistogramLevels.Last(), ScaleMax);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeScaleMinorTicksTest,
	"TrueScope.UI.ScaleMinorTicks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeScaleMinorTicksTest::RunTest(const FString&)
{
	using namespace TrueScope::Draw;

	// Resolve's scales show three short intermediate ticks between labelled
	// majors (phase-6.2 Task 3); the rule is pure, so pin it headless.
	TestEqual(TEXT("Three minor ticks per division"), MinorTicksPerDivision, 3);

	// Between two histogram majors (0 and 256): quarters of the gap.
	TArray<float> Values;
	GetMinorTickValues(0, 256, Values);
	TestEqual(TEXT("A division yields three tick values"), Values.Num(), 3);
	if (Values.Num() == 3)
	{
		TestEqual(TEXT("First tick sits at the quarter"), Values[0], 64.0f, 0.001f);
		TestEqual(TEXT("Second tick sits at the half"), Values[1], 128.0f, 0.001f);
		TestEqual(TEXT("Third tick sits at three quarters"), Values[2], 192.0f, 0.001f);
	}

	// The last waveform division ends on 1023, not 1024 — the ticks must
	// subdivide the real (uneven) gap, not an assumed one.
	Values.Reset();
	GetMinorTickValues(896, ScaleMax, Values);
	TestEqual(TEXT("Uneven final division still yields three ticks"), Values.Num(), 3);
	if (Values.Num() == 3)
	{
		TestTrue(TEXT("Ticks stay inside the division"),
			Values[0] > 896.0f && Values[2] < static_cast<float>(ScaleMax));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeHistogramChannelLayoutTest,
	"TrueScope.UI.HistogramChannelLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeHistogramChannelLayoutTest::RunTest(const FString&)
{
	constexpr int32 NumChannels = FTrueScopeAnalysisResult::NumChannels;
	const FSlateRect Plot(10.0f, 20.0f, 210.0f, 320.0f);

	// Overlay mode: every channel gets the whole plot, so the three curves are
	// drawn superimposed on identical geometry — the same data, denser view.
	for (int32 Channel = 0; Channel < NumChannels; ++Channel)
	{
		const FSlateRect Rect =
			STrueScopeHistogram::GetChannelPlotRect(Plot, ETrueScopeHistogramMode::RgbOverlay, Channel);
		TestTrue(*FString::Printf(TEXT("Overlay channel %d fills the plot"), Channel), Rect == Plot);
	}

	// Separate mode (the default): three equal, disjoint bands stacked R, G, B
	// from the top, sharing the plot's full width and reaching its bottom.
	FSlateRect Bands[NumChannels];
	for (int32 Channel = 0; Channel < NumChannels; ++Channel)
	{
		Bands[Channel] = STrueScopeHistogram::GetChannelPlotRect(Plot, ETrueScopeHistogramMode::SeparateChannels, Channel);

		TestEqual(*FString::Printf(TEXT("Band %d spans the plot width (left)"), Channel), Bands[Channel].Left, Plot.Left);
		TestEqual(*FString::Printf(TEXT("Band %d spans the plot width (right)"), Channel), Bands[Channel].Right, Plot.Right);
		TestTrue(*FString::Printf(TEXT("Band %d has positive height"), Channel), Bands[Channel].GetSize().Y > 0.0f);
	}

	TestEqual(TEXT("Red band starts at the top of the plot"), Bands[0].Top, Plot.Top);
	TestEqual(TEXT("Blue band ends at the bottom of the plot"), Bands[2].Bottom, Plot.Bottom);
	TestEqual(TEXT("Green and red bands have equal height"),
		Bands[1].GetSize().Y, Bands[0].GetSize().Y);
	TestEqual(TEXT("Blue and red bands have equal height"),
		Bands[2].GetSize().Y, Bands[0].GetSize().Y);

	// Stacked top-down and never overlapping: this is what lets a channel
	// imbalance be read off directly instead of hiding behind another curve.
	TestTrue(TEXT("Red band is above the green band"), Bands[0].Bottom < Bands[1].Top);
	TestTrue(TEXT("Green band is above the blue band"), Bands[1].Bottom < Bands[2].Top);

	// A degenerate plot must not produce inverted rects.
	const FSlateRect Tiny(0.0f, 0.0f, 4.0f, 4.0f);
	for (int32 Channel = 0; Channel < NumChannels; ++Channel)
	{
		const FSlateRect Rect =
			STrueScopeHistogram::GetChannelPlotRect(Tiny, ETrueScopeHistogramMode::SeparateChannels, Channel);
		TestTrue(*FString::Printf(TEXT("Tiny plot band %d is not inverted"), Channel), Rect.Bottom >= Rect.Top);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeHistogramChannelAssignmentTest,
	"TrueScope.UI.HistogramChannelAssignment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeHistogramChannelAssignmentTest::RunTest(const FString&)
{
	// The gap the M4 analysis tests left open: they stop at the bin arrays, this
	// one covers which bin array is drawn as which coloured curve. A red/blue
	// swap in GetChannelBins fails here — the values are distinct and asymmetric,
	// so the peaks cannot line up by accident.
	using FResult = FTrueScopeAnalysisResult;
	constexpr int32 NumChannels = FResult::NumChannels;
	constexpr int32 Width = 32;
	constexpr int32 Height = 16;
	constexpr uint8 Levels[NumChannels] = {64, 128, 192};

	TArray<FColor> Pixels;
	Pixels.Init(FColor(Levels[0], Levels[1], Levels[2], 255), Width * Height);

	FResult Result;
	TrueScope::AnalyzePixels(Pixels, Width, Height, Result);

	auto PeakBin = [](TArrayView<const uint32> Bins)
	{
		int32 Peak = 0;
		for (int32 Bin = 1; Bin < Bins.Num(); ++Bin)
		{
			if (Bins[Bin] > Bins[Peak])
			{
				Peak = Bin;
			}
		}
		return Peak;
	};

	for (int32 Channel = 0; Channel < NumChannels; ++Channel)
	{
		const TArrayView<const uint32> Bins = STrueScopeHistogram::GetChannelBins(Result, Channel);
		TestEqual(*FString::Printf(TEXT("Curve %d is fed its own channel's bins"), Channel),
			PeakBin(Bins), (int32)Levels[Channel]);

		// ...and is drawn in that channel's colour: the curve peaking at 64 must
		// be the red one. Binding and tint have to agree, or the scope lies.
		const FLinearColor Line = STrueScopeHistogram::GetChannelLineColor(Channel);
		const FLinearColor Fill = STrueScopeHistogram::GetChannelFillColor(Channel);
		const float LineComponents[3] = {Line.R, Line.G, Line.B};
		const float FillComponents[3] = {Fill.R, Fill.G, Fill.B};
		for (int32 Other = 0; Other < NumChannels; ++Other)
		{
			if (Other == Channel)
			{
				continue;
			}
			TestTrue(*FString::Printf(TEXT("Curve %d line colour is dominated by component %d"), Channel, Channel),
				LineComponents[Channel] > LineComponents[Other]);
			TestTrue(*FString::Printf(TEXT("Curve %d fill colour is dominated by component %d"), Channel, Channel),
				FillComponents[Channel] > FillComponents[Other]);
		}
	}

	// Both display modes read the same binding — overlay only changes geometry,
	// which is why the swap hypothesis would have been invisible there. Assert
	// each channel's peak lands at the same X in either mode.
	const FSlateRect Plot(0.0f, 0.0f, 255.0f, 300.0f);
	for (int32 Channel = 0; Channel < NumChannels; ++Channel)
	{
		const int32 Peak = PeakBin(STrueScopeHistogram::GetChannelBins(Result, Channel));
		const FSlateRect Stacked =
			STrueScopeHistogram::GetChannelPlotRect(Plot, ETrueScopeHistogramMode::SeparateChannels, Channel);
		const FSlateRect Overlay =
			STrueScopeHistogram::GetChannelPlotRect(Plot, ETrueScopeHistogramMode::RgbOverlay, Channel);

		const float StackedX = Stacked.Left + Stacked.GetSize().X * (Peak / float(FResult::NumBins - 1));
		const float OverlayX = Overlay.Left + Overlay.GetSize().X * (Peak / float(FResult::NumBins - 1));
		TestEqual(*FString::Printf(TEXT("Channel %d peaks at the same value in both modes"), Channel),
			OverlayX, StackedX);
		TestEqual(*FString::Printf(TEXT("Channel %d peak sits on its own value"), Channel),
			StackedX, (float)Levels[Channel]);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeHistogramCurveScaleTest,
	"TrueScope.UI.HistogramCurveScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeHistogramCurveScaleTest::RunTest(const FString&)
{
	// The histogram's vertical axis is a pixel count, so the curve height must be
	// proportional to it. Regression guard for the M4 review bug: the density
	// mapping (right for the parade's texel *brightness*) let a handful of
	// outlier pixels decide where a channel appeared to end.
	//
	// Since M6.3 the scale is a FIXED clip threshold rather than the frame's
	// peak: a count equal to the threshold fills the band, more clips there, and
	// the scale is the same for every curve and every frame of the same size.
	//
	// DERIVED, never a free literal. This used to be 14556.0f — a count off the
	// frame the M4 bug was found on — which asserted the mapping against a number
	// unrelated to the scale that ships, and left a reader two candidate "the
	// threshold" values to reconcile. Reading the production constant instead
	// makes every assertion below a statement about the shipping scale (the
	// 116-pixel outlier further down is now measured against the real band).
	//
	// Exactly one test spells the documented 1.58 % out as a literal, as a
	// deliberate guard on architecture.md (TrueScope.UI.HistogramClipThreshold).
	// Every other test derives, this one included: a second hand-written copy of
	// the same number does not add a check, it only adds something that can drift.
	constexpr int64 FramePixels = 960 * 540;   // the shipping capture size
	const float Threshold =
		TrueScope::HistogramClipThresholdCount(FramePixels, TrueScope::DefaultGainMultiplier);

	// The threshold is a fraction of a pixel count and so is not a whole number
	// (8 190.72 px for 960x540); the smallest bin count that reaches it is the
	// ceiling, which is what "a bin at the threshold" means for integer counts.
	const uint32 ThresholdCount = static_cast<uint32>(FMath::CeilToInt(Threshold));

	TestEqual(TEXT("Empty bin has no height"), STrueScopeHistogram::GetCurveHeight(0, Threshold), 0.0f);
	TestEqual(TEXT("No threshold draws nothing"), STrueScopeHistogram::GetCurveHeight(10, 0.0f), 0.0f);
	TestEqual(TEXT("A bin at the threshold fills the band"),
		STrueScopeHistogram::GetCurveHeight(ThresholdCount, Threshold), 1.0f);
	TestEqual(TEXT("One pixel past the threshold clips at the band top"),
		STrueScopeHistogram::GetCurveHeight(ThresholdCount + 1, Threshold), 1.0f);
	TestEqual(TEXT("Ten times the threshold still just clips"),
		STrueScopeHistogram::GetCurveHeight(10 * ThresholdCount, Threshold), 1.0f);
	TestEqual(TEXT("Half the threshold is half the height"),
		STrueScopeHistogram::GetCurveHeight(ThresholdCount / 2, Threshold), 0.5f, 0.001f);
	TestEqual(TEXT("One percent of the threshold is one percent of the height"),
		STrueScopeHistogram::GetCurveHeight(ThresholdCount / 100, Threshold), 0.01f, 0.001f);

	// The exact bin that inverted the apparent R/B order against Resolve.
	constexpr uint32 OutlierCount = 116;
	const float Height = STrueScopeHistogram::GetCurveHeight(OutlierCount, Threshold);
	TestTrue(TEXT("A 116-pixel outlier bin stays under 2 % of the band height"), Height < 0.02f);

	// Spell out what the density mapping would have done, so nobody swaps the
	// two mappings back (the M4 bug): at unity gain it draws that same bin at
	// full height, because a density texel normalises against a column, not a
	// frame.
	const float DensityIntensity = TrueScope::CountToIntensity(OutlierCount, 540, 1.0f);
	TestTrue(TEXT("The density mapping would have drawn that bin far taller"),
		DensityIntensity > 10.0f * Height);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
