// Copyright 2026 TrueScope contributors
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
