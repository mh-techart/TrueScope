// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "CoreMinimal.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "UI/STrueScopePanel.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "FTrueScopeModule"

static const FName TrueScopeTabName("TrueScope");

class FTrueScopeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// The module loads at PostConfigInit (required later for shader
		// directory registration), long before Slate and the editor UI exist —
		// defer all UI registration until the engine is fully initialized.
		FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FTrueScopeModule::RegisterTabSpawner);
	}

	virtual void ShutdownModule() override
	{
		FCoreDelegates::GetOnPostEngineInit().RemoveAll(this);

		if (FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TrueScopeTabName);
		}
	}

private:
	void RegisterTabSpawner()
	{
		// Commandlets and headless runs have no Slate application.
		if (!FSlateApplication::IsInitialized())
		{
			return;
		}

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TrueScopeTabName,
				FOnSpawnTab::CreateRaw(this, &FTrueScopeModule::SpawnTrueScopeTab))
			.SetDisplayName(LOCTEXT("TabTitle", "TrueScope"))
			.SetTooltipText(LOCTEXT("TabTooltip",
				"Open the TrueScope panel: Waveform, RGB Parade, Vectorscope and Histogram scopes for the active viewport."))
			.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory());
	}

	TSharedRef<SDockTab> SpawnTrueScopeTab(const FSpawnTabArgs& SpawnTabArgs)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(STrueScopePanel)
			];
	}
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FTrueScopeModule, TrueScope)
