// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "Capture/TrueScopeViewExtension.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/AutomationTest.h"
#include "Rendering/DrawElements.h"
#include "UI/STrueScopeWaveform.h"
#include "UI/TrueScopeScopeState.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWindow.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * M4.1 paused indicator. The bug class these guard against does not live in
 * AnalyzePixels but one layer up, in *when* the widget decides it is paused —
 * so they drive the widget's state machine, not pixel arrays.
 *
 * The two failure modes worth pinning are opposite ones:
 *
 * - Too narrow: the hint never appears, and a viewport that dropped out during
 *   PIE looks like a frozen scope (the whole reason this phase exists).
 * - Too broad: the hint appears whenever PIE runs, even though another focused
 *   viewport is still feeding the scopes — it would then sit over a *live*
 *   scope and lie about it.
 *
 * Both directions are crossed below.
 */

namespace
{
	/** ~2.5 Hz capture: a result this fresh means the scopes are live. */
	constexpr double FreshResultAgeSeconds = 0.1;

	/** Well past PausedAfterMissedIntervals * 0.4 s — the viewport has stopped delivering. */
	constexpr double StaleResultAgeSeconds = 5.0;

	/**
	 * A real scope widget (so the base class's wiring is what gets tested) with the
	 * one editor-global input replaced: whether a PIE session is running.
	 */
	class STrueScopePausedTestScope : public STrueScopeWaveform
	{
	public:
		void SetPlayInEditorActive(bool bInActive)
		{
			bPlayInEditorActive = bInActive;
		}

		/** Drive the state as the active timer would, without an editor tick. */
		void DriveState(double NowSeconds, double SecondsSinceLastResult)
		{
			SetLastResultTimeSecondsForTesting(NowSeconds - SecondsSinceLastResult);
			UpdateScopeState(NowSeconds);
		}

		/** The protected paint branch every scope's OnPaint ends in. */
		int32 DrawPausedHintForTesting(FSlateWindowElementList& OutDrawElements, int32 LayerId,
			const FGeometry& AllottedGeometry, const FSlateRect& Content) const
		{
			return DrawPausedHint(OutDrawElements, LayerId, AllottedGeometry, Content);
		}

	protected:
		virtual bool IsPlayInEditorActive() const override
		{
			return bPlayInEditorActive;
		}

	private:
		bool bPlayInEditorActive = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopePausedStateTest,
	"TrueScope.UI.PausedState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopePausedStateTest::RunTest(const FString&)
{
	using namespace TrueScope;

	// --- The decision itself, as a pure rule ------------------------------------
	// The rule is expressed in capture intervals, so it holds at any measurement
	// rate; the shipped default is the natural case to pin it at.
	const double Interval = DefaultCaptureIntervalSeconds;
	const double Stale = PausedAfterMissedIntervals * Interval + 1.0;

	TestTrue(TEXT("No PIE, results flowing: live"),
		DetermineScopeState(false, FreshResultAgeSeconds, Interval) == EScopeState::Live);

	// The "no viewport focused" idle state: nothing arrives, but no play session
	// is running — this keeps its existing silent presentation, it is not paused.
	TestTrue(TEXT("No PIE, nothing arriving (no viewport): still live, not paused"),
		DetermineScopeState(false, Stale, Interval) == EScopeState::Live);

	// The too-broad guard: PIE runs, but a focused viewport keeps feeding us.
	TestTrue(TEXT("PIE running while another viewport still delivers: live, no hint"),
		DetermineScopeState(true, FreshResultAgeSeconds, Interval) == EScopeState::Live);

	// The case the phase exists for: the measured viewport entered PIE.
	TestTrue(TEXT("PIE running and results stopped: paused"),
		DetermineScopeState(true, Stale, Interval) == EScopeState::PausedInPie);

	// The hint must not flicker on the normal capture cadence: a single missed
	// interval is not yet a pause.
	TestTrue(TEXT("PIE running, one missed interval: not yet paused"),
		DetermineScopeState(true, Interval * 1.5, Interval) == EScopeState::Live);

	// --- The same states, on a real scope widget --------------------------------
	TSharedRef<STrueScopePausedTestScope> Scope = SNew(STrueScopePausedTestScope, nullptr);
	constexpr double Now = 1000.0;

	Scope->SetPlayInEditorActive(false);
	Scope->DriveState(Now, FreshResultAgeSeconds);
	TestFalse(TEXT("Widget: live scope draws no hint"), Scope->ShouldDrawPausedHint());

	Scope->SetPlayInEditorActive(true);
	Scope->DriveState(Now, FreshResultAgeSeconds);
	TestFalse(TEXT("Widget: PIE but still delivering draws no hint"), Scope->ShouldDrawPausedHint());

	Scope->DriveState(Now, StaleResultAgeSeconds);
	TestTrue(TEXT("Widget: measured viewport in PIE shows the hint"), Scope->ShouldDrawPausedHint());

	// Resuming — by stopping PIE, or by focusing another viewport that delivers
	// again — must clear the hint. Both are crossed, because the paused state is a
	// conjunction and either input alone can clear it.
	Scope->DriveState(Now, FreshResultAgeSeconds);
	TestFalse(TEXT("Widget: results resume (other viewport focused) clears the hint"),
		Scope->ShouldDrawPausedHint());

	Scope->DriveState(Now, StaleResultAgeSeconds);
	TestTrue(TEXT("Widget: paused again"), Scope->ShouldDrawPausedHint());
	Scope->SetPlayInEditorActive(false);
	Scope->DriveState(Now, StaleResultAgeSeconds);
	TestFalse(TEXT("Widget: PIE stopped clears the hint"), Scope->ShouldDrawPausedHint());

	// --- The paint branch -------------------------------------------------------
	// A live scope must add no draw element at all: this is the tooth on
	// DrawPausedHint's own guard, not just on the state that feeds it.
	const FGeometry Geometry = FGeometry::MakeRoot(FVector2f(400.0f, 300.0f), FSlateLayoutTransform());
	const FSlateRect Content(0.0f, 0.0f, 400.0f, 300.0f);
	constexpr int32 BaseLayer = 7;

	{
		const TSharedPtr<SWindow> NoWindow;
		FSlateWindowElementList Elements(NoWindow);
		TestEqual(TEXT("Paint: live scope leaves the layer untouched (draws nothing)"),
			Scope->DrawPausedHintForTesting(Elements, BaseLayer, Geometry, Content), BaseLayer);
	}

	// Drawing the hint needs the font-measure service, so it is only asserted when
	// a Slate application exists (it does in the editor; a bare headless run has
	// no renderer). The state assertions above hold either way.
	if (FSlateApplication::IsInitialized())
	{
		Scope->SetPlayInEditorActive(true);
		Scope->DriveState(Now, StaleResultAgeSeconds);

		const TSharedPtr<SWindow> NoWindow;
		FSlateWindowElementList Elements(NoWindow);
		TestTrue(TEXT("Paint: paused scope draws the hint above the held trace"),
			Scope->DrawPausedHintForTesting(Elements, BaseLayer, Geometry, Content) > BaseLayer);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
