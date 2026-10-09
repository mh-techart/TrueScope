// Copyright 2026 Michael Herber
// SPDX-License-Identifier: Apache-2.0

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/AutomationTest.h"
#include "UI/STrueScopePanel.h"
#include "UI/TrueScopeDraw.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** The panel header's laid-out height: first row of the panel's SVerticalBox. */
	float MeasurePanelHeaderHeight(const TSharedRef<STrueScopePanel>& Panel)
	{
		Panel->SlatePrepass(1.0f);

		FChildren* Outer = Panel->GetChildren();
		if (!Outer || Outer->Num() != 1)
		{
			return -1.0f;
		}

		FChildren* Rows = Outer->GetChildAt(0)->GetChildren();
		if (!Rows || Rows->Num() < 1)
		{
			return -1.0f;
		}

		return static_cast<float>(Rows->GetChildAt(0)->GetDesiredSize().Y);
	}
}

/**
 * Phase-6.1 Task 5: below a readable size a scope shows "Panel too small"
 * instead of a broken render, and below the size where even that hint's plate
 * fits, DrawCenteredHint draws nothing rather than clipped text.
 *
 * M6.4 Task 8 re-derived the thresholds. The rule they encode is "the size at
 * which the nine value-axis labels stop colliding", so this test does not just
 * exercise the comparison — it re-measures the two font metrics and the panel
 * header the derivation is built on, and fails if any of them moves away from
 * the constants recorded in TrueScopeDraw.h. A derived number whose inputs are
 * never re-checked decays into a chosen one.
 *
 * The vectorscope is the scope that reaches the threshold first, because its
 * content rect is the largest centered square.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopePanelSizeTest,
	"TrueScope.UI.PanelSizeHint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopePanelSizeTest::RunTest(const FString&)
{
	using namespace TrueScope::Draw;

	// --- The rule ---------------------------------------------------------

	// A normal quarter of the 2x2 grid is readable.
	TestFalse(TEXT("A full-size scope canvas is readable"), IsTooSmallToRead(FVector2f(600.0f, 400.0f)));
	TestFalse(TEXT("Exactly the threshold is still readable"),
		IsTooSmallToRead(FVector2f(MinReadableCanvasWidth, MinReadableCanvasHeight)));

	// One axis short is enough: the chrome is fixed-size, so a squashed canvas
	// has no plot left even when the other axis is generous.
	TestTrue(TEXT("Too narrow counts as too small"),
		IsTooSmallToRead(FVector2f(MinReadableCanvasWidth - 1.0f, 400.0f)));
	TestTrue(TEXT("Too flat counts as too small"),
		IsTooSmallToRead(FVector2f(600.0f, MinReadableCanvasHeight - 1.0f)));
	TestTrue(TEXT("A tiny canvas counts as too small"), IsTooSmallToRead(FVector2f(40.0f, 30.0f)));

	// The aspect-locked vectorscope sees a SQUARE content rect, so a wide but
	// flat panel shrinks it in both axes and it reaches the threshold first: a
	// 600x100 canvas gives the vectorscope a 100x100 square (too small), while a
	// responsive scope of the same canvas is only short on one axis.
	const FVector2f FlatCanvas(600.0f, 100.0f);
	const float SquareSide = FMath::Min(FlatCanvas.X, FlatCanvas.Y);
	TestTrue(TEXT("The vectorscope's square is too small in a flat canvas"),
		IsTooSmallToRead(FVector2f(SquareSide, SquareSide)));

	// --- The inputs the thresholds are derived from (M6.4 Task 8) ---------

	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FSlateFontInfo LabelFont = GetLabelFont();
	const float MeasuredLineHeight = static_cast<float>(FontMeasure->GetMaxCharacterHeight(LabelFont));
	const float MeasuredWidestLabel = static_cast<float>(FontMeasure->Measure(TEXT("1023"), LabelFont).X);

	AddInfo(FString::Printf(TEXT("Label font line height %.2f, widest label \"1023\" %.2f wide"),
		MeasuredLineHeight, MeasuredWidestLabel));

	TestEqual(TEXT("Label line height still matches the recorded metric"),
		MeasuredLineHeight, LabelLineHeight, 0.01f);
	TestEqual(TEXT("Widest value label still matches the recorded metric"),
		MeasuredWidestLabel, WidestValueLabelWidth, 0.01f);
	TestEqual(TEXT("Nine labelled levels, hence eight gaps"),
		GetWaveformGraticuleLevels().Num(), ValueAxisLabelCount);
	TestEqual(TEXT("The histogram labels the same nine levels"),
		GetHistogramGraticuleLevels().Num(), ValueAxisLabelCount);

	// Width comes from the histogram (labels along X, chrome 2 x PlotPadding),
	// height from waveform/parade (labels stacked along Y, same chrome).
	TestEqual(TEXT("Width threshold is 8 label widths plus the histogram's horizontal chrome"),
		MinReadableCanvasWidth, ValueAxisLabelGaps * MeasuredWidestLabel + 2.0f * PlotPadding, 0.01f);
	TestEqual(TEXT("Height threshold is 8 label lines plus the waveform's vertical chrome"),
		MinReadableCanvasHeight, ValueAxisLabelGaps * MeasuredLineHeight + 2.0f * PlotPadding, 0.01f);

	// --- Canvas vs. panel: the header is the difference ---------------------

	const TSharedRef<STrueScopePanel> Panel = SNew(STrueScopePanel, nullptr).PanelIndex(0);
	const float MeasuredHeader = MeasurePanelHeaderHeight(Panel);
	AddInfo(FString::Printf(TEXT("Panel header desired height: %.2f"), MeasuredHeader));

	TestEqual(TEXT("The panel header still has the recorded height"),
		MeasuredHeader, PanelHeaderHeight, 0.01f);
	TestEqual(TEXT("A panel affords the canvas threshold plus its header"),
		MinReadablePanelHeight, MinReadableCanvasHeight + MeasuredHeader, 0.01f);
	TestEqual(TEXT("The header costs no width"), MinReadablePanelWidth, MinReadableCanvasWidth, 0.01f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
