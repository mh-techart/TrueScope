// Copyright 2026 Michael Herber
// SPDX-License-Identifier: Apache-2.0

#include "TrueScopeStyle.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateTypes.h"

// Resolves IMAGE_BRUSH_SVG paths against the style set's content root.
#define RootToContentDir Style->RootToContentDir
#include "Styling/SlateStyleMacros.h"

TSharedPtr<FSlateStyleSet> FTrueScopeStyle::StyleInstance;

namespace TrueScopeMenuStyle
{
	// ---------------------------------------------------------------------
	// One text column for every row of every TrueScope menu (M6.4.1 Task 2).
	//
	// Where a menu row's text starts, as UE 5.8 lays it out with FCoreStyle's
	// "Menu" style — every number below is read off the engine source, none is
	// chosen by eye:
	//
	//   entry row   (SMenuEntryBlock.cpp:920, 882-903, 866-874)
	//     Menu.Block.Padding.Left | 2 | check column | 8 | 2 | 2 | label
	//
	//   widget row  (SWidgetBlock.cpp:116, 128, 216) — the gain slider
	//     Menu.Block.IndentedPadding.Left | 14 | 2 | label
	//
	// The check column is shared across one menu but not across menus: each
	// entry's check widget sits in an SLinkedBox registered with that
	// multibox's single FLinkedBoxManager, so the column is as wide as the
	// widest check widget in that menu and 0 when the menu has none (a
	// collapsed widget has no desired size). Measured on 2026-08-21 with the
	// TrueScope.UI.MenuTextColumn test: radio-only menus indented their entries
	// to 36, the vectorscope menu (which has a toggle) to 46, the parade menu
	// (neither) to 26 — while every slider row sat at 46 regardless.
	//
	// The widget row's own "account for checkmark" allowance is a fixed 14 and
	// cannot follow the menu it is in, so the engine cannot solve this for us.
	// What it does tell us is which column it considers correct: 30 + 14 + 2 =
	// 46, which is also exactly where an entry row lands when the menu contains
	// a check box (12 + 2 + 20 + 8 + 2 + 2). Two independent engine constants
	// agree on 46, so that is the column TrueScope aligns everything to, and
	// the vectorscope menu — the one the human saw as already correct — does
	// not move at all.
	//
	// Reaching it needs one override: widen Menu.Block.Padding by exactly what
	// this menu's checkmark column lacks. Nothing else uses that margin (the
	// only two readers in the whole MultiBox module are the menu-entry buttons
	// at SMenuEntryBlock.cpp:664 and :920), so headings (Menu.Heading.Padding)
	// and separators (Menu.Separator.Padding) keep the engine's look, and the
	// row highlight still spans the full width because ContentPadding insets
	// only the content.
	// ---------------------------------------------------------------------

	// ---------------------------------------------------------------------
	// One vertical rhythm for every row of every TrueScope menu (M6.4.1 menu
	// polish). Same approach: the number is read off the engine, not chosen by
	// eye.
	//
	// Horizontally the two row kinds differ in their indent; vertically they
	// differ in a button. An entry row is an SMenuEntryButton
	// (SMenuEntryBlock.cpp:659), and FCoreStyle's "Menu.Button" carries
	// NormalPadding = FMargin(0, 2) (StarshipCoreStyle.cpp:2622-2629), so an
	// entry row's content is inset 2 above and 2 below on top of
	// Menu.Block.Padding's own 1. A widget row has no button and gets only the
	// block padding's 1, so the gaps come out:
	//
	//   entry  -> entry   1 + 2 + 2 + 1 = 6
	//   entry  -> slider  1 + 2 + 0 + 1 = 4
	//
	// which is the 2-unit deficit the human saw as "the slider rows sit closer
	// to the row above them than every other row pair". Widening
	// Menu.Block.IndentedPadding vertically by exactly the menu button's own
	// NormalPadding closes it, and closes it by the amount the engine itself
	// chose for entry rows rather than by a tuned constant. Left and Right stay
	// untouched: the 46-unit text column above is derived from
	// IndentedPadding.Left, so moving it would undo the alignment.
	//
	// Section headings (Menu.Heading.Padding) and the separator above the reset
	// row (Menu.Separator.Padding) are separate margins and keep their own,
	// larger spacing.
	// ---------------------------------------------------------------------

	/** Fixed slot paddings an entry row puts around its checkmark column. */
	constexpr float EntryFixedIndent = 2.0f + 8.0f + 2.0f + 2.0f;

	/** SWidgetBlock's fixed FMargin(14,0,8,0) "account for checkmark" allowance. */
	constexpr float WidgetCheckAllowance = 14.0f;

	/** Left padding of the label slot in a widget row. */
	constexpr float WidgetLabelIndent = 2.0f;

	/** Width of the check widget the engine sizes the column to, from its style. */
	float CheckColumnWidth(TrueScope::EMenuCheckColumn Column)
	{
		FName StyleName;
		switch (Column)
		{
		case TrueScope::EMenuCheckColumn::Radio:
			StyleName = TEXT("Menu.RadioButton");
			break;
		case TrueScope::EMenuCheckColumn::CheckBox:
			StyleName = TEXT("Menu.CheckBox");
			break;
		default:
			// Every check widget is collapsed, so the column has no width.
			return 0.0f;
		}

		// An SCheckBox with empty content is as wide as its widest image plus
		// its style padding (SCheckBox.cpp:536-566): the background and the
		// state images sit in one SOverlay, and the (empty) content slot still
		// contributes the padding.
		const FCheckBoxStyle& Style = FCoreStyle::Get().GetWidgetStyle<FCheckBoxStyle>(StyleName);
		const float ImageWidth = FMath::Max(
			FMath::Max(Style.BackgroundImage.GetImageSize().X, Style.CheckedImage.GetImageSize().X),
			Style.UncheckedImage.GetImageSize().X);
		return ImageWidth + Style.Padding.GetTotalSpaceAlong<Orient_Horizontal>();
	}

	/** The column every TrueScope menu row starts in: the engine's widget-row column. */
	static float TextColumn()
	{
		return FCoreStyle::Get().GetMargin("Menu.Block.IndentedPadding").Left
			+ WidgetCheckAllowance + WidgetLabelIndent;
	}

	static TMap<uint8, TSharedPtr<FSlateStyleSet>> Styles;

	static const ISlateStyle& Get(TrueScope::EMenuCheckColumn Column)
	{
		TSharedPtr<FSlateStyleSet>& Style = Styles.FindOrAdd(static_cast<uint8>(Column));
		if (!Style.IsValid())
		{
			Style = MakeShared<FSlateStyleSet>(
				FName(*FString::Printf(TEXT("TrueScopeMenuStyle.%d"), static_cast<int32>(Column))));

			// Everything TrueScope does not override comes from the style the
			// engine's own menus use, so the menus keep the editor's look and
			// follow it through future engine changes. Deliberately not
			// registered with FSlateStyleRegistry: nothing looks these up by
			// name, they are handed to FMenuBuilder as a pointer.
			Style->SetParentStyleName(FCoreStyle::Get().GetStyleSetName());

			const FMargin& BlockPadding = FCoreStyle::Get().GetMargin("Menu.Block.Padding");
			const float Left = TextColumn() - EntryFixedIndent - CheckColumnWidth(Column);
			Style->Set("Menu.Block.Padding",
				FMargin(Left, BlockPadding.Top, BlockPadding.Right, BlockPadding.Bottom));

			// The slider row's own vertical padding, raised by the entry rows'
			// button padding so both kinds of row keep one rhythm. Left and
			// Right are the engine's, untouched — the text column depends on
			// Left.
			const FMargin& IndentedPadding = FCoreStyle::Get().GetMargin("Menu.Block.IndentedPadding");
			const FMargin& EntryButtonPadding =
				FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Menu.Button").NormalPadding;
			Style->Set("Menu.Block.IndentedPadding",
				FMargin(IndentedPadding.Left,
					IndentedPadding.Top + EntryButtonPadding.Top,
					IndentedPadding.Right,
					IndentedPadding.Bottom + EntryButtonPadding.Bottom));
		}
		return *Style;
	}

	static void ReleaseAll()
	{
		Styles.Empty();
	}
}

namespace TrueScope
{
	const ISlateStyle& GetMenuStyle(EMenuCheckColumn CheckColumn)
	{
		return TrueScopeMenuStyle::Get(CheckColumn);
	}

	float GetMenuCheckColumnWidth(EMenuCheckColumn CheckColumn)
	{
		return TrueScopeMenuStyle::CheckColumnWidth(CheckColumn);
	}
}

void FTrueScopeStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FTrueScopeStyle::Shutdown()
{
	TrueScopeMenuStyle::ReleaseAll();

	if (StyleInstance.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
		ensure(StyleInstance.IsUnique());
		StyleInstance.Reset();
	}
}

FName FTrueScopeStyle::GetStyleSetName()
{
	static const FName StyleSetName(TEXT("TrueScopeStyle"));
	return StyleSetName;
}

const ISlateStyle& FTrueScopeStyle::Get()
{
	return *StyleInstance;
}

TSharedRef<FSlateStyleSet> FTrueScopeStyle::Create()
{
	TSharedRef<FSlateStyleSet> Style = MakeShared<FSlateStyleSet>(GetStyleSetName());

	// Resolved, not hardcoded: the plugin may live under the engine, the
	// project, or a marketplace install.
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("TrueScope"));
	if (ensure(Plugin.IsValid()))
	{
		Style->SetContentRoot(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources")));
	}

	const FVector2f Icon16x16(16.0f, 16.0f);
	Style->Set(TrueScope::TabIconStyleName, new IMAGE_BRUSH_SVG("Icons/TrueScope_Icon_mono", Icon16x16));

	return Style;
}

#undef RootToContentDir
