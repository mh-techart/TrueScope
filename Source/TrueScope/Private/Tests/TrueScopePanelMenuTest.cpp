// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "Capture/TrueScopeViewExtension.h"
#include "InputCoreTypes.h"
#include "Misc/AutomationTest.h"
#include "TrueScopeModule.h"
#include "UI/STrueScopeHistogram.h"
#include "UI/STrueScopeMainBar.h"
#include "UI/STrueScopePanel.h"
#include "UI/STrueScopeParade.h"
#include "UI/STrueScopeVectorscope.h"
#include "UI/STrueScopeWaveform.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * M6.4 Tasks 4-5: what each panel menu offers, and what the settings popup does.
 *
 * The menu is the only place a panel's options live now, so "the vectorscope's
 * markers ended up in the histogram's menu" is a shipping bug rather than a
 * cosmetic one — and it is invisible to every other test, which drives the
 * scopes through their programmatic API rather than through the menu. This
 * builds the real menu widgets and reads the rows off the built Slate tree.
 */

namespace
{
	/**
	 * The user-visible rows of a built menu, in order.
	 *
	 * A multibox also carries text blocks the user never sees: an empty or
	 * "None" input-chord block per entry, and one block per section holding the
	 * section's extension-hook NAME. The hook names are internal identifiers, so
	 * they are filtered by the caller's ignore list rather than pinned as rows.
	 */
	TArray<FString> CollectMenuRows(const TSharedRef<SWidget>& Widget, const TArray<FString>& Ignored)
	{
		TArray<FString> Rows;

		TFunction<void(const TSharedRef<SWidget>&)> Walk = [&Rows, &Ignored, &Walk](const TSharedRef<SWidget>& Node)
		{
			if (Node->GetType() == TEXT("STextBlock"))
			{
				const FString Text = StaticCastSharedRef<STextBlock>(Node)->GetText().ToString();
				// Case-SENSITIVE: FString comparison ignores case by default, and
				// a section's hook name differs from its heading in exactly that
				// ("Markers" vs. the heading "MARKERS") — a case-insensitive
				// filter silently swallowed the heading row this test exists to
				// check.
				const bool bIgnored = Ignored.ContainsByPredicate([&Text](const FString& Name)
				{
					return Text.Equals(Name, ESearchCase::CaseSensitive);
				});
				if (!Text.IsEmpty() && Text != TEXT("None") && !bIgnored)
				{
					Rows.Add(Text);
				}
			}

			FChildren* Children = Node->GetChildren();
			for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
			{
				Walk(Children->GetChildAt(Index));
			}
		};

		Walk(Widget);
		return Rows;
	}

	/** The internal identifiers the builder uses for its sections. */
	TArray<FString> GetSectionHookNames(const TSharedRef<STrueScopeWidget>& Scope)
	{
		TArray<FString> Names;
		Names.Add(TEXT("TrueScopeDisplayModes"));
		for (const FTrueScopeOptionGroup& Group : Scope->GetOptionGroups())
		{
			Names.Add(Group.Key.ToString());
		}
		return Names;
	}

	FString Describe(const TArray<FString>& Rows)
	{
		return FString::Join(Rows, TEXT(" | "));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopePanelMenuContentsTest,
	"TrueScope.UI.PanelMenuContents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopePanelMenuContentsTest::RunTest(const FString&)
{
	// Section headings are drawn in capitals by the engine's menu style; the
	// text below is what the built tree actually holds.
	auto CheckMenu = [this](const TSharedRef<STrueScopeWidget>& Scope, const TCHAR* What,
		const TArray<FString>& Expected)
	{
		const TArray<FString> Rows = CollectMenuRows(Scope->BuildPanelMenu(), GetSectionHookNames(Scope));
		TestEqual(FString::Printf(TEXT("%s panel menu rows"), What), Describe(Rows), Describe(Expected));
	};

	CheckMenu(SNew(STrueScopeWaveform, nullptr).PanelIndex(0), TEXT("Waveform"),
		{TEXT("DISPLAY"), TEXT("RGB"), TEXT("Luma"), TEXT("Trace Brightness"), TEXT("Reset to Defaults")});

	// Deliberately thin: parade display modes are planned in architecture.md,
	// not shipped, so the menu invents none.
	CheckMenu(SNew(STrueScopeParade, nullptr).PanelIndex(1), TEXT("Parade"),
		{TEXT("Trace Brightness"), TEXT("Reset to Defaults")});

	CheckMenu(SNew(STrueScopeVectorscope, nullptr).PanelIndex(2), TEXT("Vectorscope"),
		{TEXT("ZOOM"), TEXT("Normal"), TEXT("2× Zoom"),
		 TEXT("MARKERS"), TEXT("75%"), TEXT("100%"), TEXT("75% + 100%"), TEXT("Show Skin Tone Line"),
		 TEXT("Trace Brightness"), TEXT("Reset to Defaults")});

	CheckMenu(SNew(STrueScopeHistogram, nullptr).PanelIndex(3), TEXT("Histogram"),
		{TEXT("DISPLAY"), TEXT("Separate Channels RGB"), TEXT("Separate Channels YRGB"), TEXT("RGB Overlay"),
		 TEXT("Histogram Gain"), TEXT("Reset to Defaults")});

	// No panel menu carries a global option any more (M6.4 Task 5): the
	// analysis path and the capture rate live in the main bar's settings popup,
	// so each is reachable from exactly one place. Asserted across all four
	// menus at once, since the section used to be added to every one of them.
	const TArray<TSharedRef<STrueScopeWidget>> AllScopes = {
		SNew(STrueScopeWaveform, nullptr).PanelIndex(0),
		SNew(STrueScopeParade, nullptr).PanelIndex(1),
		SNew(STrueScopeVectorscope, nullptr).PanelIndex(2),
		SNew(STrueScopeHistogram, nullptr).PanelIndex(3),
	};
	for (const TSharedRef<STrueScopeWidget>& Scope : AllScopes)
	{
		const FString Rows = Describe(CollectMenuRows(Scope->BuildPanelMenu(), GetSectionHookNames(Scope)));
		TestFalse(TEXT("No panel menu offers the analysis path"), Rows.Contains(TEXT("GPU")));
		TestFalse(TEXT("No panel menu offers the analysis path"), Rows.Contains(TEXT("CPU")));
		TestFalse(TEXT("No panel menu offers the capture rate"), Rows.Contains(TEXT("Hz")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopePanelMenuSingleEntryPointTest,
	"TrueScope.UI.PanelMenuSingleEntryPoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopePanelMenuSingleEntryPointTest::RunTest(const FString&)
{
	// M6.4.1 Task 3 repurposed this test. M6.4 shipped two entry points to the
	// panel menu — the header button and a right-click on the scope canvas —
	// and this asserted the two agreed. With the right-click removed there is
	// no second list to compare against, so a test that still only compared
	// would pass for the wrong reason. It now pins the two halves of "one
	// option, one place":
	//   1. the panel header still delegates to the scope's builder rather than
	//      growing a list of its own, and
	//   2. right-clicking a scope canvas opens nothing — deliberate, not an
	//      oversight, so nobody "fixes" the handler back in.
	TSharedRef<STrueScopePanel> Panel = SNew(STrueScopePanel, nullptr).PanelIndex(2);
	const TSharedPtr<STrueScopeWidget> Scope = Panel->GetScopeWidget();
	if (!TestTrue(TEXT("The panel built its scope"), Scope.IsValid()))
	{
		return false;
	}

	const TArray<FString> Ignored = GetSectionHookNames(Scope.ToSharedRef());
	const TArray<FString> FromHeader = CollectMenuRows(Panel->BuildPanelMenu(), Ignored);
	const TArray<FString> FromScope = CollectMenuRows(Scope->BuildPanelMenu(), Ignored);

	TestEqual(TEXT("The header menu is the scope's own menu"),
		Describe(FromHeader), Describe(FromScope));
	TestTrue(TEXT("...and that list is not empty"), FromHeader.Num() > 0);

	// A right-click delivered straight to the scope widget. Unhandled means the
	// scope declined it and no menu was pushed; the pre-M6.4.1 handler returned
	// Handled from both, having captured the mouse and pushed the menu.
	const FGeometry Geometry = FGeometry::MakeRoot(FVector2f(400.0f, 300.0f), FSlateLayoutTransform());
	const FPointerEvent RightClick(
		/*PointerIndex*/ 0, FVector2f(10.0f, 10.0f), FVector2f(10.0f, 10.0f),
		TSet<FKey>{EKeys::RightMouseButton}, EKeys::RightMouseButton,
		/*WheelDelta*/ 0.0f, FModifierKeysState());

	TestFalse(TEXT("Right-press on the scope canvas is not handled"),
		Scope->OnMouseButtonDown(Geometry, RightClick).IsEventHandled());
	TestFalse(TEXT("Right-release on the scope canvas is not handled"),
		Scope->OnMouseButtonUp(Geometry, RightClick).IsEventHandled());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeSettingsPopupContentsTest,
	"TrueScope.UI.SettingsPopupContents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeSettingsPopupContentsTest::RunTest(const FString&)
{
	// M6.4 Task 5: the settings popup holds the two options that belong to no
	// single panel, and nothing else. "Nothing else" is the half that rots — a
	// per-scope control added here would be invisible to every other test, and
	// would reintroduce exactly the crowded main bar this phase removed.
	TSharedRef<STrueScopeMainBar> MainBar = SNew(STrueScopeMainBar);
	const TArray<FString> Ignored = {TEXT("TrueScopeAnalysisPath"), TEXT("TrueScopeCaptureRate")};
	const TArray<FString> Rows = CollectMenuRows(MainBar->BuildSettingsMenu(), Ignored);

	TArray<FString> Expected = {
		TEXT("ANALYSIS PATH (ALL SCOPES)"), TEXT("GPU (performance)"), TEXT("CPU (compatibility)"),
		TEXT("CAPTURE RATE")};
	// The rates are read from the capture side's shortlist rather than re-listed,
	// so this pins "the popup offers exactly the implemented rates", not a copy
	// of them that can drift.
	for (float RateHz : TrueScope::MeasurementRatesHz)
	{
		// Culture-aware, like the popup itself: on a German locale the row reads
		// "2,5 Hz", and a "%g"-formatted expectation failed here for that reason
		// alone. What is pinned is the rate SET and the "Hz" suffix, not the
		// decimal separator.
		Expected.Add(FText::Format(NSLOCTEXT("TrueScopeMenuTest", "RateFormat", "{0} Hz"),
			FText::AsNumber(RateHz, &FNumberFormattingOptions()
				.SetMinimumFractionalDigits(0)
				.SetMaximumFractionalDigits(1))).ToString());
	}

	TestEqual(TEXT("Settings popup rows"), Describe(Rows), Describe(Expected));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeSettingsPopupEffectivePathTest,
	"TrueScope.UI.SettingsPopupEffectivePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeSettingsPopupEffectivePathTest::RunTest(const FString&)
{
	// The popup must show the path that actually produced the numbers on screen,
	// so a diagnostic TrueScope.AnalysisPath override moves the radio dot. The
	// old main-bar buttons read the same function; this pins that the popup that
	// replaced them did not quietly switch to the remembered UI choice.
	IConsoleVariable* PathOverride =
		IConsoleManager::Get().FindConsoleVariable(TEXT("TrueScope.AnalysisPath"));
	if (!TestNotNull(TEXT("The diagnostic CVar exists"), PathOverride))
	{
		return false;
	}

	TSharedRef<STrueScopeMainBar> MainBar = SNew(STrueScopeMainBar);
	const int32 SavedOverride = PathOverride->GetInt();

	PathOverride->Set(1, ECVF_SetByCode);
	if (!FTrueScopeModule::Get().GetEffectiveUseGpuAnalysis())
	{
		// No live view extension (commandlet/-nullrhi without the capture side):
		// the effective path cannot be moved, so there is nothing to compare.
		AddInfo(TEXT("Skipped: no live view extension, so the effective path cannot be driven here."));
		PathOverride->Set(SavedOverride, ECVF_SetByCode);
		return true;
	}

	TestTrue(TEXT("Override to GPU selects the GPU entry"), MainBar->IsAnalysisPathSelected(true));
	TestFalse(TEXT("Override to GPU deselects the CPU entry"), MainBar->IsAnalysisPathSelected(false));

	PathOverride->Set(0, ECVF_SetByCode);
	TestTrue(TEXT("Override to CPU selects the CPU entry"), MainBar->IsAnalysisPathSelected(false));
	TestFalse(TEXT("Override to CPU deselects the GPU entry"), MainBar->IsAnalysisPathSelected(true));

	PathOverride->Set(SavedOverride, ECVF_SetByCode);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
