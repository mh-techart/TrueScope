// Copyright 2026 Michael Herber
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Capture/TrueScopePathSwitch.h"
#include "TrueScopeRedrawRequest.h"
#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "UObject/GCObject.h"
#include "UObject/ObjectPtr.h"

#include <atomic>

enum ELevelViewportType : int;

class IConsoleVariable;

class FRDGBuilder;
class FTextureRenderTargetResource;
class FTrueScopeAnalyzer;
class FTrueScopeCaptureGate;
class FTrueScopeGpuAnalysis;
class UTextureRenderTarget2D;
struct FPostProcessMaterialInputs;
struct FScreenPassTexture;

namespace TrueScope
{
	/** Fixed working resolution of the capture render target (M2; settings UI comes later). */
	constexpr int32 CaptureWidth = 960;
	constexpr int32 CaptureHeight = 540;

	/** Minimum time between captures out of the box — ~2.5 Hz refresh (M2). */
	constexpr double DefaultCaptureIntervalSeconds = 0.4;

	/**
	 * Measurement rates the settings popup offers (phase-6.1 Task 2). Deliberately
	 * a short list of useful cadences, not a free slider: the artist picks
	 * "slower / normal / faster", and the adaptive rate that would make the choice
	 * automatic is backlog. 2.5 Hz is the shipped default.
	 */
	inline constexpr float MeasurementRatesHz[] = {1.0f, 2.5f, 5.0f};
	inline constexpr float DefaultMeasurementRateHz = 2.5f;

	/**
	 * Must stay below the OCIO display extension's priority (100, see
	 * OPENCOLORIO_SCENE_VIEW_EXTENSION_PRIORITY): extensions are sorted by
	 * priority descending and their after-Tonemap callbacks execute in that
	 * order, each receiving the previous one's output — lower priority means
	 * TrueScope samples the post-OCIO image.
	 */
	constexpr int32 CaptureExtensionPriority = 0;

	/**
	 * True when the focused viewport's show flags are stale from a missed
	 * engine path (work order 13 Fix 1): switching an orthographic viewport
	 * to a placed camera only sets the actor lock — SetViewportType, which
	 * would re-apply the view-mode show flags, never runs — so the viewport
	 * renders perspective while PostProcessing is still off from the ortho
	 * state, and the capture hook is never subscribed (see class comment).
	 * The signature is precise: the stored type still says ortho while the
	 * effective type reports perspective (the camera lock overrides it).
	 *
	 * Pure so the automation test can pin the condition; the caller decides
	 * what "restore" means (re-run ApplyViewMode, the exact call the engine
	 * itself makes on the ortho -> Perspective menu path).
	 */
	bool IsStaleOrthoShowFlagState(ELevelViewportType StoredViewportType,
		ELevelViewportType EffectiveViewportType, bool bPostProcessingShowFlag);
}

/**
 * Captures the post-tonemap (and post-OCIO) display-encoded image of the last
 * focused Level Editor viewport into a plugin-owned render target, downscaled
 * to a fixed working resolution and throttled to a few updates per second.
 *
 * Read-only pass: subscribes to the after-Tonemap callback chain, copies the
 * incoming scene color into the capture render target and returns the scene
 * color untouched.
 *
 * The after-pass callbacks exist only while the PostProcessing show flag is
 * on (verified in 5.8, work order 13 Fix 1): with the flag off the renderer
 * takes its minimal path, whose branch never collects scene-view-extension
 * pass callbacks at all — the subscribe loops in PostProcessing.cpp
 * (AddPostProcessingPasses) sit inside the bPostProcessingEnabled branch
 * only. That is why orthographic views (and Wireframe/Unlit view modes,
 * which ApplyViewMode maps to PostProcessing=0) can never be captured
 * through this hook. An earlier version of this comment claimed the Tonemap
 * callbacks also run on the minimal path; they do not.
 * BeginRenderViewFamily repairs the one case where the flag is stale rather
 * than intended.
 */
class FTrueScopeViewExtension : public FSceneViewExtensionBase, public FGCObject
{
public:
	FTrueScopeViewExtension(const FAutoRegister& AutoRegister,
		const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& InAnalyzer,
		const TSharedPtr<FTrueScopeGpuAnalysis, ESPMode::ThreadSafe>& InGpuAnalysis,
		const TSharedPtr<FTrueScopeCaptureGate, ESPMode::ThreadSafe>& InCaptureGate);

	//~ Begin ISceneViewExtension interface
	virtual int32 GetPriority() const override { return TrueScope::CaptureExtensionPriority; }
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void SubscribeToPostProcessingPass(EPostProcessingPass PassId, const FSceneView& View,
		FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override;
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;
	//~ End ISceneViewExtension interface

	//~ Begin FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FTrueScopeViewExtension"); }
	//~ End FGCObject interface

	/** The render target receiving the captured image. Game thread. */
	UTextureRenderTarget2D* GetCaptureRenderTarget() const { return CaptureRenderTarget; }

	/**
	 * UI-selected analysis path: true = GPU compute, false = CPU readback
	 * (still the default; the flip is an M7 decision). Global — one value
	 * selects the path for all four scopes at once (phase-5.2). Set from the
	 * game thread, read on the render thread per capture. The
	 * TrueScope.AnalysisPath console variable overrides this for diagnostics.
	 *
	 * A switch takes effect promptly regardless of the viewport's Realtime
	 * setting (phase-5.1 Task 1): the last captured frame is re-analysed
	 * through the newly selected path — see HandleAnalysisPathChanged.
	 */
	void SetUseGpuAnalysis(bool bInUseGpuAnalysis);
	bool GetUseGpuAnalysis() const { return bUseGpuAnalysis.load(std::memory_order_relaxed); }

	/**
	 * Routes changes of the TrueScope.AnalysisPath diagnostic CVar through the
	 * same prompt-switch handling as the UI setting. Call once after creation
	 * (game thread); the SP binding unbinds itself with this object.
	 */
	void BindAnalysisPathCVarSink();

	/**
	 * Effective path after the diagnostic CVar override — what is actually
	 * running, which is what the UI must show (phase-6.1). Game thread.
	 */
	bool ResolveEffectiveUseGpu() const;

	/**
	 * Re-runs the GPU analysis on the RESIDENT capture render target so a
	 * changed display setting becomes visible without waiting for a new capture
	 * (M6.4.1, Trace Brightness / Histogram Gain taking effect during the drag).
	 *
	 * The same route the path switch has used since phase-5.1, for the same
	 * reason: while any Slate menu is open the level viewport does not tick, so
	 * no further capture can arrive — exactly the condition under which the
	 * sliders appeared dead. No new capture, no readback, and no retained bin
	 * buffers: the bins are re-derived from the frame that is still in the RT.
	 *
	 * GPU path only. On the CPU path the counts are retained in the published
	 * result and re-mapped there (FTrueScopeAnalyzer::
	 * RemapLastResultWithCurrentGains) — this must never fall back to the
	 * readback branch, which exists solely for the manual path switch.
	 *
	 * Non-blocking: it enqueues one render command and returns. Game thread.
	 */
	TrueScope::ERedrawOutcome RedrawFromLastCapture();

	/**
	 * Measurement rate (captures per second, phase-6.1 Task 2). Set from the
	 * game thread, read by the capture throttle. The rate is a user-facing
	 * trade-off — a faster scope costs more, and on the CPU path each capture
	 * blocks the render thread for 1-2 frame-times.
	 */
	void SetMeasurementRateHz(float InRateHz);
	float GetMeasurementRateHz() const { return MeasurementRateHz.load(std::memory_order_relaxed); }

	/** The throttle interval derived from the rate. */
	double GetCaptureIntervalSeconds() const
	{
		return 1.0 / FMath::Max(GetMeasurementRateHz(), KINDA_SMALL_NUMBER);
	}

private:
	FScreenPassTexture AfterTonemapPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	/**
	 * Compares the effective path against the last one seen and, on a change,
	 * makes the switch take effect promptly by re-analysing the resident last
	 * capture (approach A) — no redraw, no new capture, and the user's
	 * Realtime-Viewport setting is never touched. Game thread.
	 */
	void HandleAnalysisPathChanged();
	void HandleAnalysisPathCVarChanged(IConsoleVariable*);

	/** Executes the decided switch action on the resident capture RT. */
	void ReanalyzeLastCapture(TrueScope::EPathSwitchAction Action);

	/** Plugin-owned capture target, kept alive via AddReferencedObjects. */
	TObjectPtr<UTextureRenderTarget2D> CaptureRenderTarget;

	/** Receives the captured texture for async readback + analysis (M3). */
	TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe> Analyzer;

	/** GPU analysis producer (all four scopes); replaces the readback on the GPU path. */
	TSharedPtr<FTrueScopeGpuAnalysis, ESPMode::ThreadSafe> GpuAnalysis;

	/**
	 * Tab-visibility gate (M5): capture runs only while a TrueScope panel is
	 * actually painted — a closed or hidden tab pays no capture cost.
	 */
	TSharedPtr<FTrueScopeCaptureGate, ESPMode::ThreadSafe> CaptureGate;

	/** See SetUseGpuAnalysis. */
	std::atomic<bool> bUseGpuAnalysis{false};

	/** See SetMeasurementRateHz. */
	std::atomic<float> MeasurementRateHz{TrueScope::DefaultMeasurementRateHz};

	/**
	 * Whether at least one capture pass has run — the cold-start guard of the
	 * switch re-analysis (before the first capture the RT holds only its
	 * initial clear). Written on the render thread, read on the game thread.
	 */
	std::atomic<bool> bHasCapturedFrame{false};

	/** Effective path last seen by HandleAnalysisPathChanged. Game thread only. */
	bool bLastEffectiveGpu = false;

	/** Render target resource, cached for the render thread. Render thread only. */
	FTextureRenderTargetResource* CaptureResourceRenderThread = nullptr;

	/**
	 * Time of the last accepted capture; the throttle bookkeeping happens in
	 * IsActiveThisFrame_Internal (game thread only), hence mutable.
	 */
	mutable double LastCaptureTimeSeconds = -1.0e12;

	/** One-time diagnostic log when the capture pass first runs. Render thread. */
	bool bFirstCaptureLogged = false;
};
