// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "Analysis/TrueScopeGpuAnalysis.h"

#include "Analysis/TrueScopeAnalysis.h"
#include "Analysis/TrueScopeAnalyzer.h"
#include "Capture/TrueScopeViewExtension.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "ShaderParameterStruct.h"
#include "TextureResource.h"
#include "UI/TrueScopeDraw.h" // histogram curve colours, shared with the widget

namespace
{
	/** Square thread-group side of both parade passes (64 threads per group). */
	constexpr int32 ThreadGroupSize = 8;

	/**
	 * Shared compilation setup of both parade shaders: NUM_BINS is generated
	 * from FTrueScopeAnalysisResult::NumBins so the buffer layout cannot drift
	 * from the CPU path's, and only SM5+ platforms compile (compute + typed
	 * UAVs).
	 */
	void ModifyParadeShaderEnvironment(FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("NUM_BINS"), FTrueScopeAnalysisResult::NumBins);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ThreadGroupSize);
	}

	class FTrueScopeColumnBinCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FTrueScopeColumnBinCS);
		SHADER_USE_PARAMETER_STRUCT(FTrueScopeColumnBinCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, InputTexture)
			SHADER_PARAMETER(FIntPoint, InputSize)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, ColumnBinsOut)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters,
			FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			ModifyParadeShaderEnvironment(OutEnvironment);
		}
	};

	class FTrueScopeColumnIntensityCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FTrueScopeColumnIntensityCS);
		SHADER_USE_PARAMETER_STRUCT(FTrueScopeColumnIntensityCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ColumnBinsIn)
			SHADER_PARAMETER(FIntPoint, OutputSize)
			SHADER_PARAMETER(uint32, MaxCount)
			SHADER_PARAMETER(float, IntensityGain)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RedOut)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, GreenOut)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, BlueOut)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, WaveformRgbOut)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, WaveformLumaOut)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters,
			FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			ModifyParadeShaderEnvironment(OutEnvironment);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FTrueScopeColumnBinCS, "/Plugin/TrueScope/TrueScopeParade.usf", "ColumnBinCS", SF_Compute);
	IMPLEMENT_GLOBAL_SHADER(FTrueScopeColumnIntensityCS, "/Plugin/TrueScope/TrueScopeParade.usf", "ColumnIntensityCS",
		SF_Compute);

	/**
	 * Shared compilation setup of both vectorscope shaders (phase-5.1):
	 * NUM_VECTOR_BINS is generated from FTrueScopeAnalysisResult::NumVectorBins
	 * so the grid layout cannot drift from the CPU path's.
	 */
	void ModifyVectorscopeShaderEnvironment(FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("NUM_VECTOR_BINS"), FTrueScopeAnalysisResult::NumVectorBins);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ThreadGroupSize);
	}

	class FTrueScopeVectorBinCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FTrueScopeVectorBinCS);
		SHADER_USE_PARAMETER_STRUCT(FTrueScopeVectorBinCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, InputTexture)
			SHADER_PARAMETER(FIntPoint, InputSize)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, VectorBinsOut)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters,
			FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			ModifyVectorscopeShaderEnvironment(OutEnvironment);
		}
	};

	class FTrueScopeVectorIntensityCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FTrueScopeVectorIntensityCS);
		SHADER_USE_PARAMETER_STRUCT(FTrueScopeVectorIntensityCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, VectorBinsIn)
			SHADER_PARAMETER(uint32, MaxCount)
			SHADER_PARAMETER(float, IntensityGain)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, VectorscopeOut)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters,
			FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			ModifyVectorscopeShaderEnvironment(OutEnvironment);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FTrueScopeVectorBinCS, "/Plugin/TrueScope/TrueScopeVectorscope.usf", "VectorBinCS",
		SF_Compute);
	IMPLEMENT_GLOBAL_SHADER(FTrueScopeVectorIntensityCS, "/Plugin/TrueScope/TrueScopeVectorscope.usf",
		"VectorIntensityCS", SF_Compute);

	// --- Histogram (phase-5.2) ---------------------------------------------

	/** Threads per group of the 1D histogram reduce pass. */
	constexpr int32 HistogramReduceGroupSize = 64;

	/**
	 * Raster sizes of the two histogram curve sets. The Separate Channels
	 * bands are about a third of the plot height while RGB Overlay uses the
	 * whole plot, so each set is rasterized near its mode's on-screen size —
	 * keeping the outline's on-screen width close to the CPU draw's 1 px line
	 * (a single texture would render the line 3x too wide in one mode or
	 * undersampled to dashes in the other).
	 */
	const FIntPoint HistogramStackedSize(512, 144);
	const FIntPoint HistogramOverlaySize(512, 432);

	/** Outline half-thickness in raster texels (the CPU line is 1 px, AA'd). */
	constexpr float HistogramLineHalfWidth = 1.0f;

	void ModifyHistogramShaderEnvironment(FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("NUM_BINS"), FTrueScopeAnalysisResult::NumBins);
		OutEnvironment.SetDefine(TEXT("REDUCE_GROUP_SIZE"), HistogramReduceGroupSize);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ThreadGroupSize);
	}

	class FTrueScopeHistogramReduceCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FTrueScopeHistogramReduceCS);
		SHADER_USE_PARAMETER_STRUCT(FTrueScopeHistogramReduceCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ColumnBinsIn)
			SHADER_PARAMETER(int32, SourceWidth)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, HistogramOut)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, SharedMaxOut)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters,
			FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			ModifyHistogramShaderEnvironment(OutEnvironment);
		}
	};

	/**
	 * Shared parameters of the two histogram draw entry points. Each entry
	 * point references only its own output UAVs, so the other set stays null.
	 */
	BEGIN_SHADER_PARAMETER_STRUCT(FTrueScopeHistogramDrawParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, HistogramIn)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, SharedMaxIn)
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, LineHalfWidth)
		SHADER_PARAMETER(FVector4f, FillColorR)
		SHADER_PARAMETER(FVector4f, FillColorG)
		SHADER_PARAMETER(FVector4f, FillColorB)
		SHADER_PARAMETER(FVector4f, LineColorR)
		SHADER_PARAMETER(FVector4f, LineColorG)
		SHADER_PARAMETER(FVector4f, LineColorB)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, StackedROut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, StackedGOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, StackedBOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OverlayOut)
	END_SHADER_PARAMETER_STRUCT()

	class FTrueScopeHistogramDrawStackedCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FTrueScopeHistogramDrawStackedCS);
		SHADER_USE_PARAMETER_STRUCT(FTrueScopeHistogramDrawStackedCS, FGlobalShader);
		using FParameters = FTrueScopeHistogramDrawParameters;

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters,
			FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			ModifyHistogramShaderEnvironment(OutEnvironment);
		}
	};

	class FTrueScopeHistogramDrawOverlayCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FTrueScopeHistogramDrawOverlayCS);
		SHADER_USE_PARAMETER_STRUCT(FTrueScopeHistogramDrawOverlayCS, FGlobalShader);
		using FParameters = FTrueScopeHistogramDrawParameters;

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters,
			FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			ModifyHistogramShaderEnvironment(OutEnvironment);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FTrueScopeHistogramReduceCS, "/Plugin/TrueScope/TrueScopeHistogram.usf",
		"HistogramReduceCS", SF_Compute);
	IMPLEMENT_GLOBAL_SHADER(FTrueScopeHistogramDrawStackedCS, "/Plugin/TrueScope/TrueScopeHistogram.usf",
		"HistogramDrawStackedCS", SF_Compute);
	IMPLEMENT_GLOBAL_SHADER(FTrueScopeHistogramDrawOverlayCS, "/Plugin/TrueScope/TrueScopeHistogram.usf",
		"HistogramDrawOverlayCS", SF_Compute);

	/** Fills the draw parameters shared by both histogram draw dispatches. */
	void FillHistogramDrawParameters(FTrueScopeHistogramDrawParameters& Parameters,
		FRDGBufferSRVRef HistogramSRV, FRDGBufferSRVRef SharedMaxSRV, FIntPoint OutputSize)
	{
		Parameters.HistogramIn = HistogramSRV;
		Parameters.SharedMaxIn = SharedMaxSRV;
		Parameters.OutputSize = OutputSize;
		Parameters.LineHalfWidth = HistogramLineHalfWidth;
		// The widget's exact curve colours (TrueScopeDraw.h), so both paths
		// draw pixel-identical colours by construction.
		Parameters.FillColorR = FVector4f(TrueScope::Draw::HistogramFillColors[0]);
		Parameters.FillColorG = FVector4f(TrueScope::Draw::HistogramFillColors[1]);
		Parameters.FillColorB = FVector4f(TrueScope::Draw::HistogramFillColors[2]);
		Parameters.LineColorR = FVector4f(TrueScope::Draw::HistogramLineColors[0]);
		Parameters.LineColorG = FVector4f(TrueScope::Draw::HistogramLineColors[1]);
		Parameters.LineColorB = FVector4f(TrueScope::Draw::HistogramLineColors[2]);
	}
}

namespace TrueScope
{
	FRDGBufferRef AddColumnBinningPass(FRDGBuilder& GraphBuilder, FRDGTextureRef InputTexture, FIntPoint InputSize)
	{
		const int32 NumCells = ColumnBinChannels * InputSize.X * FTrueScopeAnalysisResult::NumBins;
		FRDGBufferRef Bins = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumCells), TEXT("TrueScope.ColumnBins"));
		FRDGBufferUAVRef BinsUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(Bins, PF_R32_UINT));

		AddClearUAVPass(GraphBuilder, BinsUAV, 0u);

		FTrueScopeColumnBinCS::FParameters* Parameters =
			GraphBuilder.AllocParameters<FTrueScopeColumnBinCS::FParameters>();
		Parameters->InputTexture = InputTexture;
		Parameters->InputSize = InputSize;
		Parameters->ColumnBinsOut = BinsUAV;

		TShaderMapRef<FTrueScopeColumnBinCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(GraphBuilder,
			RDG_EVENT_NAME("TrueScope.ColumnBin %dx%d", InputSize.X, InputSize.Y),
			ComputeShader, Parameters,
			FComputeShaderUtils::GetGroupCount(InputSize, ThreadGroupSize));

		return Bins;
	}

	FRDGBufferRef AddVectorscopeBinningPass(FRDGBuilder& GraphBuilder, FRDGTextureRef InputTexture,
		FIntPoint InputSize)
	{
		const int32 NumCells = FTrueScopeAnalysisResult::NumVectorBins * FTrueScopeAnalysisResult::NumVectorBins;
		FRDGBufferRef Bins = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumCells), TEXT("TrueScope.VectorBins"));
		FRDGBufferUAVRef BinsUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(Bins, PF_R32_UINT));

		AddClearUAVPass(GraphBuilder, BinsUAV, 0u);

		FTrueScopeVectorBinCS::FParameters* Parameters =
			GraphBuilder.AllocParameters<FTrueScopeVectorBinCS::FParameters>();
		Parameters->InputTexture = InputTexture;
		Parameters->InputSize = InputSize;
		Parameters->VectorBinsOut = BinsUAV;

		TShaderMapRef<FTrueScopeVectorBinCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(GraphBuilder,
			RDG_EVENT_NAME("TrueScope.VectorBin %dx%d", InputSize.X, InputSize.Y),
			ComputeShader, Parameters,
			FComputeShaderUtils::GetGroupCount(InputSize, ThreadGroupSize));

		return Bins;
	}

	FHistogramReduceOutputs AddHistogramReducePass(FRDGBuilder& GraphBuilder, FRDGBufferRef ColumnBins,
		int32 SourceWidth)
	{
		constexpr int32 NumCells = FTrueScopeAnalysisResult::NumChannels * FTrueScopeAnalysisResult::NumBins;

		FHistogramReduceOutputs Outputs;
		Outputs.Bins = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumCells), TEXT("TrueScope.HistogramBins"));
		Outputs.SharedMax = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("TrueScope.HistogramSharedMax"));

		FRDGBufferUAVRef SharedMaxUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(Outputs.SharedMax, PF_R32_UINT));
		AddClearUAVPass(GraphBuilder, SharedMaxUAV, 0u);

		FTrueScopeHistogramReduceCS::FParameters* Parameters =
			GraphBuilder.AllocParameters<FTrueScopeHistogramReduceCS::FParameters>();
		Parameters->ColumnBinsIn = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ColumnBins, PF_R32_UINT));
		Parameters->SourceWidth = SourceWidth;
		Parameters->HistogramOut = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(Outputs.Bins, PF_R32_UINT));
		Parameters->SharedMaxOut = SharedMaxUAV;

		TShaderMapRef<FTrueScopeHistogramReduceCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(GraphBuilder,
			RDG_EVENT_NAME("TrueScope.HistogramReduce %d cells", NumCells),
			ComputeShader, Parameters,
			FIntVector(FMath::DivideAndRoundUp(NumCells, HistogramReduceGroupSize), 1, 1));

		return Outputs;
	}
}

FTrueScopeGpuAnalysis::FTrueScopeGpuAnalysis(const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& InAnalyzer)
	: Analyzer(InAnalyzer)
{
	// Fixed size: capture width x value bins, the CPU parade image's layout.
	// PF_R8G8B8A8 (not the RTF_RGBA8 default PF_B8G8R8A8): typed-UAV stores on
	// BGRA8 are not guaranteed on all RHIs, RGBA8 unorm is (Task 0 finding).
	// Linear gamma: raw intensity values, drawn with NoGamma like the CPU
	// path's SRGB=false transient textures.
	for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
		Target->bCanCreateUAV = true;
		Target->ClearColor = FLinearColor::Black;
		Target->Filter = TF_Bilinear;
		Target->InitCustomFormat(TrueScope::CaptureWidth, FTrueScopeAnalysisResult::NumBins, PF_R8G8B8A8,
			/*bInForceLinearGamma*/ true);
		Target->UpdateResourceImmediate(/*bClearRenderTarget*/ true);
		ChannelTarget[Channel] = Target;
		ChannelTargetWeak[Channel] = Target;
	}

	// Vectorscope density target (phase-5.1): the (Cb, Cr) grid, same format
	// rationale as the parade targets.
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
		Target->bCanCreateUAV = true;
		Target->ClearColor = FLinearColor::Black;
		Target->Filter = TF_Bilinear;
		Target->InitCustomFormat(FTrueScopeAnalysisResult::NumVectorBins, FTrueScopeAnalysisResult::NumVectorBins,
			PF_R8G8B8A8, /*bInForceLinearGamma*/ true);
		Target->UpdateResourceImmediate(/*bClearRenderTarget*/ true);
		VectorscopeTarget = Target;
		VectorscopeTargetWeak = Target;
	}

	// Waveform trace targets (phase-5.2): full-width RGB overlay and neutral
	// luma trace, same layout as the parade channel targets.
	for (TObjectPtr<UTextureRenderTarget2D>* TargetSlot : {&WaveformRgbTarget, &WaveformLumaTarget})
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
		Target->bCanCreateUAV = true;
		Target->ClearColor = FLinearColor::Black;
		Target->Filter = TF_Bilinear;
		Target->InitCustomFormat(TrueScope::CaptureWidth, FTrueScopeAnalysisResult::NumBins, PF_R8G8B8A8,
			/*bInForceLinearGamma*/ true);
		Target->UpdateResourceImmediate(/*bClearRenderTarget*/ true);
		*TargetSlot = Target;
	}
	WaveformRgbTargetWeak = WaveformRgbTarget;
	WaveformLumaTargetWeak = WaveformLumaTarget;

	// Histogram curve targets (phase-5.2): coverage rasterizations carrying
	// straight (non-premultiplied) RGBA, drawn by Slate's standard blending.
	// Cleared to transparent so the pre-first-capture state draws nothing.
	auto MakeHistogramTarget = [](FIntPoint Size)
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
		Target->bCanCreateUAV = true;
		Target->ClearColor = FLinearColor::Transparent;
		Target->Filter = TF_Bilinear;
		Target->InitCustomFormat(Size.X, Size.Y, PF_R8G8B8A8, /*bInForceLinearGamma*/ true);
		Target->UpdateResourceImmediate(/*bClearRenderTarget*/ true);
		return Target;
	};
	for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
	{
		HistogramStackedTarget[Channel] = MakeHistogramTarget(HistogramStackedSize);
		HistogramStackedTargetWeak[Channel] = HistogramStackedTarget[Channel];
	}
	HistogramOverlayTarget = MakeHistogramTarget(HistogramOverlaySize);
	HistogramOverlayTargetWeak = HistogramOverlayTarget;
}

void FTrueScopeGpuAnalysis::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
	{
		Collector.AddReferencedObject(ChannelTarget[Channel]);
	}
	Collector.AddReferencedObject(VectorscopeTarget);
	Collector.AddReferencedObject(WaveformRgbTarget);
	Collector.AddReferencedObject(WaveformLumaTarget);
	for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
	{
		Collector.AddReferencedObject(HistogramStackedTarget[Channel]);
	}
	Collector.AddReferencedObject(HistogramOverlayTarget);
}

void FTrueScopeGpuAnalysis::CacheResources_GameThread()
{
	FTextureRenderTargetResource* Resources[FTrueScopeAnalysisResult::NumChannels] = {};
	for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
	{
		Resources[Channel] =
			ChannelTarget[Channel] ? ChannelTarget[Channel]->GameThread_GetRenderTargetResource() : nullptr;
	}
	FTextureRenderTargetResource* VectorResource =
		VectorscopeTarget ? VectorscopeTarget->GameThread_GetRenderTargetResource() : nullptr;
	FTextureRenderTargetResource* WaveformRgbResource =
		WaveformRgbTarget ? WaveformRgbTarget->GameThread_GetRenderTargetResource() : nullptr;
	FTextureRenderTargetResource* WaveformLumaResource =
		WaveformLumaTarget ? WaveformLumaTarget->GameThread_GetRenderTargetResource() : nullptr;
	FTextureRenderTargetResource* HistogramStackedResources[FTrueScopeAnalysisResult::NumChannels] = {};
	for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
	{
		HistogramStackedResources[Channel] = HistogramStackedTarget[Channel]
			? HistogramStackedTarget[Channel]->GameThread_GetRenderTargetResource()
			: nullptr;
	}
	FTextureRenderTargetResource* HistogramOverlayResource =
		HistogramOverlayTarget ? HistogramOverlayTarget->GameThread_GetRenderTargetResource() : nullptr;

	ENQUEUE_RENDER_COMMAND(TrueScopeCacheAnalysisResources)(
		[WeakThis = AsWeak(), Resources, VectorResource, WaveformRgbResource, WaveformLumaResource,
			HistogramStackedResources, HistogramOverlayResource](FRHICommandListImmediate&)
		{
			if (TSharedPtr<FTrueScopeGpuAnalysis> This =
				StaticCastWeakPtr<FTrueScopeGpuAnalysis>(WeakThis).Pin())
			{
				for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
				{
					This->ChannelResourceRenderThread[Channel] = Resources[Channel];
					This->HistogramStackedResourceRenderThread[Channel] = HistogramStackedResources[Channel];
				}
				This->VectorscopeResourceRenderThread = VectorResource;
				This->WaveformRgbResourceRenderThread = WaveformRgbResource;
				This->WaveformLumaResourceRenderThread = WaveformLumaResource;
				This->HistogramOverlayResourceRenderThread = HistogramOverlayResource;
			}
		});
}

void FTrueScopeGpuAnalysis::AddAnalysisPasses_RenderThread(FRDGBuilder& GraphBuilder, FRDGTextureRef CaptureTexture)
{
	check(IsInRenderingThread());

	FRHITexture* TargetTextures[FTrueScopeAnalysisResult::NumChannels] = {};
	for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
	{
		TargetTextures[Channel] = ChannelResourceRenderThread[Channel]
			? ChannelResourceRenderThread[Channel]->GetRenderTargetTexture()
			: nullptr;
		if (!TargetTextures[Channel])
		{
			return; // resources not cached yet; the next capture will have them
		}
	}
	FRHITexture* VectorscopeTexture =
		VectorscopeResourceRenderThread ? VectorscopeResourceRenderThread->GetRenderTargetTexture() : nullptr;
	FRHITexture* WaveformRgbTexture =
		WaveformRgbResourceRenderThread ? WaveformRgbResourceRenderThread->GetRenderTargetTexture() : nullptr;
	FRHITexture* WaveformLumaTexture =
		WaveformLumaResourceRenderThread ? WaveformLumaResourceRenderThread->GetRenderTargetTexture() : nullptr;
	FRHITexture* HistogramStackedTextures[FTrueScopeAnalysisResult::NumChannels] = {};
	for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
	{
		HistogramStackedTextures[Channel] = HistogramStackedResourceRenderThread[Channel]
			? HistogramStackedResourceRenderThread[Channel]->GetRenderTargetTexture()
			: nullptr;
	}
	FRHITexture* HistogramOverlayTexture =
		HistogramOverlayResourceRenderThread ? HistogramOverlayResourceRenderThread->GetRenderTargetTexture() : nullptr;
	if (!VectorscopeTexture || !WaveformRgbTexture || !WaveformLumaTexture || !HistogramStackedTextures[0]
		|| !HistogramStackedTextures[1] || !HistogramStackedTextures[2] || !HistogramOverlayTexture)
	{
		return; // all-or-nothing, same as the parade targets above
	}

	const FIntPoint InputSize(CaptureTexture->Desc.Extent.X, CaptureTexture->Desc.Extent.Y);
	const FIntPoint OutputSize(InputSize.X, FTrueScopeAnalysisResult::NumBins);

	// Pass 1: pixel values -> per-channel column x bin counts (R, G, B, luma).
	FRDGBufferRef Bins = TrueScope::AddColumnBinningPass(GraphBuilder, CaptureTexture, InputSize);

	// Pass 2: counts -> the three single-channel parade density textures and
	// the two waveform traces (log count -> brightness mapping, same curve
	// constant as the CPU path).
	FTrueScopeColumnIntensityCS::FParameters* Parameters =
		GraphBuilder.AllocParameters<FTrueScopeColumnIntensityCS::FParameters>();
	Parameters->ColumnBinsIn = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(Bins, PF_R32_UINT));
	Parameters->OutputSize = OutputSize;
	Parameters->MaxCount = static_cast<uint32>(InputSize.Y);
	Parameters->IntensityGain = TrueScope::DensityIntensityGain;

	FRDGTextureUAVRef ChannelUAV[FTrueScopeAnalysisResult::NumChannels];
	static const TCHAR* TargetNames[FTrueScopeAnalysisResult::NumChannels] = {
		TEXT("TrueScope.ParadeRed"), TEXT("TrueScope.ParadeGreen"), TEXT("TrueScope.ParadeBlue")};
	for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
	{
		FRDGTextureRef Target =
			RegisterExternalTexture(GraphBuilder, TargetTextures[Channel], TargetNames[Channel]);
		ChannelUAV[Channel] = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Target));
	}
	Parameters->RedOut = ChannelUAV[FTrueScopeAnalysisResult::ChannelR];
	Parameters->GreenOut = ChannelUAV[FTrueScopeAnalysisResult::ChannelG];
	Parameters->BlueOut = ChannelUAV[FTrueScopeAnalysisResult::ChannelB];
	Parameters->WaveformRgbOut = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(
		RegisterExternalTexture(GraphBuilder, WaveformRgbTexture, TEXT("TrueScope.WaveformRgb"))));
	Parameters->WaveformLumaOut = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(
		RegisterExternalTexture(GraphBuilder, WaveformLumaTexture, TEXT("TrueScope.WaveformLuma"))));

	TShaderMapRef<FTrueScopeColumnIntensityCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FComputeShaderUtils::AddPass(GraphBuilder,
		RDG_EVENT_NAME("TrueScope.ColumnIntensity %dx%d", OutputSize.X, OutputSize.Y),
		ComputeShader, Parameters,
		FComputeShaderUtils::GetGroupCount(OutputSize, ThreadGroupSize));

	// Vectorscope (phase-5.1): binning pass into the (Cb, Cr) grid, then the
	// log intensity pass into the vectorscope density target.
	{
		constexpr int32 NumVectorBins = FTrueScopeAnalysisResult::NumVectorBins;

		FRDGBufferRef VectorBins = TrueScope::AddVectorscopeBinningPass(GraphBuilder, CaptureTexture, InputSize);

		FTrueScopeVectorIntensityCS::FParameters* VectorParameters =
			GraphBuilder.AllocParameters<FTrueScopeVectorIntensityCS::FParameters>();
		VectorParameters->VectorBinsIn = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(VectorBins, PF_R32_UINT));
		// Max possible count in one (Cb, Cr) cell: the whole frame on one chroma
		// coordinate — the CPU path's normalization (BuildVectorscopeImage).
		VectorParameters->MaxCount = static_cast<uint32>(InputSize.X * InputSize.Y);
		VectorParameters->IntensityGain = TrueScope::DensityIntensityGain;

		FRDGTextureRef VectorTarget =
			RegisterExternalTexture(GraphBuilder, VectorscopeTexture, TEXT("TrueScope.Vectorscope"));
		VectorParameters->VectorscopeOut = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(VectorTarget));

		TShaderMapRef<FTrueScopeVectorIntensityCS> VectorComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(GraphBuilder,
			RDG_EVENT_NAME("TrueScope.VectorIntensity %dx%d", NumVectorBins, NumVectorBins),
			VectorComputeShader, VectorParameters,
			FComputeShaderUtils::GetGroupCount(FIntPoint(NumVectorBins, NumVectorBins), ThreadGroupSize));
	}

	// Histogram (phase-5.2): reduce the column bins into the per-channel 1D
	// histogram plus the shared maximum, then rasterize the curve textures —
	// count -> curve HEIGHT, linear against the shared maximum
	// (CountToCurveHeight), never the log density mapping above.
	{
		const TrueScope::FHistogramReduceOutputs Histogram =
			TrueScope::AddHistogramReducePass(GraphBuilder, Bins, InputSize.X);
		FRDGBufferSRVRef HistogramSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(Histogram.Bins, PF_R32_UINT));
		FRDGBufferSRVRef SharedMaxSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(Histogram.SharedMax, PF_R32_UINT));

		// Stacked (Separate Channels) set: one texture per channel.
		{
			FTrueScopeHistogramDrawParameters* DrawParameters =
				GraphBuilder.AllocParameters<FTrueScopeHistogramDrawParameters>();
			FillHistogramDrawParameters(*DrawParameters, HistogramSRV, SharedMaxSRV, HistogramStackedSize);
			static const TCHAR* StackedNames[FTrueScopeAnalysisResult::NumChannels] = {
				TEXT("TrueScope.HistogramStackedR"), TEXT("TrueScope.HistogramStackedG"),
				TEXT("TrueScope.HistogramStackedB")};
			FRDGTextureUAVRef StackedUAV[FTrueScopeAnalysisResult::NumChannels];
			for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
			{
				StackedUAV[Channel] = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RegisterExternalTexture(
					GraphBuilder, HistogramStackedTextures[Channel], StackedNames[Channel])));
			}
			DrawParameters->StackedROut = StackedUAV[FTrueScopeAnalysisResult::ChannelR];
			DrawParameters->StackedGOut = StackedUAV[FTrueScopeAnalysisResult::ChannelG];
			DrawParameters->StackedBOut = StackedUAV[FTrueScopeAnalysisResult::ChannelB];

			TShaderMapRef<FTrueScopeHistogramDrawStackedCS> StackedShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FComputeShaderUtils::AddPass(GraphBuilder,
				RDG_EVENT_NAME("TrueScope.HistogramDrawStacked %dx%d", HistogramStackedSize.X, HistogramStackedSize.Y),
				StackedShader, DrawParameters,
				FComputeShaderUtils::GetGroupCount(HistogramStackedSize, ThreadGroupSize));
		}

		// Overlay set: all three channels composited into one texture.
		{
			FTrueScopeHistogramDrawParameters* DrawParameters =
				GraphBuilder.AllocParameters<FTrueScopeHistogramDrawParameters>();
			FillHistogramDrawParameters(*DrawParameters, HistogramSRV, SharedMaxSRV, HistogramOverlaySize);
			DrawParameters->OverlayOut = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RegisterExternalTexture(
				GraphBuilder, HistogramOverlayTexture, TEXT("TrueScope.HistogramOverlay"))));

			TShaderMapRef<FTrueScopeHistogramDrawOverlayCS> OverlayShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FComputeShaderUtils::AddPass(GraphBuilder,
				RDG_EVENT_NAME("TrueScope.HistogramDrawOverlay %dx%d", HistogramOverlaySize.X, HistogramOverlaySize.Y),
				OverlayShader, DrawParameters,
				FComputeShaderUtils::GetGroupCount(HistogramOverlaySize, ThreadGroupSize));
		}
	}

	// Publish the GPU-resident result: metadata plus the target textures the
	// widgets draw directly. The count/intensity arrays stay empty — there is
	// no CPU copy of anything on this path, by design.
	TSharedRef<FTrueScopeAnalysisResult, ESPMode::ThreadSafe> Result =
		MakeShared<FTrueScopeAnalysisResult, ESPMode::ThreadSafe>();
	Result->SourceSize = InputSize;
	Result->ParadeImageSize = OutputSize;
	Result->bHasGpuParade = true;
	for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
	{
		Result->ParadeChannelTarget[Channel] = ChannelTargetWeak[Channel];
	}
	Result->bHasGpuVectorscope = true;
	Result->VectorscopeTarget = VectorscopeTargetWeak;
	Result->bHasGpuWaveform = true;
	Result->WaveformRgbTarget = WaveformRgbTargetWeak;
	Result->WaveformLumaTarget = WaveformLumaTargetWeak;
	Result->bHasGpuHistogram = true;
	for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
	{
		Result->HistogramStackedTarget[Channel] = HistogramStackedTargetWeak[Channel];
	}
	Result->HistogramOverlayTarget = HistogramOverlayTargetWeak;

	if (Analyzer)
	{
		Analyzer->PublishResult(Result);
	}
}
