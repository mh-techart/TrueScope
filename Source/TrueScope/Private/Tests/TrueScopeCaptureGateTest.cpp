// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "Capture/TrueScopeCaptureGate.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeCaptureGateTest,
	"TrueScope.Capture.TabVisibilityGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeCaptureGateTest::RunTest(const FString&)
{
	constexpr double Window = FTrueScopeCaptureGate::VisibilityWindowSeconds;

	// Never painted (tab never opened): capture must be off — this is the
	// backlog bug ("capture runs at full cost while the tab is closed").
	FTrueScopeCaptureGate Gate;
	TestFalse(TEXT("No capture before the panel ever painted"), Gate.ShouldCapture(100.0));

	// Painted just now: capture on, and it stays on within the window.
	Gate.NotifyPanelPainted(100.0);
	TestTrue(TEXT("Capture right after a paint"), Gate.ShouldCapture(100.0));
	TestTrue(TEXT("Capture within the visibility window"), Gate.ShouldCapture(100.0 + Window * 0.5));

	// Tab hidden/closed: paints stop, capture must stop after the window.
	TestFalse(TEXT("No capture once paints stopped (tab hidden or closed)"),
		Gate.ShouldCapture(100.0 + Window + 0.1));

	// Tab re-shown: the next paint resumes capture immediately.
	Gate.NotifyPanelPainted(200.0);
	TestTrue(TEXT("Capture resumes on the next paint"), Gate.ShouldCapture(200.0));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
