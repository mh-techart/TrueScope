// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "TrueScopeModule.h"

#include "Analysis/TrueScopeAnalyzer.h"
#include "Capture/TrueScopeViewExtension.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/CoreDelegates.h"
#include "SceneViewExtension.h"
#include "UI/STrueScopePanel.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "FTrueScopeModule"

static const FName TrueScopeTabName("TrueScope");

void FTrueScopeModule::StartupModule()
{
	// The module loads at PostConfigInit (required later for shader directory
	// registration), long before the engine, Slate and the editor UI exist —
	// defer view-extension and UI registration until engine init completes.
	FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FTrueScopeModule::OnPostEngineInit);
}

void FTrueScopeModule::ShutdownModule()
{
	FCoreDelegates::GetOnPostEngineInit().RemoveAll(this);

	// The extension registry only holds a weak reference; releasing ours
	// unregisters the extension, so no further capture pass can enqueue a
	// readback. Then tear down the analyzer (unhooks its render-thread poll
	// delegate and releases the readback on the render thread, flushing).
	ViewExtension.Reset();
	if (Analyzer.IsValid())
	{
		Analyzer->Shutdown();
		Analyzer.Reset();
	}

	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TrueScopeTabName);
	}
}

void FTrueScopeModule::OnPostEngineInit()
{
	// Commandlets and headless runs have no Slate application and no need for
	// a capture extension.
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}

	Analyzer = MakeShared<FTrueScopeAnalyzer, ESPMode::ThreadSafe>();
	ViewExtension = FSceneViewExtensions::NewExtension<FTrueScopeViewExtension>(Analyzer);

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TrueScopeTabName,
			FOnSpawnTab::CreateRaw(this, &FTrueScopeModule::SpawnTrueScopeTab))
		.SetDisplayName(LOCTEXT("TabTitle", "TrueScope"))
		.SetTooltipText(LOCTEXT("TabTooltip",
			"Open the TrueScope panel: Waveform, RGB Parade, Vectorscope and Histogram scopes for the active viewport."))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory());
}

TSharedRef<SDockTab> FTrueScopeModule::SpawnTrueScopeTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(STrueScopePanel)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FTrueScopeModule, TrueScope)
