// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "TrueScopeModule.h"

#include "Analysis/TrueScopeAnalyzer.h"
#include "Analysis/TrueScopeGpuParade.h"
#include "Capture/TrueScopeViewExtension.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "SceneViewExtension.h"
#include "ShaderCore.h"
#include "UI/STrueScopePanel.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "FTrueScopeModule"

static const FName TrueScopeTabName("TrueScope");

void FTrueScopeModule::StartupModule()
{
	// Map the plugin's virtual shader directory before any shader compilation
	// can reference it — this is why the module loads at PostConfigInit
	// (OpenColorIO pattern).
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("TrueScope"));
	if (ensure(Plugin.IsValid()))
	{
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/TrueScope"),
			FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders")));
	}

	// The engine, Slate and the editor UI do not exist yet at PostConfigInit —
	// defer view-extension and UI registration until engine init completes.
	FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FTrueScopeModule::OnPostEngineInit);
}

void FTrueScopeModule::ShutdownModule()
{
	FCoreDelegates::GetOnPostEngineInit().RemoveAll(this);

	// The extension registry only holds a weak reference; releasing ours
	// unregisters the extension, so no further capture pass can enqueue a
	// readback or GPU analysis. Then tear down the analyzer (unhooks its
	// render-thread poll delegate and releases the readback on the render
	// thread, flushing) — that flush also covers the GPU producer's pending
	// render commands, so it can simply be released afterwards.
	ViewExtension.Reset();
	if (Analyzer.IsValid())
	{
		Analyzer->Shutdown();
		Analyzer.Reset();
	}
	GpuParade.Reset();

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
	GpuParade = MakeShared<FTrueScopeGpuParade, ESPMode::ThreadSafe>(Analyzer);
	ViewExtension = FSceneViewExtensions::NewExtension<FTrueScopeViewExtension>(Analyzer, GpuParade);

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TrueScopeTabName,
			FOnSpawnTab::CreateRaw(this, &FTrueScopeModule::SpawnTrueScopeTab))
		.SetDisplayName(LOCTEXT("TabTitle", "TrueScope"))
		.SetTooltipText(LOCTEXT("TabTooltip",
			"Open the TrueScope panel: Waveform, RGB Parade, Vectorscope and Histogram scopes for the active viewport."))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory());
}

void FTrueScopeModule::SetUseGpuParade(bool bUseGpuParade)
{
	if (ViewExtension.IsValid())
	{
		ViewExtension->SetUseGpuParade(bUseGpuParade);
	}
}

bool FTrueScopeModule::GetUseGpuParade() const
{
	return ViewExtension.IsValid() && ViewExtension->GetUseGpuParade();
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
