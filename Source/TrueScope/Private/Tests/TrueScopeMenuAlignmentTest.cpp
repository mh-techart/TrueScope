// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "Layout/ArrangedChildren.h"
#include "Layout/Geometry.h"
#include "Misc/AutomationTest.h"
#include "UI/STrueScopeHistogram.h"
#include "UI/STrueScopeMainBar.h"
#include "UI/STrueScopeParade.h"
#include "UI/STrueScopeVectorscope.h"
#include "UI/STrueScopeWaveform.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * M6.4.1 Task 2: every row of every TrueScope menu starts its text in the same
 * column, and that column is the same in all five menus.
 *
 * Measured rather than eyeballed: the menu is laid out for real and the left
 * edge of each row's label is read off the arranged geometry, so "they look
 * aligned" becomes a number. The engine's own defaults do NOT produce this —
 * see the comment on the fix in STrueScopeWidget::BuildPanelMenu — which is
 * exactly why it needs a guard.
 */

namespace
{
	struct FMenuRow
	{
		/** Which multiblock the label belongs to (menu entry, custom widget, heading). */
		FString Block;
		FString Text;
		/** Left edge of the label in the menu's own space, in Slate units. */
		float Left = 0.0f;
	};

	/** True for the block types whose labels must share one column. */
	bool IsAlignedRow(const FString& Block)
	{
		return Block.Contains(TEXT("MenuEntryBlock")) || Block.Contains(TEXT("WidgetBlock"));
	}

	/**
	 * Lays the menu out and reads the left edge of every VISIBLE label.
	 *
	 * ArrangeChildren rather than GetChildren: a multibox carries collapsed
	 * text blocks the user never sees (an empty input-chord block per entry, a
	 * hook-name block per section), and arranging with a Visible filter drops
	 * exactly those — the same rows the eye drops.
	 */
	TArray<FMenuRow> MeasureRows(const TSharedRef<SWidget>& Menu)
	{
		TArray<FMenuRow> Rows;

		Menu->SlatePrepass(1.0f);
		const FVector2D Desired(Menu->GetDesiredSize());
		const FGeometry Root = FGeometry::MakeRoot(
			FVector2D(FMath::Max(Desired.X, 320.0), FMath::Max(Desired.Y, 32.0)),
			FSlateLayoutTransform());

		TFunction<void(const TSharedRef<SWidget>&, const FGeometry&, const FString&)> Walk =
			[&Rows, &Walk](const TSharedRef<SWidget>& Node, const FGeometry& Geometry, const FString& Block)
		{
			const FString Type = Node->GetType().ToString();

			// The multibox wraps each row in a widget whose type name ends in
			// "Block" (SMenuEntryBlock, SWidgetBlock, SHeadingBlock). STextBlock
			// ends in "Block" too, but it is the label itself rather than the row
			// it sits in, so it must not claim ownership of its own row.
			const bool bIsRowBlock = Type.EndsWith(TEXT("Block")) && Type != TEXT("STextBlock");
			const FString OwningBlock = bIsRowBlock ? Type : Block;

			if (Type == TEXT("STextBlock"))
			{
				const FString Text = StaticCastSharedRef<STextBlock>(Node)->GetText().ToString();
				if (!Text.IsEmpty())
				{
					Rows.Add(FMenuRow{OwningBlock, Text,
						static_cast<float>(Geometry.AbsolutePosition.X)});
				}
			}

			FArrangedChildren Arranged(EVisibility::Visible);
			Node->ArrangeChildren(Geometry, Arranged, /*bUpdateAttributes*/ true);
			for (int32 Index = 0; Index < Arranged.Num(); ++Index)
			{
				Walk(Arranged[Index].Widget, Arranged[Index].Geometry, OwningBlock);
			}
		};

		Walk(Menu, Root, FString());
		return Rows;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeMenuAlignmentTest,
	"TrueScope.UI.MenuTextColumn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeMenuAlignmentTest::RunTest(const FString&)
{
	struct FMenuUnderTest
	{
		const TCHAR* Name;
		TSharedRef<SWidget> Menu;
	};

	TSharedRef<STrueScopeWaveform> Waveform = SNew(STrueScopeWaveform, nullptr).PanelIndex(0);
	TSharedRef<STrueScopeParade> Parade = SNew(STrueScopeParade, nullptr).PanelIndex(1);
	TSharedRef<STrueScopeVectorscope> Vectorscope = SNew(STrueScopeVectorscope, nullptr).PanelIndex(2);
	TSharedRef<STrueScopeHistogram> Histogram = SNew(STrueScopeHistogram, nullptr).PanelIndex(3);
	TSharedRef<STrueScopeMainBar> MainBar = SNew(STrueScopeMainBar);

	const FMenuUnderTest Menus[] = {
		{TEXT("Waveform"), Waveform->BuildPanelMenu()},
		{TEXT("Parade"), Parade->BuildPanelMenu()},
		{TEXT("Vectorscope"), Vectorscope->BuildPanelMenu()},
		{TEXT("Histogram"), Histogram->BuildPanelMenu()},
		{TEXT("Settings"), MainBar->BuildSettingsMenu()},
	};

	TOptional<float> FamilyColumn;
	const TCHAR* FamilySource = TEXT("");

	for (const FMenuUnderTest& Menu : Menus)
	{
		for (const FMenuRow& Row : MeasureRows(Menu.Menu))
		{
			AddInfo(FString::Printf(TEXT("%s: [%s] '%s' left=%.1f"),
				Menu.Name, *Row.Block, *Row.Text, Row.Left));

			if (!IsAlignedRow(Row.Block))
			{
				continue;   // section headings sit in their own column by design
			}

			if (!FamilyColumn.IsSet())
			{
				FamilyColumn = Row.Left;
				FamilySource = Menu.Name;
				continue;
			}

			// Half a Slate unit: the columns are built from integer paddings, so
			// anything the eye could catch is a whole unit or more.
			TestTrue(*FString::Printf(
				TEXT("%s row '%s' starts at %.1f, the same column as the %s menu (%.1f)"),
				Menu.Name, *Row.Text, Row.Left, FamilySource, FamilyColumn.GetValue()),
				FMath::IsNearlyEqual(Row.Left, FamilyColumn.GetValue(), 0.5f));
		}
	}

	TestTrue(TEXT("Some rows were measured at all"), FamilyColumn.IsSet());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
