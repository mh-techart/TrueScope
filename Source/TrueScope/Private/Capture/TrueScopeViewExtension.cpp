// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "Capture/TrueScopeViewExtension.h"

#include "Analysis/TrueScopeAnalyzer.h"
#include "CommonRenderResources.h"
#include "Engine/TextureRenderTarget2D.h"
#include "LevelEditorViewport.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RHIStaticStates.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneView.h"
#include "ScreenPass.h"
#include "TextureResource.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogTrueScope, Log, All);

FTrueScopeViewExtension::FTrueScopeViewExtension(const FAutoRegister& AutoRegister,
	const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& InAnalyzer)
	: FSceneViewExtensionBase(AutoRegister)
	, Analyzer(InAnalyzer)
{
	CaptureRenderTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
	// Non-sRGB: the after-tonemap scene color already holds display-encoded
	// values; the capture must copy them 1:1 without hardware re-encoding.
	CaptureRenderTarget->RenderTargetFormat = RTF_RGBA8;
	CaptureRenderTarget->ClearColor = FLinearColor::Black;
	CaptureRenderTarget->InitAutoFormat(TrueScope::CaptureWidth, TrueScope::CaptureHeight);
	CaptureRenderTarget->UpdateResourceImmediate(/*bClearRenderTarget*/ true);
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

	// Throttle: skipping activation entirely makes a skipped frame cost nothing.
	const double Now = FPlatformTime::Seconds();
	if (Now - LastCaptureTimeSeconds < TrueScope::CaptureIntervalSeconds)
	{
		return false;
	}

	LastCaptureTimeSeconds = Now;
	return true;
}

void FTrueScopeViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	// Resolve the RT resource on the game thread and hand the pointer to the
	// render thread (same pattern as the OCIO display extension's resource cache).
	FTextureRenderTargetResource* Resource =
		CaptureRenderTarget ? CaptureRenderTarget->GameThread_GetRenderTargetResource() : nullptr;

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
	// Tonemap is the only pass whose after-callbacks run on both the full and
	// the minimal post-processing path; OCIO applies its transform in the same
	// callback chain, ahead of us by priority (see header).
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

			// M3: enqueue the async CPU readback of the freshly written
			// capture in the same graph (RDG orders it after the draw).
			if (Analyzer)
			{
				Analyzer->EnqueueReadback_RenderThread(GraphBuilder, OutputTexture);
			}

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
