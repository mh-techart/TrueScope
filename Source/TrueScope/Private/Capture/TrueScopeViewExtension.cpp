// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "Capture/TrueScopeViewExtension.h"

#include "Analysis/TrueScopeAnalyzer.h"
#include "Analysis/TrueScopeGpuAnalysis.h"
#include "Capture/TrueScopeCaptureGate.h"
#include "CommonRenderResources.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/IConsoleManager.h"
#include "LevelEditorViewport.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RHIStaticStates.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneView.h"
#include "ScreenPass.h"
#include "ShowFlags.h"
#include "TextureResource.h"
#include "UnrealClient.h"

bool TrueScope::IsStaleOrthoShowFlagState(ELevelViewportType StoredViewportType,
	ELevelViewportType EffectiveViewportType, bool bPostProcessingShowFlag)
{
	// Stored still ortho + effective perspective can only mean a camera lock
	// taken straight out of an ortho view; with PostProcessing also still off,
	// the show flags provably carry the ortho state the engine forgot to
	// re-apply. A deliberate Show > Post Processing uncheck in a normal
	// perspective viewport does NOT match (its stored type IS Perspective)
	// and stays untouched.
	return StoredViewportType != LVT_Perspective
		&& EffectiveViewportType == LVT_Perspective
		&& !bPostProcessingShowFlag;
}

DEFINE_LOG_CATEGORY_STATIC(LogTrueScope, Log, All);

/**
 * Diagnostic override of the analysis path — the user-facing control is the
 * "Analysis Path (all scopes)" section in every scope's right-click menu,
 * never this CVar alone (architecture: CVars are a developer channel in
 * addition to the UI). Global since phase-5.2: one value selects the path
 * for all four scopes at once. Changes are routed through the same
 * prompt-switch handling as the UI setting (BindAnalysisPathCVarSink), so
 * both channels take effect promptly.
 */
static TAutoConsoleVariable<int32> CVarTrueScopeAnalysisPath(
	TEXT("TrueScope.AnalysisPath"), -1,
	TEXT("Diagnostic override of the TrueScope analysis path (global - all four scopes).\n")
	TEXT(" -1: use the UI setting (default)\n")
	TEXT("  0: force the CPU (compatibility) path\n")
	TEXT("  1: force the GPU (performance) path"),
	ECVF_RenderThreadSafe);

FTrueScopeViewExtension::FTrueScopeViewExtension(const FAutoRegister& AutoRegister,
	const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& InAnalyzer,
	const TSharedPtr<FTrueScopeGpuAnalysis, ESPMode::ThreadSafe>& InGpuAnalysis,
	const TSharedPtr<FTrueScopeCaptureGate, ESPMode::ThreadSafe>& InCaptureGate)
	: FSceneViewExtensionBase(AutoRegister)
	, Analyzer(InAnalyzer)
	, GpuAnalysis(InGpuAnalysis)
	, CaptureGate(InCaptureGate)
{
	CaptureRenderTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
	// Non-sRGB: the after-tonemap scene color already holds display-encoded
	// values; the capture must copy them 1:1 without hardware re-encoding.
	CaptureRenderTarget->RenderTargetFormat = RTF_RGBA8;
	CaptureRenderTarget->ClearColor = FLinearColor::Black;
	CaptureRenderTarget->InitAutoFormat(TrueScope::CaptureWidth, TrueScope::CaptureHeight);
	CaptureRenderTarget->UpdateResourceImmediate(/*bClearRenderTarget*/ true);
}

void FTrueScopeViewExtension::SetUseGpuAnalysis(bool bInUseGpuAnalysis)
{
	bUseGpuAnalysis.store(bInUseGpuAnalysis, std::memory_order_relaxed);
	HandleAnalysisPathChanged();
}

void FTrueScopeViewExtension::SetMeasurementRateHz(float InRateHz)
{
	MeasurementRateHz.store(FMath::Max(InRateHz, 0.1f), std::memory_order_relaxed);
}

void FTrueScopeViewExtension::BindAnalysisPathCVarSink()
{
	CVarTrueScopeAnalysisPath.AsVariable()->SetOnChangedCallback(FConsoleVariableDelegate::CreateSP(
		this, &FTrueScopeViewExtension::HandleAnalysisPathCVarChanged));
}

void FTrueScopeViewExtension::HandleAnalysisPathCVarChanged(IConsoleVariable*)
{
	HandleAnalysisPathChanged();
}

bool FTrueScopeViewExtension::ResolveEffectiveUseGpu() const
{
	const int32 PathOverride = CVarTrueScopeAnalysisPath.GetValueOnGameThread();
	return PathOverride < 0 ? GetUseGpuAnalysis() : PathOverride == 1;
}

void FTrueScopeViewExtension::HandleAnalysisPathChanged()
{
	check(IsInGameThread());

	const bool bNewGpu = ResolveEffectiveUseGpu();
	const bool bOldGpu = bLastEffectiveGpu;
	bLastEffectiveGpu = bNewGpu;

	const TrueScope::EPathSwitchAction Action = TrueScope::DecidePathSwitchAction(
		bOldGpu, bNewGpu, bHasCapturedFrame.load(std::memory_order_acquire));
	if (Action == TrueScope::EPathSwitchAction::None)
	{
		if (bOldGpu != bNewGpu)
		{
			UE_LOG(LogTrueScope, Log, TEXT("Analysis path switched to %s; no frame captured yet — nothing to re-analyse"),
				bNewGpu ? TEXT("GPU") : TEXT("CPU"));
		}
		return;
	}

	// Logged here rather than in ReanalyzeLastCapture: the re-analysis itself
	// serves two callers now (the path switch and the M6.4.1 gain redraw), and
	// only this one is a path switch.
	UE_LOG(LogTrueScope, Log, TEXT("Analysis path switched to %s — re-analysing the last capture (%s)"),
		Action == TrueScope::EPathSwitchAction::ReanalyzeGpu ? TEXT("GPU") : TEXT("CPU"),
		Action == TrueScope::EPathSwitchAction::ReanalyzeGpu
			? TEXT("GPU compute on the resident capture RT, no readback")
			: TEXT("one-time readback seeding the CPU path — the sole documented exception"));

	ReanalyzeLastCapture(Action);
}

void FTrueScopeViewExtension::ReanalyzeLastCapture(TrueScope::EPathSwitchAction Action)
{
	// Approach A (phase-5.1 Task 1): run the already-captured last frame
	// through the newly selected path once, so the switch takes visible effect
	// promptly even while the viewport is not Realtime — no redraw, no new
	// capture, no change to the user's viewport settings. Because this
	// re-analyses the identical frame, it is also the cleanest possible
	// CPU-vs-GPU comparison (same input, no temporal jitter).
	FTextureRenderTargetResource* Resource =
		CaptureRenderTarget ? CaptureRenderTarget->GameThread_GetRenderTargetResource() : nullptr;
	if (!Resource)
	{
		return;
	}

	if (Action == TrueScope::EPathSwitchAction::ReanalyzeGpu && GpuAnalysis)
	{
		// FIFO render commands: the resource cache lands before the passes below.
		GpuAnalysis->CacheResources_GameThread();
	}

	ENQUEUE_RENDER_COMMAND(TrueScopeReanalyzeLastCapture)(
		[WeakThis = AsWeak(), Resource, Action](FRHICommandListImmediate& RHICmdList)
		{
			TSharedPtr<FTrueScopeViewExtension> This =
				StaticCastWeakPtr<FTrueScopeViewExtension>(WeakThis).Pin();
			if (!This)
			{
				return;
			}
			FRHITexture* CaptureTexture = Resource->GetRenderTargetTexture();
			if (!CaptureTexture)
			{
				return;
			}

			// Standalone graph outside the capture pass — the engine's own
			// pattern for one-off RT work (FTextureRenderTarget2DResource::
			// UpdateDeferredResource).
			FRDGBuilder GraphBuilder(RHICmdList);
			FRDGTextureRef Texture =
				RegisterExternalTexture(GraphBuilder, CaptureTexture, TEXT("TrueScope.CaptureRT"));
			if (Action == TrueScope::EPathSwitchAction::ReanalyzeGpu && This->GpuAnalysis)
			{
				This->GpuAnalysis->AddAnalysisPasses_RenderThread(GraphBuilder, Texture);
			}
			else if (Action == TrueScope::EPathSwitchAction::SeedCpuReadback && This->Analyzer)
			{
				This->Analyzer->EnqueueReadback_RenderThread(GraphBuilder, Texture);
			}
			GraphBuilder.Execute();
		});
}

TrueScope::ERedrawOutcome FTrueScopeViewExtension::RedrawFromLastCapture()
{
	check(IsInGameThread());

	// GPU path only, and never the readback branch: a gain change is a steady-
	// state event and the no-readback rule is absolute there. The CPU path
	// re-maps its retained counts instead, so "not the GPU path" is NoData here
	// rather than a fallback.
	if (!GpuAnalysis || !ResolveEffectiveUseGpu())
	{
		return TrueScope::ERedrawOutcome::NoData;
	}

	// Same cold-start guard as the path switch: before the first capture the RT
	// holds only its initial clear, and analysing that would fabricate a
	// measurement.
	if (!bHasCapturedFrame.load(std::memory_order_acquire))
	{
		return TrueScope::ERedrawOutcome::NoData;
	}

	UE_LOG(LogTrueScope, Verbose,
		TEXT("Display setting changed — re-analysing the resident capture (GPU compute, no readback)"));

	ReanalyzeLastCapture(TrueScope::EPathSwitchAction::ReanalyzeGpu);
	return TrueScope::ERedrawOutcome::Started;
}

bool FTrueScopeViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	// Measure only the last focused Level Editor viewport (M2). No viewport →
	// no capture, the render target simply stays at its last/cleared content.
	if (!Context.Viewport || !GCurrentLevelEditingViewportClient
		|| Context.Viewport->GetClient() != GCurrentLevelEditingViewportClient)
	{
		return false;
	}

	const double Now = FPlatformTime::Seconds();

	// Tab-visibility gate (M5): a closed/hidden TrueScope tab stops painting,
	// so capture stops paying its cost entirely; re-showing the tab paints
	// immediately and capture resumes within one throttle interval. Checked
	// before the throttle so gated frames do not consume the interval.
	if (CaptureGate && !CaptureGate->ShouldCapture(Now))
	{
		return false;
	}

	// Throttle: skipping activation entirely makes a skipped frame cost nothing.
	// The interval follows the user's measurement rate (phase-6.1 Task 2).
	if (Now - LastCaptureTimeSeconds < GetCaptureIntervalSeconds())
	{
		return false;
	}

	LastCaptureTimeSeconds = Now;

	// Diagnostic (silent unless LogTrueScope is set to Verbose): activation says
	// "TrueScope wants this frame", which is NOT the same as "a frame will
	// arrive" — the capture hook only fires if the view actually runs the
	// post-tonemap chain. When the scopes hold still with a viewport focused,
	// this line plus the "capture pass ran" line below tell the two apart.
	UE_LOG(LogTrueScope, Verbose,
		TEXT("Capture activated: view type %d, perspective %d, view mode %d, post-processing %d, locked actor %s"),
		static_cast<int32>(GCurrentLevelEditingViewportClient->GetViewportType()),
		GCurrentLevelEditingViewportClient->IsPerspective() ? 1 : 0,
		static_cast<int32>(GCurrentLevelEditingViewportClient->GetViewMode()),
		GCurrentLevelEditingViewportClient->EngineShowFlags.PostProcessing ? 1 : 0,
		GCurrentLevelEditingViewportClient->GetActiveActorLock().IsValid()
			? *GCurrentLevelEditingViewportClient->GetActiveActorLock()->GetActorLabel()
			: TEXT("none"));

	return true;
}

void FTrueScopeViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	// Ortho -> placed-camera repair (work order 13 Fix 1, human-approved
	// 2026-07-17): picking a placed camera from an orthographic view sets only
	// the actor lock; SetViewportType — and with it ApplyViewMode — never runs,
	// so the viewport renders perspective with the ortho show flags still
	// active. PostProcessing stays off, the renderer takes its minimal path,
	// and the capture hook is never subscribed (class comment): the scopes
	// freeze on the last ortho-era frame. Re-run the engine's own view-mode
	// re-apply — exactly the call the ortho -> Perspective menu path makes —
	// probing on a copy first so a view mode that legitimately wants
	// PostProcessing off (e.g. perspective Wireframe) is left alone rather
	// than invalidated every capture. State-based, not edge-based, so it also
	// heals transitions that happened while the tab was closed.
	if (FLevelEditorViewportClient* Client = GCurrentLevelEditingViewportClient)
	{
		if (TrueScope::IsStaleOrthoShowFlagState(
				Client->ViewportType, Client->GetViewportType(), Client->EngineShowFlags.PostProcessing))
		{
			FEngineShowFlags RestoredFlags = Client->EngineShowFlags;
			ApplyViewMode(Client->GetViewMode(), Client->IsPerspective(), RestoredFlags);
			if (RestoredFlags.PostProcessing)
			{
				Client->EngineShowFlags = RestoredFlags;
				Client->Invalidate();
				UE_LOG(LogTrueScope, Log,
					TEXT("Re-applied perspective show flags after a camera lock from an orthographic view (the engine skips ApplyViewMode on that path); capture resumes"));
			}
		}
	}

	// Resolve the RT resource on the game thread and hand the pointer to the
	// render thread (same pattern as the OCIO display extension's resource cache).
	FTextureRenderTargetResource* Resource =
		CaptureRenderTarget ? CaptureRenderTarget->GameThread_GetRenderTargetResource() : nullptr;

	if (GpuAnalysis)
	{
		GpuAnalysis->CacheResources_GameThread();
	}

	ENQUEUE_RENDER_COMMAND(TrueScopeCacheCaptureResource)(
		[WeakThis = AsWeak(), Resource](FRHICommandListImmediate&)
		{
			if (TSharedPtr<FTrueScopeViewExtension> This =
				StaticCastWeakPtr<FTrueScopeViewExtension>(WeakThis).Pin())
			{
				This->CaptureResourceRenderThread = Resource;
			}
		});
}

void FTrueScopeViewExtension::SubscribeToPostProcessingPass(EPostProcessingPass PassId, const FSceneView& View,
	FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
{
	// Only ever reached with the PostProcessing show flag on — the minimal
	// path never subscribes extension callbacks (see class comment). OCIO
	// applies its transform in the same callback chain, ahead of us by
	// priority (see header).
	if (PassId == EPostProcessingPass::Tonemap)
	{
		InOutPassCallbacks.Add(
			FAfterPassCallbackDelegate::CreateRaw(this, &FTrueScopeViewExtension::AfterTonemapPass_RenderThread));
	}
}

FScreenPassTexture FTrueScopeViewExtension::AfterTonemapPass_RenderThread(FRDGBuilder& GraphBuilder,
	const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	const FScreenPassTexture SceneColor =
		FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));

	if (SceneColor.IsValid() && CaptureResourceRenderThread)
	{
		if (FRHITexture* CaptureTexture = CaptureResourceRenderThread->GetRenderTargetTexture())
		{
			FRDGTextureRef OutputTexture =
				RegisterExternalTexture(GraphBuilder, CaptureTexture, TEXT("TrueScope.CaptureRT"));

			// Downscale the view rect into the full capture target (bilinear),
			// stretching to the working resolution: scopes analyze pixel
			// values, so the capture's aspect is irrelevant for measurement.
			//
			// RGB-only writes (CW_RGB): post-tonemap scene-color alpha is
			// undefined (the tonemapper e.g. stores luma there for FXAA) and
			// would make Slate alpha-blend the preview away — the target's
			// alpha stays opaque from its initial clear instead.
			FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
			TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);
			TShaderMapRef<FCopyRectPS> PixelShader(ShaderMap);

			FCopyRectPS::FParameters* Parameters = GraphBuilder.AllocParameters<FCopyRectPS::FParameters>();
			Parameters->InputTexture = SceneColor.Texture;
			Parameters->InputSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			Parameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::ELoad);

			AddDrawScreenPass(
				GraphBuilder,
				RDG_EVENT_NAME("TrueScope.Capture"),
				View,
				FScreenPassTextureViewport(OutputTexture),
				FScreenPassTextureViewport(SceneColor),
				VertexShader,
				PixelShader,
				TStaticBlendState<CW_RGB>::GetRHI(),
				Parameters);

			// Analysis producer, in the same graph (RDG orders it after the
			// draw). GPU path (M5): compute passes, the data never leaves the
			// GPU. CPU path (M3, default this phase): async readback.
			const int32 PathOverride = CVarTrueScopeAnalysisPath.GetValueOnRenderThread();
			const bool bUseGpu = PathOverride < 0 ? GetUseGpuAnalysis() : PathOverride == 1;
			if (bUseGpu && GpuAnalysis)
			{
				GpuAnalysis->AddAnalysisPasses_RenderThread(GraphBuilder, OutputTexture);
			}
			else if (Analyzer)
			{
				Analyzer->EnqueueReadback_RenderThread(GraphBuilder, OutputTexture);
			}

			// The switch re-analysis (cold-start guard) may now trust the RT.
			bHasCapturedFrame.store(true, std::memory_order_release);

			// The counterpart of the activation line: this one only appears when
			// a frame really reached the scopes.
			UE_LOG(LogTrueScope, Verbose, TEXT("Capture pass ran (%dx%d)"),
				SceneColor.ViewRect.Width(), SceneColor.ViewRect.Height());

			if (!bFirstCaptureLogged)
			{
				bFirstCaptureLogged = true;
				UE_LOG(LogTrueScope, Log,
					TEXT("First viewport capture (%dx%d -> %dx%d), input format %s%s"),
					SceneColor.ViewRect.Width(), SceneColor.ViewRect.Height(),
					OutputTexture->Desc.Extent.X, OutputTexture->Desc.Extent.Y,
					GPixelFormats[SceneColor.Texture->Desc.Format].Name,
					EnumHasAnyFlags(SceneColor.Texture->Desc.Flags, TexCreate_SRGB) ? TEXT(" (sRGB)") : TEXT(""));
			}
		}
	}

	// Read-only pass: hand the scene color through (fills OverrideOutput when
	// this callback happens to be the last pass in the chain).
	return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
}

void FTrueScopeViewExtension::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(CaptureRenderTarget);
}
