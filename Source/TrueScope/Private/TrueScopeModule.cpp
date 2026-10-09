// Copyright 2026 Michael Herber
// SPDX-License-Identifier: Apache-2.0

#include "TrueScopeModule.h"

#include "Analysis/TrueScopeAnalyzer.h"
#include "Analysis/TrueScopeDisplayGains.h"
#include "Analysis/TrueScopeGpuAnalysis.h"
#include "Analysis/TrueScopeRasterSizes.h"
#include "Capture/TrueScopeCaptureGate.h"
#include "Capture/TrueScopeViewExtension.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "SceneViewExtension.h"
#include "ShaderCore.h"
#include "Textures/SlateIcon.h"
#include "TrueScopeSettings.h"
#include "UI/STrueScopeTab.h"
#include "UI/TrueScopeStyle.h"
#include "UI/TrueScopeType.h"
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

	// The plugin's own brushes (tab icon). The style set only reads files and
	// registers brushes, so it is safe this early; the UI that uses it is
	// created later.
	FTrueScopeStyle::Initialize();

	// The engine, Slate and the editor UI do not exist yet at PostConfigInit —
	// defer view-extension and UI registration until engine init completes.
	FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FTrueScopeModule::OnPostEngineInit);
}

void FTrueScopeModule::ShutdownModule()
{
	FCoreDelegates::GetOnPostEngineInit().RemoveAll(this);

	// Before the producers below are released: the tick calls into both.
	if (ScopeRedrawTickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ScopeRedrawTickHandle);
		ScopeRedrawTickHandle.Reset();
	}

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
	GpuAnalysis.Reset();
	RasterSizes.Reset();
	DisplayGains.Reset();
	CaptureGate.Reset();

	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TrueScopeTabName);
	}

	FTrueScopeStyle::Shutdown();
}

void FTrueScopeModule::SeedDisplayGainsFromSettings()
{
	if (!DisplayGains)
	{
		return;
	}

	// Walk the panels, not the scope types (M6.4): the gain belongs to the panel,
	// and the panel's type says which analysis slot it feeds.
	UTrueScopeSettings& Settings = UTrueScopeSettings::Get();
	for (int32 PanelIndex = 0; PanelIndex < TrueScope::NumPanels; ++PanelIndex)
	{
		const ETrueScopeType Type = Settings.GetPanelScopeType(PanelIndex);
		const FName GainKey = Type == ETrueScopeType::Histogram
			? TrueScope::HistogramGainKey : TrueScope::TraceBrightnessGainKey;

		// Clamped like the widget does, so a hand-edited ini cannot seed a gain
		// the slider is unable to express or undo.
		const float Multiplier = FMath::Clamp(Settings.GetPanelGainMultiplier(PanelIndex, GainKey),
			TrueScope::MinGainMultiplier(), TrueScope::MaxGainMultiplier());

		switch (Type)
		{
		case ETrueScopeType::Waveform:
			DisplayGains->SetDensityGain(TrueScope::EDensityScope::Waveform, Multiplier);
			break;
		case ETrueScopeType::Parade:
			DisplayGains->SetDensityGain(TrueScope::EDensityScope::Parade, Multiplier);
			break;
		case ETrueScopeType::Vectorscope:
			DisplayGains->SetDensityGain(TrueScope::EDensityScope::Vectorscope, Multiplier);
			break;
		case ETrueScopeType::Histogram:
			DisplayGains->SetHistogramGain(Multiplier);
			break;
		}
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
	CaptureGate = MakeShared<FTrueScopeCaptureGate, ESPMode::ThreadSafe>();
	RasterSizes = MakeShared<FTrueScopeRasterSizes, ESPMode::ThreadSafe>();
	DisplayGains = MakeShared<FTrueScopeDisplayGains, ESPMode::ThreadSafe>();
	Analyzer->SetDisplayGains(DisplayGains);
	GpuAnalysis = MakeShared<FTrueScopeGpuAnalysis, ESPMode::ThreadSafe>(Analyzer, RasterSizes, DisplayGains);
	ViewExtension = FSceneViewExtensions::NewExtension<FTrueScopeViewExtension>(Analyzer, GpuAnalysis, CaptureGate);
	ViewExtension->BindAnalysisPathCVarSink();

	// Reopen with the user's remembered options (phase-6.1 Task 3). The scopes'
	// display modes restore themselves in InitializeScopeWidget; these two are
	// plugin-wide, so they are applied here, before the first capture.
	const UTrueScopeSettings& Settings = UTrueScopeSettings::Get();
	ViewExtension->SetUseGpuAnalysis(Settings.bUseGpuAnalysis);
	ViewExtension->SetMeasurementRateHz(Settings.MeasurementRateHz);

	// The remembered gains are seeded here rather than left to the widgets
	// (M6.3): a capture can be analysed before the tab has ever been opened, and
	// analysing it at the default gain would make the first frame the user sees
	// after opening the tab differ from every one after it.
	SeedDisplayGainsFromSettings();

	// Services the sliders' redraw requests (M6.4.1). One tick per frame is
	// what makes the coalescing a per-FRAME budget rather than a per-event one.
	ScopeRedrawTickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FTrueScopeModule::TickScopeRedraw));

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TrueScopeTabName,
			FOnSpawnTab::CreateRaw(this, &FTrueScopeModule::SpawnTrueScopeTab))
		.SetDisplayName(LOCTEXT("TabTitle", "TrueScope"))
		.SetTooltipText(LOCTEXT("TabTooltip",
			"Open the TrueScope panel: Waveform, RGB Parade, Vectorscope and Histogram scopes for the active viewport."))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory())
		.SetIcon(FSlateIcon(FTrueScopeStyle::GetStyleSetName(), TrueScope::TabIconStyleName));
}

void FTrueScopeModule::RequestScopeRedraw()
{
	ScopeRedrawRequest.Request();
}

bool FTrueScopeModule::TickScopeRedraw(float /*DeltaSeconds*/)
{
	if (!ScopeRedrawRequest.Consume())
	{
		return true; // nothing asked for; two atomic loads is the whole cost
	}

	// Which producer holds the last frame decides how it is re-rendered: the GPU
	// path re-derives its bins from the resident capture RT, the CPU path
	// re-maps the counts it retained. Neither captures and neither reads back.
	const TrueScope::ERedrawOutcome Outcome = GetEffectiveUseGpuAnalysis()
		? (ViewExtension.IsValid() ? ViewExtension->RedrawFromLastCapture() : TrueScope::ERedrawOutcome::NoData)
		: (Analyzer.IsValid() ? Analyzer->RemapLastResultWithCurrentGains() : TrueScope::ERedrawOutcome::NoData);

	switch (Outcome)
	{
	case TrueScope::ERedrawOutcome::Started:
		++ScopeRedrawCount;
		break;

	case TrueScope::ERedrawOutcome::Busy:
		// Put it back rather than drop it: the value that would be lost is the
		// one the user stopped on, which is the only one that really matters.
		ScopeRedrawRequest.Request();
		break;

	case TrueScope::ERedrawOutcome::NoData:
		// Nothing measured yet. Whatever arrives next already carries the
		// current gains, so there is nothing to retry.
		break;
	}

	return true;
}

void FTrueScopeModule::SetUseGpuAnalysis(bool bUseGpuAnalysis)
{
	// A diagnostic override left on the console would silently outvote the click
	// the user just made in the tab, so the UI action clears it (phase-6.1). The
	// CVar sink then re-resolves the effective path, as on any CVar change.
	if (IConsoleVariable* PathOverride =
		IConsoleManager::Get().FindConsoleVariable(TEXT("TrueScope.AnalysisPath")))
	{
		if (PathOverride->GetInt() >= 0)
		{
			PathOverride->Set(-1, ECVF_SetByCode);
		}
	}

	if (ViewExtension.IsValid())
	{
		ViewExtension->SetUseGpuAnalysis(bUseGpuAnalysis);
	}

	// Persist the user's choice, not a diagnostic CVar's (the CVar sink never
	// reaches this function).
	UTrueScopeSettings& Settings = UTrueScopeSettings::Get();
	if (Settings.bUseGpuAnalysis != bUseGpuAnalysis)
	{
		Settings.bUseGpuAnalysis = bUseGpuAnalysis;
		Settings.SaveConfig();
	}
}

bool FTrueScopeModule::GetUseGpuAnalysis() const
{
	return ViewExtension.IsValid() && ViewExtension->GetUseGpuAnalysis();
}

bool FTrueScopeModule::GetEffectiveUseGpuAnalysis() const
{
	return ViewExtension.IsValid() && ViewExtension->ResolveEffectiveUseGpu();
}

void FTrueScopeModule::SetMeasurementRateHz(float RateHz)
{
	if (ViewExtension.IsValid())
	{
		ViewExtension->SetMeasurementRateHz(RateHz);
	}

	UTrueScopeSettings& Settings = UTrueScopeSettings::Get();
	if (!FMath::IsNearlyEqual(Settings.MeasurementRateHz, RateHz))
	{
		Settings.MeasurementRateHz = RateHz;
		Settings.SaveConfig();
	}
}

float FTrueScopeModule::GetMeasurementRateHz() const
{
	return ViewExtension.IsValid() ? ViewExtension->GetMeasurementRateHz()
		: TrueScope::DefaultMeasurementRateHz;
}

double FTrueScopeModule::GetCaptureIntervalSeconds() const
{
	return ViewExtension.IsValid() ? ViewExtension->GetCaptureIntervalSeconds()
		: TrueScope::DefaultCaptureIntervalSeconds;
}

TSharedRef<SDockTab> FTrueScopeModule::SpawnTrueScopeTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(STrueScopeTab)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FTrueScopeModule, TrueScope)
