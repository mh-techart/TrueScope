// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class FTrueScopeAnalyzer;
class FTrueScopeCaptureGate;
class FTrueScopeGpuAnalysis;
class FTrueScopeDisplayGains;
class FTrueScopeRasterSizes;
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
	 * The scope widgets' plot rects in device pixels (phase-6.1): every scope
	 * publishes the rect it draws its raster into on each paint, and the GPU
	 * producer sizes its render targets to them, so the rasters are drawn at
	 * display resolution. Null in commandlets, like the other services.
	 */
	TSharedPtr<FTrueScopeRasterSizes, ESPMode::ThreadSafe> GetRasterSizes() const { return RasterSizes; }

	/**
	 * The user's per-scope gain multipliers (M6.3): the density scopes' Trace
	 * Brightness and the histogram's Histogram Gain. Published by the scope
	 * widgets, read by both analysis paths. Null in commandlets, like the
	 * other services.
	 */
	TSharedPtr<FTrueScopeDisplayGains, ESPMode::ThreadSafe> GetDisplayGains() const { return DisplayGains; }

	/**
	 * UI-selected analysis path: true = GPU (performance), false = CPU
	 * (compatibility). Global since phase-5.2 — one switch selects the path
	 * for all four scopes at once. Default CPU through M5.x; the flip to
	 * GPU-default is an M7 decision. Game thread.
	 *
	 * Setting it from the UI also clears an active TrueScope.AnalysisPath
	 * override (phase-6.1): the CVar is a diagnostic channel, so a user's click
	 * in the tab must never be silently outvoted by a console value they cannot
	 * see from there.
	 */
	void SetUseGpuAnalysis(bool bUseGpuAnalysis);
	bool GetUseGpuAnalysis() const;

	/**
	 * The path actually running, i.e. after the diagnostic CVar override — what
	 * the settings popup and the status line must show, since that is what produced the
	 * numbers on screen.
	 */
	bool GetEffectiveUseGpuAnalysis() const;

	/**
	 * Measurement rate in captures per second (phase-6.1 Task 2). One value for
	 * the whole plugin: it throttles the capture, and the scopes' staleness
	 * clock is expressed in these intervals.
	 */
	void SetMeasurementRateHz(float RateHz);
	float GetMeasurementRateHz() const;

	/** The capture interval derived from the rate; the default before engine init. */
	double GetCaptureIntervalSeconds() const;

private:
	void OnPostEngineInit();

	/** Applies the remembered gain multipliers before the first capture (M6.3). */
	void SeedDisplayGainsFromSettings();
	TSharedRef<class SDockTab> SpawnTrueScopeTab(const class FSpawnTabArgs& SpawnTabArgs);

	TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe> Analyzer;
	TSharedPtr<FTrueScopeCaptureGate, ESPMode::ThreadSafe> CaptureGate;
	TSharedPtr<FTrueScopeRasterSizes, ESPMode::ThreadSafe> RasterSizes;
	TSharedPtr<FTrueScopeDisplayGains, ESPMode::ThreadSafe> DisplayGains;
	TSharedPtr<FTrueScopeGpuAnalysis, ESPMode::ThreadSafe> GpuAnalysis;
	TSharedPtr<FTrueScopeViewExtension, ESPMode::ThreadSafe> ViewExtension;
};
