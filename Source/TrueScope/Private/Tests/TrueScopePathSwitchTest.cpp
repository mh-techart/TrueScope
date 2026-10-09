// Copyright 2026 Michael Herber
// SPDX-License-Identifier: Apache-2.0

/*
	Regression test for the switch/state layer of the path-switch responsiveness
	fix (phase-5.1 Task 1). DecidePathSwitchAction is the decision core: it says
	whether a switch triggers the prompt re-analysis of the resident last
	capture, and through which producer. Pinning it guards the two ways the fix
	can silently regress:

	- the re-analysis stops firing on a real switch (the switch appears dead
	  again whenever the viewport is not Realtime), or
	- it fires when it must not (cold start: analysing the RT's initial clear
	  would fabricate a measurement; no-change: every menu click would burn a
	  redundant analysis — and in the GPU->CPU case a forbidden extra readback).
*/

#include "Capture/TrueScopePathSwitch.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopePathSwitchDecisionTest,
	"TrueScope.Capture.PathSwitchDecision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopePathSwitchDecisionTest::RunTest(const FString&)
{
	using TrueScope::DecidePathSwitchAction;
	using TrueScope::EPathSwitchAction;

	// The two real switches with a captured frame present: each direction must
	// re-analyse through the NEWLY selected path — GPU compute for CPU->GPU,
	// the one-time seeding readback for GPU->CPU (never swapped).
	TestEqual(TEXT("CPU->GPU with a captured frame re-analyses on the GPU"),
		DecidePathSwitchAction(/*bOldGpu*/ false, /*bNewGpu*/ true, /*bHasCapturedFrame*/ true),
		EPathSwitchAction::ReanalyzeGpu);
	TestEqual(TEXT("GPU->CPU with a captured frame seeds the CPU path via the one-time readback"),
		DecidePathSwitchAction(true, false, true),
		EPathSwitchAction::SeedCpuReadback);

	// Cold-start guard: no capture has ever run, so there is no last frame —
	// a switch must do nothing (in particular no readback of the cleared RT).
	TestEqual(TEXT("CPU->GPU without a captured frame does nothing"),
		DecidePathSwitchAction(false, true, false), EPathSwitchAction::None);
	TestEqual(TEXT("GPU->CPU without a captured frame does nothing"),
		DecidePathSwitchAction(true, false, false), EPathSwitchAction::None);

	// No effective change (e.g. re-clicking the active menu entry, or a CVar
	// set to the value already in effect) must not trigger any analysis,
	// regardless of capture state.
	for (const bool bHasCaptured : {false, true})
	{
		TestEqual(TEXT("CPU->CPU does nothing"),
			DecidePathSwitchAction(false, false, bHasCaptured), EPathSwitchAction::None);
		TestEqual(TEXT("GPU->GPU does nothing"),
			DecidePathSwitchAction(true, true, bHasCaptured), EPathSwitchAction::None);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
