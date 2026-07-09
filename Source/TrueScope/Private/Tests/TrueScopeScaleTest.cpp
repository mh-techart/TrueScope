// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

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

	// Histogram: same axis, quartered.
	const TArrayView<const int32> HistogramLevels = GetHistogramGraticuleLevels();
	TestEqual(TEXT("Histogram axis has 4 divisions"), HistogramLevels.Num(), 5);
	TestEqual(TEXT("Histogram axis starts at zero"), HistogramLevels[0], 0);
	TestEqual(TEXT("Histogram axis ends on full scale"), HistogramLevels.Last(), ScaleMax);

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

#endif // WITH_DEV_AUTOMATION_TESTS
