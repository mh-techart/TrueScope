// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "UObject/GCObject.h"
#include "UObject/ObjectPtr.h"

class FRDGBuilder;
class FTextureRenderTargetResource;
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
	FTrueScopeViewExtension(const FAutoRegister& AutoRegister);

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

private:
	FScreenPassTexture AfterTonemapPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	/** Plugin-owned capture target, kept alive via AddReferencedObjects. */
	TObjectPtr<UTextureRenderTarget2D> CaptureRenderTarget;

	/** Render target resource, cached for the render thread. Render thread only. */
	FTextureRenderTargetResource* CaptureResourceRenderThread = nullptr;

	/**
	 * Time of the last accepted capture; the throttle bookkeeping happens in
	 * IsActiveThisFrame_Internal (game thread only), hence mutable.
	 */
	mutable double LastCaptureTimeSeconds = -1.0e12;
};
