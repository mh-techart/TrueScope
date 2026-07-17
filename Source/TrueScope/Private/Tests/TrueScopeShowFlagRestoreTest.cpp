// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "Capture/TrueScopeViewExtension.h"
#include "Editor/UnrealEdTypes.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Pins the stale-show-flag condition behind the ortho -> placed-camera resume
 * fix (work order 13 Fix 1). The freeze it guards against: switching an
 * orthographic viewport straight to a CineCameraActor sets only the actor
 * lock, so the viewport renders perspective while PostProcessing is still off
 * from the ortho state, and the capture hook is never subscribed — the scopes
 * hold the last ortho-era frame until a Perspective-and-back dance.
 *
 * The condition must fire for exactly that state and must NOT fire for a
 * deliberate Show > Post Processing uncheck in a normal perspective viewport
 * (stored type Perspective), which the user owns.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeShowFlagRestoreTest,
	"TrueScope.Capture.ShowFlagRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeShowFlagRestoreTest::RunTest(const FString& Parameters)
{
	using TrueScope::IsStaleOrthoShowFlagState;

	// The frozen state from the live repro (2026-07-17): stored type still
	// ortho, camera lock makes the effective type perspective, PostProcessing
	// carried over as off. Every ortho type must resume, not just Top.
	for (ELevelViewportType Stored : {LVT_OrthoXY, LVT_OrthoNegativeXY, LVT_OrthoXZ,
		LVT_OrthoNegativeXZ, LVT_OrthoYZ, LVT_OrthoNegativeYZ, LVT_OrthoFreelook})
	{
		TestTrue(FString::Printf(TEXT("stale ortho state resumes (stored type %d)"), int32(Stored)),
			IsStaleOrthoShowFlagState(Stored, LVT_Perspective, /*bPostProcessing*/ false));
	}

	// A deliberate Show > Post Processing uncheck in a perspective viewport is
	// the user's choice: stored type IS Perspective, must not be overridden.
	TestFalse(TEXT("post-processing unchecked in a perspective viewport is left alone"),
		IsStaleOrthoShowFlagState(LVT_Perspective, LVT_Perspective, false));

	// Healthy states never trigger a restore.
	TestFalse(TEXT("healthy perspective view does not trigger"),
		IsStaleOrthoShowFlagState(LVT_Perspective, LVT_Perspective, true));
	TestFalse(TEXT("plain ortho view does not trigger (no signal is correct there)"),
		IsStaleOrthoShowFlagState(LVT_OrthoXY, LVT_OrthoXY, false));

	// Ortho view whose view mode legitimately keeps post-processing on
	// (e.g. Lit ortho): nothing stale, nothing to restore.
	TestFalse(TEXT("ortho with post-processing on does not trigger"),
		IsStaleOrthoShowFlagState(LVT_OrthoXY, LVT_OrthoXY, true));

	// Camera lock straight out of ortho where the flags DID come back on
	// (already healed): must not keep firing.
	TestFalse(TEXT("healed camera-lock state does not re-trigger"),
		IsStaleOrthoShowFlagState(LVT_OrthoXY, LVT_Perspective, true));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
