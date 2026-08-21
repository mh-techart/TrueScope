// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"
#include "UI/STrueScopeHistogram.h"
#include "UI/STrueScopePanel.h"
#include "UI/STrueScopeParade.h"
#include "UI/STrueScopeVectorscope.h"
#include "UI/STrueScopeWaveform.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * M6.4 Task 4: what each panel menu offers.
 *
 * The menu is the only place a panel's options live now, so "the vectorscope's
 * markers ended up in the histogram's menu" is a shipping bug rather than a
 * cosmetic one — and it is invisible to every other test, which drives the
 * scopes through their programmatic API rather than through the menu. This
 * builds the real menu widget and reads the rows off the built Slate tree.
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
		{TEXT("DISPLAY"), TEXT("RGB"), TEXT("Luma"), TEXT("Trace Brightness")});

	// Deliberately thin: parade display modes are planned in architecture.md,
	// not shipped, so the menu invents none.
	CheckMenu(SNew(STrueScopeParade, nullptr).PanelIndex(1), TEXT("Parade"),
		{TEXT("Trace Brightness")});

	CheckMenu(SNew(STrueScopeVectorscope, nullptr).PanelIndex(2), TEXT("Vectorscope"),
		{TEXT("ZOOM"), TEXT("Normal"), TEXT("2× Zoom"),
		 TEXT("MARKERS"), TEXT("75%"), TEXT("100%"), TEXT("75% + 100%"), TEXT("Show Skin Tone Line"),
		 TEXT("Trace Brightness")});

	CheckMenu(SNew(STrueScopeHistogram, nullptr).PanelIndex(3), TEXT("Histogram"),
		{TEXT("DISPLAY"), TEXT("Separate Channels RGB"), TEXT("Separate Channels YRGB"), TEXT("RGB Overlay"),
		 TEXT("Histogram Gain")});

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopePanelMenuEntryPointsTest,
	"TrueScope.UI.PanelMenuEntryPoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopePanelMenuEntryPointsTest::RunTest(const FString&)
{
	// The panel header's menu button and the canvas's right-click menu are the
	// same menu. They are the same function today; this pins that the header
	// keeps delegating to the scope rather than growing a second list beside it.
	TSharedRef<STrueScopePanel> Panel = SNew(STrueScopePanel, nullptr).PanelIndex(2);
	const TSharedPtr<STrueScopeWidget> Scope = Panel->GetScopeWidget();
	if (!TestTrue(TEXT("The panel built its scope"), Scope.IsValid()))
	{
		return false;
	}

	const TArray<FString> Ignored = GetSectionHookNames(Scope.ToSharedRef());
	const TArray<FString> FromHeader = CollectMenuRows(Panel->BuildPanelMenu(), Ignored);
	const TArray<FString> FromCanvas = CollectMenuRows(Scope->BuildPanelMenu(), Ignored);

	TestEqual(TEXT("Header menu and right-click menu list the same rows"),
		Describe(FromHeader), Describe(FromCanvas));
	TestTrue(TEXT("...and that list is not empty"), FromHeader.Num() > 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
