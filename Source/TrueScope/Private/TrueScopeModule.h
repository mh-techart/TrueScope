// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class FTrueScopeAnalyzer;
class FTrueScopeCaptureGate;
class FTrueScopeGpuAnalysis;
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

	/** Tab-visibility capture gate; the panel reports its paints here (M5). */
	TSharedPtr<FTrueScopeCaptureGate, ESPMode::ThreadSafe> GetCaptureGate() const { return CaptureGate; }

	/**
	 * UI-selected analysis path: true = GPU (performance), false = CPU
	 * (compatibility). Global since phase-5.2 — one switch selects the path
	 * for all four scopes at once. Default CPU through M5.x; the flip to
	 * GPU-default is an M7 decision. Not persisted yet (settings persistence
	 * is M6). Game thread.
	 */
	void SetUseGpuAnalysis(bool bUseGpuAnalysis);
	bool GetUseGpuAnalysis() const;

private:
	void OnPostEngineInit();
	TSharedRef<class SDockTab> SpawnTrueScopeTab(const class FSpawnTabArgs& SpawnTabArgs);

	TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe> Analyzer;
	TSharedPtr<FTrueScopeCaptureGate, ESPMode::ThreadSafe> CaptureGate;
	TSharedPtr<FTrueScopeGpuAnalysis, ESPMode::ThreadSafe> GpuAnalysis;
	TSharedPtr<FTrueScopeViewExtension, ESPMode::ThreadSafe> ViewExtension;
};
