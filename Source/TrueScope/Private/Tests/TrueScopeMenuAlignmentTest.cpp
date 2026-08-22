// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "Layout/ArrangedChildren.h"
#include "Layout/Geometry.h"
#include "Misc/AutomationTest.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "UI/STrueScopeHistogram.h"
#include "UI/STrueScopeMainBar.h"
#include "UI/STrueScopeParade.h"
#include "UI/STrueScopeVectorscope.h"
#include "UI/STrueScopeWaveform.h"
#include "UI/TrueScopeStyle.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * M6.4.1: every row of every TrueScope menu starts its text in the same column
 * (Task 2), and within one menu every icon, radio dot and check box starts in
 * the same column too (menu polish, Task 1).
 *
 * Measured rather than eyeballed: the menus are laid out for real and the left
 * edge of each row's label and of its leftmost glyph are read off the arranged
 * geometry, so "they look aligned" becomes a number. The engine's own defaults
 * produce neither column — a menu's checkmark column widens with the entry
 * types it holds, and an icon set on an entry pushes its label right — which is
 * why both need a guard. The derivations are in TrueScopeStyle.cpp and on
 * STrueScopeWidget::BuildResetRow.
 *
 * The two columns are guarded on different scopes on purpose: the text column
 * is one column for ALL menus, because they must read as one family, while the
 * glyph column is per menu, because it sits where that menu's own check widgets
 * put it. Teeth proven 2026-08-22 by moving each independently (the label
 * indent, then the icon pull) and watching only the matching half go red.
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
		/**
		 * How far the row's visible content is inset from the top and bottom of
		 * the row itself. This is what the eye reads as the gap between two
		 * rows: adjacent rows touch, so the space between their contents is the
		 * lower row's top inset plus the upper row's bottom inset.
		 */
		float InsetTop = 0.0f;
		float InsetBottom = 0.0f;
		/**
		 * Left edge of the row's leftmost non-text leaf: the check glyph on an
		 * entry row, the reset arrow on the reset row. On a slider row it is
		 * the slider itself and means nothing, which is why only entry rows are
		 * compared on it.
		 */
		float GlyphLeft = 0.0f;
	};

	/** True for the block types whose labels must share one column. */
	bool IsAlignedRow(const FString& Block)
	{
		return Block.Contains(TEXT("MenuEntryBlock")) || Block.Contains(TEXT("WidgetBlock"));
	}

	/**
	 * Lays the menu out and reads, for every VISIBLE label, its left edge and
	 * the vertical insets of the row it sits in.
	 *
	 * ArrangeChildren rather than GetChildren: a multibox carries collapsed
	 * text blocks the user never sees (an empty input-chord block per entry, a
	 * hook-name block per section), and arranging with a Visible filter drops
	 * exactly those — the same rows the eye drops.
	 *
	 * The row's content extent is the union of its visible LEAF widgets, not
	 * of the label alone: a slider row is as tall as its slider, an entry row
	 * as tall as its check widget or its text, and what must match between them
	 * is the space left around that content, not the content itself.
	 */
	TArray<FMenuRow> MeasureRows(const TSharedRef<SWidget>& Menu)
	{
		TArray<FMenuRow> Rows;

		Menu->SlatePrepass(1.0f);
		const FVector2D Desired(Menu->GetDesiredSize());
		const FGeometry Root = FGeometry::MakeRoot(
			FVector2D(FMath::Max(Desired.X, 320.0), FMath::Max(Desired.Y, 32.0)),
			FSlateLayoutTransform());

		// One row of the menu, while it is being walked: the block's own extent
		// and the union of the leaf geometry found inside it so far.
		struct FRowExtent
		{
			float BlockTop = 0.0f;
			float BlockBottom = 0.0f;
			float ContentTop = TNumericLimits<float>::Max();
			float ContentBottom = TNumericLimits<float>::Lowest();
			float GlyphLeft = TNumericLimits<float>::Max();
		};

		TFunction<void(const TSharedRef<SWidget>&, const FGeometry&, const FString&, FRowExtent*)> Walk =
			[&Rows, &Walk](const TSharedRef<SWidget>& Node, const FGeometry& Geometry, const FString& Block,
				FRowExtent* Extent)
		{
			const FString Type = Node->GetType().ToString();

			// The multibox wraps each row in a widget whose type name ends in
			// "Block" (SMenuEntryBlock, SWidgetBlock, SHeadingBlock). STextBlock
			// ends in "Block" too, but it is the label itself rather than the row
			// it sits in, so it must not claim ownership of its own row.
			const bool bIsRowBlock = Type.EndsWith(TEXT("Block")) && Type != TEXT("STextBlock");
			const FString OwningBlock = bIsRowBlock ? Type : Block;

			FRowExtent OwnExtent;
			if (bIsRowBlock)
			{
				OwnExtent.BlockTop = static_cast<float>(Geometry.AbsolutePosition.Y);
				OwnExtent.BlockBottom = OwnExtent.BlockTop + Geometry.GetLocalSize().Y;
				Extent = &OwnExtent;
			}

			FArrangedChildren Arranged(EVisibility::Visible);
			Node->ArrangeChildren(Geometry, Arranged, /*bUpdateAttributes*/ true);

			// A leaf is what actually gets painted; a panel's own box is not
			// content, it is the space the content was given.
			if (Extent && Arranged.Num() == 0)
			{
				const float Top = static_cast<float>(Geometry.AbsolutePosition.Y);
				Extent->ContentTop = FMath::Min(Extent->ContentTop, Top);
				Extent->ContentBottom = FMath::Max(Extent->ContentBottom, Top + Geometry.GetLocalSize().Y);

				if (Type != TEXT("STextBlock"))
				{
					Extent->GlyphLeft = FMath::Min(Extent->GlyphLeft,
						static_cast<float>(Geometry.AbsolutePosition.X));
				}
			}

			const int32 FirstRowOfThisBlock = Rows.Num();

			if (Type == TEXT("STextBlock"))
			{
				const FString Text = StaticCastSharedRef<STextBlock>(Node)->GetText().ToString();
				if (!Text.IsEmpty())
				{
					Rows.Add(FMenuRow{OwningBlock, Text,
						static_cast<float>(Geometry.AbsolutePosition.X)});
				}
			}

			for (int32 Index = 0; Index < Arranged.Num(); ++Index)
			{
				Walk(Arranged[Index].Widget, Arranged[Index].Geometry, OwningBlock, Extent);
			}

			// Only once the whole row has been walked is its content extent
			// known, so the insets are stamped on the way back out.
			if (bIsRowBlock && OwnExtent.ContentBottom > OwnExtent.ContentTop)
			{
				for (int32 Index = FirstRowOfThisBlock; Index < Rows.Num(); ++Index)
				{
					Rows[Index].InsetTop = OwnExtent.ContentTop - OwnExtent.BlockTop;
					Rows[Index].InsetBottom = OwnExtent.BlockBottom - OwnExtent.ContentBottom;
					Rows[Index].GlyphLeft = OwnExtent.GlyphLeft;
				}
			}
		};

		Walk(Menu, Root, FString(), nullptr);
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
		TOptional<float> MenuGlyph;
		FString MenuGlyphSource;

		for (const FMenuRow& Row : MeasureRows(Menu.Menu))
		{
			AddInfo(FString::Printf(TEXT("%s: [%s] '%s' left=%.1f inset=%.1f/%.1f glyph=%.1f"),
				Menu.Name, *Row.Block, *Row.Text, Row.Left, Row.InsetTop, Row.InsetBottom, Row.GlyphLeft));

			if (!IsAlignedRow(Row.Block))
			{
				continue;   // section headings sit in their own column by design
			}

			// The glyph column is per menu, not per family: it sits at that
			// menu's own Menu.Block.Padding.Left + 2, which varies with how
			// wide a check widget the menu holds. What must hold is that the
			// reset arrow lands on it like every dot and box in the same menu.
			if (Row.Block.Contains(TEXT("MenuEntryBlock")))
			{
				if (!MenuGlyph.IsSet())
				{
					MenuGlyph = Row.GlyphLeft;
					MenuGlyphSource = Row.Text;
				}
				else
				{
					TestTrue(*FString::Printf(
						TEXT("%s: '%s' puts its icon at %.1f, the column '%s' sits in (%.1f)"),
						Menu.Name, *Row.Text, Row.GlyphLeft, *MenuGlyphSource, MenuGlyph.GetValue()),
						FMath::IsNearlyEqual(Row.GlyphLeft, MenuGlyph.GetValue(), 0.5f));
				}
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

			// The insets are reported, not asserted, and deliberately so: they
			// measure where the row's PAINTED content sits, which is not the
			// same as how the row is spaced. Every row of a menu is at least as
			// tall as that menu's shared check column, so a row holding a
			// smaller glyph — a radio dot in the vectorscope menu, whose column
			// is sized by its check box — reports a larger inset while being
			// spaced exactly like its neighbours. Measured 2026-08-22: 3.0 for
			// every row of every menu except those radio rows, which report 4.5
			// for that reason. What the fix actually sets is the row PADDING,
			// and that is pinned structurally by MenuRowPadding below.
		}
	}

	TestTrue(TEXT("Some rows were measured at all"), FamilyColumn.IsSet());
	return true;
}

/**
 * M6.4.1 menu polish: a slider row is padded like an entry row, so rows within a
 * group are spaced evenly whatever kind they are.
 *
 * Structural rather than geometric, because the geometry cannot answer it: rows
 * of a menu are all as tall as its shared check column, so a row's painted
 * content sits at a different inset depending on how big its glyph is, and no
 * measurement of the built tree separates "spaced differently" from "holds a
 * smaller dot" (see the note in the column test above). What the fix sets is the
 * padding, and the padding is what this pins — in the engine's own terms, so the
 * guard still means something if the engine restyles its menus.
 *
 * The defect it guards: an entry row is an SMenuEntryButton and gets that
 * button's NormalPadding on top of Menu.Block.Padding; a widget row has no
 * button and got only the block padding, which left the gap above a slider row
 * two units short of every other gap.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeMenuRowPaddingTest,
	"TrueScope.UI.MenuRowPadding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeMenuRowPaddingTest::RunTest(const FString&)
{
	const FMargin& EngineEntry = FCoreStyle::Get().GetMargin("Menu.Block.Padding");
	const FMargin& EngineWidget = FCoreStyle::Get().GetMargin("Menu.Block.IndentedPadding");
	const FMargin& EntryButton =
		FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Menu.Button").NormalPadding;

	const TrueScope::EMenuCheckColumn Columns[] = {
		TrueScope::EMenuCheckColumn::None,
		TrueScope::EMenuCheckColumn::Radio,
		TrueScope::EMenuCheckColumn::CheckBox,
	};

	for (const TrueScope::EMenuCheckColumn Column : Columns)
	{
		const ISlateStyle& Style = TrueScope::GetMenuStyle(Column);
		const FMargin& Entry = Style.GetMargin("Menu.Block.Padding");
		const FMargin& Widget = Style.GetMargin("Menu.Block.IndentedPadding");
		const int32 Which = static_cast<int32>(Column);

		AddInfo(FString::Printf(
			TEXT("column %d: entry rows top/bottom %.1f/%.1f, slider rows %.1f/%.1f"),
			Which, Entry.Top, Entry.Bottom, Widget.Top, Widget.Bottom));

		// A slider row must reserve above and below exactly what an entry row
		// reserves: its own block padding plus the entry button's padding.
		TestEqual(FString::Printf(TEXT("column %d slider row top padding"), Which),
			Widget.Top, Entry.Top + EntryButton.Top);
		TestEqual(FString::Printf(TEXT("column %d slider row bottom padding"), Which),
			Widget.Bottom, Entry.Bottom + EntryButton.Bottom);

		// The vertical fix must not have moved the text column, which is
		// derived from the engine's own IndentedPadding.Left.
		TestEqual(FString::Printf(TEXT("column %d slider row left padding is the engine's"), Which),
			Widget.Left, EngineWidget.Left);
		TestEqual(FString::Printf(TEXT("column %d slider row right padding is the engine's"), Which),
			Widget.Right, EngineWidget.Right);

		// And the entry rows keep the engine's vertical padding: only the
		// slider rows were short, so only they move.
		TestEqual(FString::Printf(TEXT("column %d entry row top padding is the engine's"), Which),
			Entry.Top, EngineEntry.Top);
		TestEqual(FString::Printf(TEXT("column %d entry row bottom padding is the engine's"), Which),
			Entry.Bottom, EngineEntry.Bottom);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
