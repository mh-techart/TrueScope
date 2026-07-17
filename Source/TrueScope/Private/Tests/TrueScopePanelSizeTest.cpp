// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"
#include "UI/TrueScopeDraw.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Phase-6.1 Task 5: below a readable size a scope shows "Panel too small"
 * instead of a broken render, and below the size where even that hint's plate
 * fits, DrawCenteredHint draws nothing rather than clipped text.
 *
 * This pins the threshold rule itself (the drawing half is the phase-4.1 hint
 * mechanism, already exercised by the paused/empty-state tests). The
 * vectorscope is the scope that reaches the threshold first, because its
 * content rect is the largest centered square.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopePanelSizeTest,
	"TrueScope.UI.PanelSizeHint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopePanelSizeTest::RunTest(const FString&)
{
	using namespace TrueScope::Draw;

	// A normal quarter of the 2x2 grid is readable.
	TestFalse(TEXT("A full-size scope panel is readable"), IsTooSmallToRead(FVector2f(600.0f, 400.0f)));
	TestFalse(TEXT("Exactly the threshold is still readable"),
		IsTooSmallToRead(FVector2f(MinReadableScopeWidth, MinReadableScopeHeight)));

	// One axis short is enough: the chrome is fixed-size, so a squashed panel
	// has no plot left even when the other axis is generous.
	TestTrue(TEXT("Too narrow counts as too small"),
		IsTooSmallToRead(FVector2f(MinReadableScopeWidth - 1.0f, 400.0f)));
	TestTrue(TEXT("Too flat counts as too small"),
		IsTooSmallToRead(FVector2f(600.0f, MinReadableScopeHeight - 1.0f)));
	TestTrue(TEXT("A tiny panel counts as too small"), IsTooSmallToRead(FVector2f(40.0f, 30.0f)));

	// The aspect-locked vectorscope sees a SQUARE content rect, so a wide but
	// flat panel shrinks it in both axes and it reaches the threshold first: a
	// 600x100 panel gives the vectorscope a 100x100 square (too small), while a
	// responsive scope of the same panel is only short on one axis.
	const FVector2f FlatPanel(600.0f, 100.0f);
	const float SquareSide = FMath::Min(FlatPanel.X, FlatPanel.Y);
	TestTrue(TEXT("The vectorscope's square is too small in a flat panel"),
		IsTooSmallToRead(FVector2f(SquareSide, SquareSide)));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
