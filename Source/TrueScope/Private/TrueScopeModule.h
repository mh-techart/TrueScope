// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class FTrueScopeAnalyzer;
class FTrueScopeViewExtension;

class FTrueScopeModule : public IModuleInterface
{
public:
	static FTrueScopeModule& Get()
	{
		return FModuleManager::GetModuleChecked<FTrueScopeModule>("TrueScope");
	}

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Capture extension; valid after engine init, null in commandlets. */
	TSharedPtr<FTrueScopeViewExtension, ESPMode::ThreadSafe> GetViewExtension() const { return ViewExtension; }

	/** Async readback + CPU analysis; valid after engine init, null in commandlets. */
	TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe> GetAnalyzer() const { return Analyzer; }

private:
	void OnPostEngineInit();
	TSharedRef<class SDockTab> SpawnTrueScopeTab(const class FSpawnTabArgs& SpawnTabArgs);

	TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe> Analyzer;
	TSharedPtr<FTrueScopeViewExtension, ESPMode::ThreadSafe> ViewExtension;
};
