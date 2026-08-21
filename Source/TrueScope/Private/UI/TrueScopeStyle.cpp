// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "TrueScopeStyle.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateStyle.h"

// Resolves IMAGE_BRUSH_SVG paths against the style set's content root.
#define RootToContentDir Style->RootToContentDir
#include "Styling/SlateStyleMacros.h"

TSharedPtr<FSlateStyleSet> FTrueScopeStyle::StyleInstance;

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
