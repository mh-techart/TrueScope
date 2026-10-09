// Copyright 2026 Michael Herber
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"

class ISlateStyle;
class FSlateStyleSet;

/**
 * The plugin's own Slate style set (M6.4 Task 1).
 *
 * Holds only brushes TrueScope ships itself — currently the tab icon. Engine
 * icons are never copied into the plugin; they are referenced by style name
 * through FAppStyle (see architecture.md, settled decisions), so this set stays
 * small on purpose.
 *
 * The content root is resolved from IPluginManager rather than hardcoded, so it
 * is correct wherever the plugin is installed (engine plugin folder, project
 * plugin folder, marketplace install).
 */
class FTrueScopeStyle
{
public:
	/** Registers the style set. Safe to call once, from StartupModule. */
	static void Initialize();

	/** Unregisters and releases the style set. Called from ShutdownModule. */
	static void Shutdown();

	/** The style set's name, for FSlateIcon and GetBrush lookups. */
	static FName GetStyleSetName();

	static const ISlateStyle& Get();

private:
	static TSharedRef<FSlateStyleSet> Create();

	static TSharedPtr<FSlateStyleSet> StyleInstance;
};

namespace TrueScope
{
	/** Style name of the plugin's monochrome tab icon (16x16 vector brush). */
	inline const FName TabIconStyleName("TrueScope.TabIcon");
}

namespace TrueScope
{
	/**
	 * Which check widget the engine will size a menu's checkmark column to
	 * (M6.4.1 Task 2).
	 *
	 * A menu's checkmark column is one shared width: every entry row puts its
	 * check widget in an SLinkedBox registered with the multibox's single
	 * FLinkedBoxManager (SMenuEntryBlock.cpp), so the column is as wide as the
	 * widest check widget *in that one menu* and every row in it indents by
	 * that much. A menu of radio entries therefore indents less than a menu
	 * that also holds a toggle, and a menu with no checkable entry at all
	 * indents least — which is exactly why TrueScope's menus did not line up
	 * with each other.
	 */
	enum class EMenuCheckColumn : uint8
	{
		/** No checkable entry: every check widget is collapsed, column width 0. */
		None,
		/** At least one radio entry, no toggle: column is the radio dot's width. */
		Radio,
		/** At least one toggle entry: column is the check box's width. */
		CheckBox
	};

	/**
	 * The Slate style to build a TrueScope menu with, so that every row of every
	 * TrueScope menu starts in the same text column.
	 *
	 * Pass the check-column class the menu will actually contain; the returned
	 * style differs from the engine's only in `Menu.Block.Padding`, which is
	 * widened by exactly what that menu's checkmark column lacks. See
	 * TrueScopeStyle.cpp for the derivation.
	 */
	const ISlateStyle& GetMenuStyle(EMenuCheckColumn CheckColumn);

	/**
	 * Width of that menu's checkmark column, in Slate units — the column the
	 * radio dots and check boxes are drawn in.
	 *
	 * Needed by the one row that carries an icon rather than a check widget:
	 * the icon belongs in this same column, so the row has to know where it
	 * starts. Derived from the engine's own check-box styles, not measured by
	 * eye; see TrueScopeStyle.cpp.
	 */
	float GetMenuCheckColumnWidth(EMenuCheckColumn CheckColumn);
}
