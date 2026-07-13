// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Capture/TrueScopePathSwitch.h"
#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "UObject/GCObject.h"
#include "UObject/ObjectPtr.h"

#include <atomic>

class IConsoleVariable;

class FRDGBuilder;
class FTextureRenderTargetResource;
class FTrueScopeAnalyzer;
class FTrueScopeCaptureGate;
class FTrueScopeGpuParade;
class UTextureRenderTarget2D;
struct FPostProcessMaterialInputs;
struct FScreenPassTexture;

namespace TrueScope
{
	/** Fixed working resolution of the capture render target (M2; settings UI comes later). */
	constexpr int32 CaptureWidth = 960;
	constexpr int32 CaptureHeight = 540;

	/** Minimum time between captures — ~2.5 Hz refresh (M2 fixed constant). */
	constexpr double CaptureIntervalSeconds = 0.4;

	/**
	 * Must stay below the OCIO display extension's priority (100, see
	 * OPENCOLORIO_SCENE_VIEW_EXTENSION_PRIORITY): extensions are sorted by
	 * priority descending and their after-Tonemap callbacks execute in that
	 * order, each receiving the previous one's output — lower priority means
	 * TrueScope samples the post-OCIO image.
	 */
	constexpr int32 CaptureExtensionPriority = 0;
}

/**
 * Captures the post-tonemap (and post-OCIO) display-encoded image of the last
 * focused Level Editor viewport into a plugin-owned render target, downscaled
 * to a fixed working resolution and throttled to a few updates per second.
 *
 * Read-only pass: subscribes to the after-Tonemap callback chain, copies the
 * incoming scene color into the capture render target and returns the scene
 * color untouched. The Tonemap after-pass callbacks are the only ones invoked
 * on both the full and the minimal (post-processing disabled) render paths,
 * and they run regardless of the Tonemapper show flag — see PostProcessing.cpp.
 */
class FTrueScopeViewExtension : public FSceneViewExtensionBase, public FGCObject
{
public:
	FTrueScopeViewExtension(const FAutoRegister& AutoRegister,
		const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& InAnalyzer,
		const TSharedPtr<FTrueScopeGpuParade, ESPMode::ThreadSafe>& InGpuParade,
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

private:
	FScreenPassTexture AfterTonemapPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	/** Effective path after the diagnostic CVar override. Game thread. */
	bool ResolveEffectiveUseGpu() const;

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
	TSharedPtr<FTrueScopeGpuParade, ESPMode::ThreadSafe> GpuParade;

	/**
	 * Tab-visibility gate (M5): capture runs only while a TrueScope panel is
	 * actually painted — a closed or hidden tab pays no capture cost.
	 */
	TSharedPtr<FTrueScopeCaptureGate, ESPMode::ThreadSafe> CaptureGate;

	/** See SetUseGpuAnalysis. */
	std::atomic<bool> bUseGpuAnalysis{false};

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
