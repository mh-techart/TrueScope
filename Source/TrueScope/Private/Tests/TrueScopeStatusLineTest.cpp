// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"
#include "UI/TrueScopeStatus.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FString StatusText(TrueScope::EMeasurementState State, bool bUseGpu = false,
		const FText& Source = FText::FromString(TEXT("Perspective")))
	{
		TrueScope::FMeasurementStatus Status;
		Status.State = State;
		Status.Source = Source;
		Status.DisplayEncoding = TrueScope::GetDefaultDisplayEncoding();
		Status.bUseGpu = bUseGpu;
		return TrueScope::FormatStatusLine(Status).ToString();
	}
}

/**
 * Phase-6.1 Task 4: the status line's job is to leave no state unexplained.
 * These pin what the user is actually told in each state — a status line that
 * silently says nothing in one of them would be the bug the line exists to fix
 * (blank scopes in an ortho view were exactly that).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeStatusLineTest,
	"TrueScope.UI.StatusLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeStatusLineTest::RunTest(const FString&)
{
	using namespace TrueScope;

	// Measuring: source, display encoding and the active path, all three.
	{
		const FString Text = StatusText(EMeasurementState::Measuring, /*bUseGpu*/ true);
		TestTrue(TEXT("Measuring names the source"), Text.Contains(TEXT("Perspective")));
		TestTrue(TEXT("Measuring names the display encoding"), Text.Contains(TEXT("Rec.709")));
		TestTrue(TEXT("Measuring names the active path"), Text.Contains(TEXT("GPU")));
	}

	// The path shown is the one running, not a fixed label.
	{
		const FString Text = StatusText(EMeasurementState::Measuring, /*bUseGpu*/ false);
		TestTrue(TEXT("CPU path is named as such"), Text.Contains(TEXT("CPU")));
		TestFalse(TEXT("CPU path does not claim GPU"), Text.Contains(TEXT("GPU")));
	}

	// A piloted camera is the truthful name of the source.
	{
		const FString Text = StatusText(EMeasurementState::Measuring, /*bUseGpu*/ false,
			FText::FromString(TEXT("CineCameraActor")));
		TestTrue(TEXT("A piloted actor is named as the source"), Text.Contains(TEXT("CineCameraActor")));
	}

	// Ortho: the logged M4.1 gap — say that nothing is measured, and how to fix it.
	{
		const FString Text = StatusText(EMeasurementState::Orthographic, /*bUseGpu*/ false,
			FText::FromString(TEXT("Top")));
		TestTrue(TEXT("Ortho says there is no signal"), Text.Contains(TEXT("No signal")));
		TestTrue(TEXT("Ortho names the view"), Text.Contains(TEXT("Top")));
		TestTrue(TEXT("Ortho says how to resume"), Text.Contains(TEXT("Perspective")));
	}

	// Paused: consistent with the in-panel paused hint (PIE, values held).
	{
		const FString Text = StatusText(EMeasurementState::PausedInPie);
		TestTrue(TEXT("Paused says it is paused"), Text.Contains(TEXT("Paused")));
		TestTrue(TEXT("Paused names PIE as the reason"), Text.Contains(TEXT("PIE")));
	}

	// No viewport: the existing empty state, now named.
	{
		const FString Text = StatusText(EMeasurementState::NoViewport);
		TestTrue(TEXT("No viewport says there is no signal"), Text.Contains(TEXT("No signal")));
		TestTrue(TEXT("No viewport says what is missing"), Text.Contains(TEXT("viewport")));
	}

	// No state may be silent.
	for (const EMeasurementState State : {EMeasurementState::Measuring, EMeasurementState::NoViewport,
		EMeasurementState::Orthographic, EMeasurementState::PausedInPie})
	{
		TestTrue(TEXT("Every state says something"), !StatusText(State).IsEmpty());
	}

	// OCIO destination naming (work order 13 Fix 3): must mirror the OCIO
	// plugin's own GetDestinationString — display-view pair first, colour
	// space as the fallback, silence when nothing is configured.
	TestEqual(TEXT("Display-view pair wins"),
		TrueScope::ComposeOcioDestinationName(TEXT("sRGB - Display"), TEXT("ACES 1.0 - SDR Video"), TEXT("Rec709")),
		FString(TEXT("sRGB - Display - ACES 1.0 - SDR Video")));
	TestEqual(TEXT("Colour space is the fallback"),
		TrueScope::ComposeOcioDestinationName(FString(), FString(), TEXT("Rec709")),
		FString(TEXT("Rec709")));
	TestEqual(TEXT("Half a display-view falls back to the colour space"),
		TrueScope::ComposeOcioDestinationName(TEXT("sRGB - Display"), FString(), TEXT("Rec709")),
		FString(TEXT("Rec709")));
	TestEqual(TEXT("Nothing configured stays silent"),
		TrueScope::ComposeOcioDestinationName(FString(), FString(), FString()),
		FString());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
