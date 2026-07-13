// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "Analysis/TrueScopeGpuParade.h"

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

	class FTrueScopeParadeBinCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FTrueScopeParadeBinCS);
		SHADER_USE_PARAMETER_STRUCT(FTrueScopeParadeBinCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, InputTexture)
			SHADER_PARAMETER(FIntPoint, InputSize)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, ParadeBinsOut)
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

	class FTrueScopeParadeIntensityCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FTrueScopeParadeIntensityCS);
		SHADER_USE_PARAMETER_STRUCT(FTrueScopeParadeIntensityCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ParadeBinsIn)
			SHADER_PARAMETER(FIntPoint, OutputSize)
			SHADER_PARAMETER(uint32, MaxCount)
			SHADER_PARAMETER(float, IntensityGain)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RedOut)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, GreenOut)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, BlueOut)
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

	IMPLEMENT_GLOBAL_SHADER(FTrueScopeParadeBinCS, "/Plugin/TrueScope/TrueScopeParade.usf", "ParadeBinCS", SF_Compute);
	IMPLEMENT_GLOBAL_SHADER(FTrueScopeParadeIntensityCS, "/Plugin/TrueScope/TrueScopeParade.usf", "ParadeIntensityCS",
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
}

namespace TrueScope
{
	FRDGBufferRef AddParadeBinningPass(FRDGBuilder& GraphBuilder, FRDGTextureRef InputTexture, FIntPoint InputSize)
	{
		const int32 NumCells =
			FTrueScopeAnalysisResult::NumChannels * InputSize.X * FTrueScopeAnalysisResult::NumBins;
		FRDGBufferRef Bins = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumCells), TEXT("TrueScope.ParadeBins"));
		FRDGBufferUAVRef BinsUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(Bins, PF_R32_UINT));

		AddClearUAVPass(GraphBuilder, BinsUAV, 0u);

		FTrueScopeParadeBinCS::FParameters* Parameters =
			GraphBuilder.AllocParameters<FTrueScopeParadeBinCS::FParameters>();
		Parameters->InputTexture = InputTexture;
		Parameters->InputSize = InputSize;
		Parameters->ParadeBinsOut = BinsUAV;

		TShaderMapRef<FTrueScopeParadeBinCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(GraphBuilder,
			RDG_EVENT_NAME("TrueScope.ParadeBin %dx%d", InputSize.X, InputSize.Y),
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
}

FTrueScopeGpuParade::FTrueScopeGpuParade(const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& InAnalyzer)
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
}

void FTrueScopeGpuParade::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
	{
		Collector.AddReferencedObject(ChannelTarget[Channel]);
	}
	Collector.AddReferencedObject(VectorscopeTarget);
}

void FTrueScopeGpuParade::CacheResources_GameThread()
{
	FTextureRenderTargetResource* Resources[FTrueScopeAnalysisResult::NumChannels] = {};
	for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
	{
		Resources[Channel] =
			ChannelTarget[Channel] ? ChannelTarget[Channel]->GameThread_GetRenderTargetResource() : nullptr;
	}
	FTextureRenderTargetResource* VectorResource =
		VectorscopeTarget ? VectorscopeTarget->GameThread_GetRenderTargetResource() : nullptr;

	ENQUEUE_RENDER_COMMAND(TrueScopeCacheParadeResources)(
		[WeakThis = AsWeak(), Resources, VectorResource](FRHICommandListImmediate&)
		{
			if (TSharedPtr<FTrueScopeGpuParade> This =
				StaticCastWeakPtr<FTrueScopeGpuParade>(WeakThis).Pin())
			{
				for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
				{
					This->ChannelResourceRenderThread[Channel] = Resources[Channel];
				}
				This->VectorscopeResourceRenderThread = VectorResource;
			}
		});
}

void FTrueScopeGpuParade::AddAnalysisPasses_RenderThread(FRDGBuilder& GraphBuilder, FRDGTextureRef CaptureTexture)
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
	if (!VectorscopeTexture)
	{
		return; // all-or-nothing, same as the parade targets above
	}

	const FIntPoint InputSize(CaptureTexture->Desc.Extent.X, CaptureTexture->Desc.Extent.Y);
	const FIntPoint OutputSize(InputSize.X, FTrueScopeAnalysisResult::NumBins);

	// Pass 1: pixel values -> per-channel column x bin counts.
	FRDGBufferRef Bins = TrueScope::AddParadeBinningPass(GraphBuilder, CaptureTexture, InputSize);

	// Pass 2: counts -> the three single-channel density textures (log
	// count -> brightness mapping, same curve constant as the CPU path).
	FTrueScopeParadeIntensityCS::FParameters* Parameters =
		GraphBuilder.AllocParameters<FTrueScopeParadeIntensityCS::FParameters>();
	Parameters->ParadeBinsIn = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(Bins, PF_R32_UINT));
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

	TShaderMapRef<FTrueScopeParadeIntensityCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FComputeShaderUtils::AddPass(GraphBuilder,
		RDG_EVENT_NAME("TrueScope.ParadeIntensity %dx%d", OutputSize.X, OutputSize.Y),
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

	if (Analyzer)
	{
		Analyzer->PublishResult(Result);
	}
}
