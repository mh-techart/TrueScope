// Copyright 2026 Michael Herber
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
#include "UI/TrueScopeDraw.h" // histogram curve colours + line thickness, shared with the widget

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

	/**
	 * Shared parameters of the two intensity entry points. The parade and the
	 * waveform sit in different panels, so they rasterize into differently sized
	 * rects (phase-6.1: each raster is its widget's plot rect in device pixels)
	 * and are two dispatches over the same count buffer. Each entry point
	 * references only its own output UAVs, so the other set stays null.
	 */
	BEGIN_SHADER_PARAMETER_STRUCT(FTrueScopeColumnIntensityParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ColumnBinsIn)
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(int32, SourceWidth)
		SHADER_PARAMETER(uint32, MaxCount)
		SHADER_PARAMETER(float, IntensityGain)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RedOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, GreenOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, BlueOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, WaveformRgbOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, WaveformLumaOut)
	END_SHADER_PARAMETER_STRUCT()

	class FTrueScopeColumnIntensityParadeCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FTrueScopeColumnIntensityParadeCS);
		SHADER_USE_PARAMETER_STRUCT(FTrueScopeColumnIntensityParadeCS, FGlobalShader);
		using FParameters = FTrueScopeColumnIntensityParameters;

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

	class FTrueScopeColumnIntensityWaveformCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FTrueScopeColumnIntensityWaveformCS);
		SHADER_USE_PARAMETER_STRUCT(FTrueScopeColumnIntensityWaveformCS, FGlobalShader);
		using FParameters = FTrueScopeColumnIntensityParameters;

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
	IMPLEMENT_GLOBAL_SHADER(FTrueScopeColumnIntensityParadeCS, "/Plugin/TrueScope/TrueScopeParade.usf",
		"ColumnIntensityParadeCS", SF_Compute);
	IMPLEMENT_GLOBAL_SHADER(FTrueScopeColumnIntensityWaveformCS, "/Plugin/TrueScope/TrueScopeParade.usf",
		"ColumnIntensityWaveformCS", SF_Compute);

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
			SHADER_PARAMETER(FIntPoint, OutputSize)
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
	 * Outline thickness in raster texels. The shader reproduces Slate's own
	 * antialiased-line coverage from it, so feeding it the same constant the
	 * widget passes to MakeLines is what keeps the two strokes identical — and
	 * since phase-6.1 one raster texel IS one device pixel, the two constants
	 * finally mean the same thing: Slate's thickness is in screen-space pixels
	 * (FLineBuilder divides it by the element scale), so a device-resolution
	 * raster reproduces the CPU line's width by construction.
	 */
	constexpr float HistogramLineThickness = TrueScope::Draw::HistogramLineThickness;

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
		// The count that fills a band exactly (M6.3), from
		// TrueScope::HistogramClipThresholdCount — the same scalar for every
		// curve, which is what makes the shared scale hold by construction.
		SHADER_PARAMETER(float, ClipThresholdCount)
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, LineThickness)
		SHADER_PARAMETER(FVector4f, FillColorR)
		SHADER_PARAMETER(FVector4f, FillColorG)
		SHADER_PARAMETER(FVector4f, FillColorB)
		SHADER_PARAMETER(FVector4f, FillColorY)
		SHADER_PARAMETER(FVector4f, LineColorR)
		SHADER_PARAMETER(FVector4f, LineColorG)
		SHADER_PARAMETER(FVector4f, LineColorB)
		SHADER_PARAMETER(FVector4f, LineColorY)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, StackedROut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, StackedGOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, StackedBOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OverlayOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, YrgbYOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, YrgbROut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, YrgbGOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, YrgbBOut)
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

	/** The YRGB band set (phase-6.2): four bands, luma scale participates. */
	class FTrueScopeHistogramDrawYrgbCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FTrueScopeHistogramDrawYrgbCS);
		SHADER_USE_PARAMETER_STRUCT(FTrueScopeHistogramDrawYrgbCS, FGlobalShader);
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
	IMPLEMENT_GLOBAL_SHADER(FTrueScopeHistogramDrawYrgbCS, "/Plugin/TrueScope/TrueScopeHistogram.usf",
		"HistogramDrawYrgbCS", SF_Compute);

	/**
	 * Creates one raster target. The size follows the owning widget's plot rect
	 * from the first CacheResources_GameThread on (capture is gated on the panel
	 * painting, so a widget has always published its rect before the first
	 * capture); it starts at the smallest allocation bucket rather than at a
	 * guessed resolution.
	 *
	 * PF_R8G8B8A8 (not the RTF_RGBA8 default PF_B8G8R8A8): typed-UAV stores on
	 * BGRA8 are not guaranteed on all RHIs, RGBA8 unorm is (M5 Task 0 finding).
	 * Linear gamma: raw intensity/coverage values, drawn with NoGamma.
	 */
	UTextureRenderTarget2D* MakeRasterTarget(const FLinearColor& ClearColor)
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
		Target->bCanCreateUAV = true;
		Target->ClearColor = ClearColor;
		Target->Filter = TF_Bilinear;
		Target->InitCustomFormat(TrueScope::MinRasterSide, TrueScope::MinRasterSide, PF_R8G8B8A8,
			/*bInForceLinearGamma*/ true);
		Target->UpdateResourceImmediate(/*bClearRenderTarget*/ true);
		return Target;
	}

	/** Fills the draw parameters shared by the histogram draw dispatches. */
	void FillHistogramDrawParameters(FTrueScopeHistogramDrawParameters& Parameters,
		FRDGBufferSRVRef HistogramSRV, float ClipThresholdCount, FIntPoint OutputSize)
	{
		Parameters.HistogramIn = HistogramSRV;
		Parameters.ClipThresholdCount = ClipThresholdCount;
		Parameters.OutputSize = OutputSize;
		Parameters.LineThickness = HistogramLineThickness;
		// The widget's exact curve colours (TrueScopeDraw.h), so both paths
		// draw pixel-identical colours by construction. Fills are passed as-is
		// (numerically what the widget's ToFColor(false) packing renders); the
		// line colours go through the shared Slate-vertex encoding — see
		// GetHistogramLineRasterColor for why the raw linear value would come
		// out darker than the CPU line.
		Parameters.FillColorR = FVector4f(TrueScope::Draw::HistogramFillColors[0]);
		Parameters.FillColorG = FVector4f(TrueScope::Draw::HistogramFillColors[1]);
		Parameters.FillColorB = FVector4f(TrueScope::Draw::HistogramFillColors[2]);
		Parameters.FillColorY = FVector4f(TrueScope::Draw::HistogramLumaFillColor);
		Parameters.LineColorR = FVector4f(TrueScope::Draw::GetHistogramLineRasterColor(0));
		Parameters.LineColorG = FVector4f(TrueScope::Draw::GetHistogramLineRasterColor(1));
		Parameters.LineColorB = FVector4f(TrueScope::Draw::GetHistogramLineRasterColor(2));
		Parameters.LineColorY = FVector4f(TrueScope::Draw::GetHistogramLumaLineRasterColor());
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
		// All ColumnBinChannels channels since phase-6.2: R, G, B and the luma
		// channel (the YRGB mode's fourth curve). Channels 0-2 keep exactly
		// their phase-5.2 layout and values.
		constexpr int32 NumCells = TrueScope::ColumnBinChannels * FTrueScopeAnalysisResult::NumBins;

		FHistogramReduceOutputs Outputs;
		Outputs.Bins = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumCells), TEXT("TrueScope.HistogramBins"));

		FTrueScopeHistogramReduceCS::FParameters* Parameters =
			GraphBuilder.AllocParameters<FTrueScopeHistogramReduceCS::FParameters>();
		Parameters->ColumnBinsIn = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ColumnBins, PF_R32_UINT));
		Parameters->SourceWidth = SourceWidth;
		Parameters->HistogramOut = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(Outputs.Bins, PF_R32_UINT));

		TShaderMapRef<FTrueScopeHistogramReduceCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(GraphBuilder,
			RDG_EVENT_NAME("TrueScope.HistogramReduce %d cells", NumCells),
			ComputeShader, Parameters,
			FIntVector(FMath::DivideAndRoundUp(NumCells, HistogramReduceGroupSize), 1, 1));

		return Outputs;
	}
}

FTrueScopeGpuAnalysis::FTrueScopeGpuAnalysis(const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& InAnalyzer,
	const TSharedPtr<FTrueScopeRasterSizes, ESPMode::ThreadSafe>& InRasterSizes,
	const TSharedPtr<FTrueScopeDisplayGains, ESPMode::ThreadSafe>& InDisplayGains)
	: Analyzer(InAnalyzer)
	, RasterSizes(InRasterSizes)
	, DisplayGains(InDisplayGains)
{
	// Density targets (parade channels, waveform traces, vectorscope): opaque
	// black behind the trace, like the CPU path's intensity textures.
	for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
	{
		ChannelTarget[Channel] = MakeRasterTarget(FLinearColor::Black);
		ChannelTargetWeak[Channel] = ChannelTarget[Channel];
	}

	VectorscopeTarget = MakeRasterTarget(FLinearColor::Black);
	VectorscopeTargetWeak = VectorscopeTarget;

	WaveformRgbTarget = MakeRasterTarget(FLinearColor::Black);
	WaveformLumaTarget = MakeRasterTarget(FLinearColor::Black);
	WaveformRgbTargetWeak = WaveformRgbTarget;
	WaveformLumaTargetWeak = WaveformLumaTarget;

	// Histogram curve targets (phase-5.2): coverage rasterizations carrying
	// straight (non-premultiplied) RGBA, drawn by Slate's standard blending.
	// Cleared to transparent so the pre-first-capture state draws nothing and
	// the gridlines stay visible through the plot.
	for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
	{
		HistogramStackedTarget[Channel] = MakeRasterTarget(FLinearColor::Transparent);
		HistogramStackedTargetWeak[Channel] = HistogramStackedTarget[Channel];
	}
	for (int32 Slot = 0; Slot < FTrueScopeAnalysisResult::NumYrgbCurves; ++Slot)
	{
		HistogramYrgbTarget[Slot] = MakeRasterTarget(FLinearColor::Transparent);
		HistogramYrgbTargetWeak[Slot] = HistogramYrgbTarget[Slot];
	}
	HistogramOverlayTarget = MakeRasterTarget(FLinearColor::Transparent);
	HistogramOverlayTargetWeak = HistogramOverlayTarget;
}

FIntPoint FTrueScopeGpuAnalysis::ApplyRasterSize(UTextureRenderTarget2D* Target, TrueScope::ERasterTarget Which) const
{
	check(IsInGameThread());

	const FIntPoint Current(Target->SizeX, Target->SizeY);
	const FIntPoint Requested = RasterSizes ? RasterSizes->GetRequested(Which) : FIntPoint::ZeroValue;
	if (Requested.X <= 0 || Requested.Y <= 0)
	{
		// No widget has painted yet: rasterize the target as it stands rather
		// than skip the scope — the next paint supplies the real rect.
		return Current;
	}

	// Allocate in buckets (so a splitter drag cannot reallocate the target every
	// mouse move) and rasterize the exact request inside it. The widget samples
	// only that region, so the bucket costs memory, never sharpness.
	const FIntPoint Allocation = TrueScope::BucketRasterSize(Requested);
	if (Allocation != Current)
	{
		Target->ResizeTarget(Allocation.X, Allocation.Y);
	}
	// Min(): a request beyond MaxRasterSide buckets down to the ceiling, and the
	// raster must never claim texels the target does not have.
	return FIntPoint(FMath::Min(Requested.X, Allocation.X), FMath::Min(Requested.Y, Allocation.Y));
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
	for (int32 Slot = 0; Slot < FTrueScopeAnalysisResult::NumYrgbCurves; ++Slot)
	{
		Collector.AddReferencedObject(HistogramYrgbTarget[Slot]);
	}
	Collector.AddReferencedObject(HistogramOverlayTarget);
}

void FTrueScopeGpuAnalysis::CacheResources_GameThread()
{
	// Size every target to its widget's current plot rect first: ResizeTarget
	// enqueues the RHI resize, and render commands run in order, so the passes
	// enqueued below always see the resized texture.
	FIntPoint ParadeSize = FIntPoint::ZeroValue;
	FTextureRenderTargetResource* Resources[FTrueScopeAnalysisResult::NumChannels] = {};
	for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
	{
		if (!ChannelTarget[Channel])
		{
			continue;
		}
		// The three channel sections are congruent, so they share one request.
		ParadeSize = ApplyRasterSize(ChannelTarget[Channel], TrueScope::ERasterTarget::ParadeSection);
		Resources[Channel] = ChannelTarget[Channel]->GameThread_GetRenderTargetResource();
	}

	FIntPoint VectorscopeSize = FIntPoint::ZeroValue;
	FTextureRenderTargetResource* VectorResource = nullptr;
	if (VectorscopeTarget)
	{
		VectorscopeSize = ApplyRasterSize(VectorscopeTarget, TrueScope::ERasterTarget::Vectorscope);
		VectorResource = VectorscopeTarget->GameThread_GetRenderTargetResource();
	}

	FIntPoint WaveformSize = FIntPoint::ZeroValue;
	FTextureRenderTargetResource* WaveformRgbResource = nullptr;
	FTextureRenderTargetResource* WaveformLumaResource = nullptr;
	if (WaveformRgbTarget && WaveformLumaTarget)
	{
		// Both traces fill the same plot rect; only the display mode picks one.
		WaveformSize = ApplyRasterSize(WaveformRgbTarget, TrueScope::ERasterTarget::Waveform);
		ApplyRasterSize(WaveformLumaTarget, TrueScope::ERasterTarget::Waveform);
		WaveformRgbResource = WaveformRgbTarget->GameThread_GetRenderTargetResource();
		WaveformLumaResource = WaveformLumaTarget->GameThread_GetRenderTargetResource();
	}

	FIntPoint HistogramStackedSize = FIntPoint::ZeroValue;
	FTextureRenderTargetResource* HistogramStackedResources[FTrueScopeAnalysisResult::NumChannels] = {};
	for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
	{
		if (!HistogramStackedTarget[Channel])
		{
			continue;
		}
		HistogramStackedSize =
			ApplyRasterSize(HistogramStackedTarget[Channel], TrueScope::ERasterTarget::HistogramBand);
		HistogramStackedResources[Channel] = HistogramStackedTarget[Channel]->GameThread_GetRenderTargetResource();
	}

	FIntPoint HistogramYrgbSize = FIntPoint::ZeroValue;
	FTextureRenderTargetResource* HistogramYrgbResources[FTrueScopeAnalysisResult::NumYrgbCurves] = {};
	for (int32 Slot = 0; Slot < FTrueScopeAnalysisResult::NumYrgbCurves; ++Slot)
	{
		if (!HistogramYrgbTarget[Slot])
		{
			continue;
		}
		// The four YRGB bands are congruent, so they share one request.
		HistogramYrgbSize = ApplyRasterSize(HistogramYrgbTarget[Slot], TrueScope::ERasterTarget::HistogramBandYrgb);
		HistogramYrgbResources[Slot] = HistogramYrgbTarget[Slot]->GameThread_GetRenderTargetResource();
	}

	FIntPoint HistogramOverlaySize = FIntPoint::ZeroValue;
	FTextureRenderTargetResource* HistogramOverlayResource = nullptr;
	if (HistogramOverlayTarget)
	{
		HistogramOverlaySize = ApplyRasterSize(HistogramOverlayTarget, TrueScope::ERasterTarget::HistogramOverlay);
		HistogramOverlayResource = HistogramOverlayTarget->GameThread_GetRenderTargetResource();
	}

	// The slider gains travel with the raster sizes, sampled once here on the
	// game thread: the render thread then reads plain floats, so one capture
	// cannot be rasterized with two different gains if a slider moves mid-frame.
	const float ParadeGain = DisplayGains
		? DisplayGains->GetEffectiveDensityGain(TrueScope::EDensityScope::Parade)
		: TrueScope::GetDefaultDensityGain(TrueScope::EDensityScope::Parade);
	const float WaveformGain = DisplayGains
		? DisplayGains->GetEffectiveDensityGain(TrueScope::EDensityScope::Waveform)
		: TrueScope::GetDefaultDensityGain(TrueScope::EDensityScope::Waveform);
	const float VectorscopeGain = DisplayGains
		? DisplayGains->GetEffectiveDensityGain(TrueScope::EDensityScope::Vectorscope)
		: TrueScope::GetDefaultDensityGain(TrueScope::EDensityScope::Vectorscope);
	const float HistogramGain = DisplayGains ? DisplayGains->GetHistogramGain() : TrueScope::DefaultGainMultiplier;

	ENQUEUE_RENDER_COMMAND(TrueScopeCacheAnalysisResources)(
		[WeakThis = AsWeak(), Resources, VectorResource, WaveformRgbResource, WaveformLumaResource,
			HistogramStackedResources, HistogramYrgbResources, HistogramOverlayResource, ParadeSize, WaveformSize,
			VectorscopeSize, HistogramStackedSize, HistogramYrgbSize, HistogramOverlaySize, ParadeGain, WaveformGain,
			VectorscopeGain, HistogramGain](FRHICommandListImmediate&)
		{
			if (TSharedPtr<FTrueScopeGpuAnalysis> This =
				StaticCastWeakPtr<FTrueScopeGpuAnalysis>(WeakThis).Pin())
			{
				for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
				{
					This->ChannelResourceRenderThread[Channel] = Resources[Channel];
					This->HistogramStackedResourceRenderThread[Channel] = HistogramStackedResources[Channel];
				}
				for (int32 Slot = 0; Slot < FTrueScopeAnalysisResult::NumYrgbCurves; ++Slot)
				{
					This->HistogramYrgbResourceRenderThread[Slot] = HistogramYrgbResources[Slot];
				}
				This->VectorscopeResourceRenderThread = VectorResource;
				This->WaveformRgbResourceRenderThread = WaveformRgbResource;
				This->WaveformLumaResourceRenderThread = WaveformLumaResource;
				This->HistogramOverlayResourceRenderThread = HistogramOverlayResource;

				This->ParadeRasterSizeRenderThread = ParadeSize;
				This->WaveformRasterSizeRenderThread = WaveformSize;
				This->VectorscopeRasterSizeRenderThread = VectorscopeSize;
				This->HistogramStackedRasterSizeRenderThread = HistogramStackedSize;
				This->HistogramYrgbRasterSizeRenderThread = HistogramYrgbSize;
				This->HistogramOverlayRasterSizeRenderThread = HistogramOverlaySize;

				This->ParadeGainRenderThread = ParadeGain;
				This->WaveformGainRenderThread = WaveformGain;
				This->VectorscopeGainRenderThread = VectorscopeGain;
				This->HistogramGainRenderThread = HistogramGain;
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
	FRHITexture* HistogramYrgbTextures[FTrueScopeAnalysisResult::NumYrgbCurves] = {};
	bool bHaveAllYrgbTextures = true;
	for (int32 Slot = 0; Slot < FTrueScopeAnalysisResult::NumYrgbCurves; ++Slot)
	{
		HistogramYrgbTextures[Slot] = HistogramYrgbResourceRenderThread[Slot]
			? HistogramYrgbResourceRenderThread[Slot]->GetRenderTargetTexture()
			: nullptr;
		bHaveAllYrgbTextures &= HistogramYrgbTextures[Slot] != nullptr;
	}
	FRHITexture* HistogramOverlayTexture =
		HistogramOverlayResourceRenderThread ? HistogramOverlayResourceRenderThread->GetRenderTargetTexture() : nullptr;
	if (!VectorscopeTexture || !WaveformRgbTexture || !WaveformLumaTexture || !HistogramStackedTextures[0]
		|| !HistogramStackedTextures[1] || !HistogramStackedTextures[2] || !bHaveAllYrgbTextures
		|| !HistogramOverlayTexture)
	{
		return; // all-or-nothing, same as the parade targets above
	}

	const FIntPoint InputSize(CaptureTexture->Desc.Extent.X, CaptureTexture->Desc.Extent.Y);
	const FIntPoint BinGridSize(InputSize.X, FTrueScopeAnalysisResult::NumBins);

	// The rects the widgets draw these rasters into, in device pixels
	// (phase-6.1). All five are non-zero: a target with no request keeps its
	// current size, so the passes below always have somewhere to write.
	const FIntPoint ParadeSize = ParadeRasterSizeRenderThread;
	const FIntPoint WaveformSize = WaveformRasterSizeRenderThread;
	const FIntPoint VectorscopeSize = VectorscopeRasterSizeRenderThread;
	const FIntPoint HistogramStackedSize = HistogramStackedRasterSizeRenderThread;
	const FIntPoint HistogramYrgbSize = HistogramYrgbRasterSizeRenderThread;
	const FIntPoint HistogramOverlaySize = HistogramOverlayRasterSizeRenderThread;
	if (ParadeSize.X <= 0 || WaveformSize.X <= 0 || VectorscopeSize.X <= 0 || HistogramStackedSize.X <= 0
		|| HistogramYrgbSize.X <= 0 || HistogramOverlaySize.X <= 0)
	{
		return; // sizes not cached yet; the next capture will have them
	}

	// Pass 1: pixel values -> per-channel column x bin counts (R, G, B, luma).
	FRDGBufferRef Bins = TrueScope::AddColumnBinningPass(GraphBuilder, CaptureTexture, InputSize);

	// Pass 2: counts -> the three single-channel parade density textures and the
	// two waveform traces (linear count -> brightness mapping, hard-clipped at
	// white, same formula as the CPU path). Parade and waveform live in
	// different panels, so each rasterizes into its own plot rect and they are
	// separate dispatches — and since M6.3 they carry their own Trace
	// Brightness gain, hence the per-dispatch gain argument.
	FRDGBufferSRVRef BinsSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(Bins, PF_R32_UINT));
	auto MakeColumnIntensityParameters = [&](FIntPoint OutputSize, float IntensityGain)
	{
		FTrueScopeColumnIntensityParameters* Parameters =
			GraphBuilder.AllocParameters<FTrueScopeColumnIntensityParameters>();
		Parameters->ColumnBinsIn = BinsSRV;
		Parameters->OutputSize = OutputSize;
		Parameters->SourceWidth = BinGridSize.X;
		Parameters->MaxCount = static_cast<uint32>(InputSize.Y);
		Parameters->IntensityGain = IntensityGain;
		return Parameters;
	};

	{
		FTrueScopeColumnIntensityParameters* Parameters =
			MakeColumnIntensityParameters(ParadeSize, ParadeGainRenderThread);

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

		TShaderMapRef<FTrueScopeColumnIntensityParadeCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(GraphBuilder,
			RDG_EVENT_NAME("TrueScope.ParadeIntensity %dx%d", ParadeSize.X, ParadeSize.Y),
			ComputeShader, Parameters,
			FComputeShaderUtils::GetGroupCount(ParadeSize, ThreadGroupSize));
	}

	{
		FTrueScopeColumnIntensityParameters* Parameters =
			MakeColumnIntensityParameters(WaveformSize, WaveformGainRenderThread);
		Parameters->WaveformRgbOut = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(
			RegisterExternalTexture(GraphBuilder, WaveformRgbTexture, TEXT("TrueScope.WaveformRgb"))));
		Parameters->WaveformLumaOut = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(
			RegisterExternalTexture(GraphBuilder, WaveformLumaTexture, TEXT("TrueScope.WaveformLuma"))));

		TShaderMapRef<FTrueScopeColumnIntensityWaveformCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(GraphBuilder,
			RDG_EVENT_NAME("TrueScope.WaveformIntensity %dx%d", WaveformSize.X, WaveformSize.Y),
			ComputeShader, Parameters,
			FComputeShaderUtils::GetGroupCount(WaveformSize, ThreadGroupSize));
	}

	// Vectorscope (phase-5.1): binning pass into the (Cb, Cr) grid, then the
	// intensity pass into the vectorscope density target.
	{
		FRDGBufferRef VectorBins = TrueScope::AddVectorscopeBinningPass(GraphBuilder, CaptureTexture, InputSize);

		FTrueScopeVectorIntensityCS::FParameters* VectorParameters =
			GraphBuilder.AllocParameters<FTrueScopeVectorIntensityCS::FParameters>();
		VectorParameters->VectorBinsIn = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(VectorBins, PF_R32_UINT));
		VectorParameters->OutputSize = VectorscopeSize;
		// Max possible count in one (Cb, Cr) cell: the whole frame on one chroma
		// coordinate — the CPU path's normalization (BuildVectorscopeImage).
		VectorParameters->MaxCount = static_cast<uint32>(InputSize.X * InputSize.Y);
		VectorParameters->IntensityGain = VectorscopeGainRenderThread;

		FRDGTextureRef VectorTarget =
			RegisterExternalTexture(GraphBuilder, VectorscopeTexture, TEXT("TrueScope.Vectorscope"));
		VectorParameters->VectorscopeOut = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(VectorTarget));

		TShaderMapRef<FTrueScopeVectorIntensityCS> VectorComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(GraphBuilder,
			RDG_EVENT_NAME("TrueScope.VectorIntensity %dx%d", VectorscopeSize.X, VectorscopeSize.Y),
			VectorComputeShader, VectorParameters,
			FComputeShaderUtils::GetGroupCount(VectorscopeSize, ThreadGroupSize));
	}

	// Histogram (phase-5.2): reduce the column bins into the per-channel 1D
	// histogram, then rasterize the curve textures — count -> curve HEIGHT,
	// linear against the FIXED clip threshold since M6.3 (CountToCurveHeight),
	// never the density mapping above.
	{
		const TrueScope::FHistogramReduceOutputs Histogram =
			TrueScope::AddHistogramReducePass(GraphBuilder, Bins, InputSize.X);
		FRDGBufferSRVRef HistogramSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(Histogram.Bins, PF_R32_UINT));

		// The one scale every curve is drawn against, from the analysed
		// frame's pixel count and the Histogram Gain — the same computation
		// STrueScopeHistogram::GetClipThresholdCount runs on the CPU path.
		const float ClipThresholdCount = TrueScope::HistogramClipThresholdCount(
			static_cast<int64>(InputSize.X) * static_cast<int64>(InputSize.Y), HistogramGainRenderThread);

		// Stacked (Separate Channels RGB) set: one texture per channel.
		{
			FTrueScopeHistogramDrawParameters* DrawParameters =
				GraphBuilder.AllocParameters<FTrueScopeHistogramDrawParameters>();
			FillHistogramDrawParameters(*DrawParameters, HistogramSRV, ClipThresholdCount, HistogramStackedSize);
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

		// YRGB band set (phase-6.2): four textures, display order Y, R, G, B.
		// Luma and the colour channels share the RGB modes' scale because the
		// clip threshold is fixed — no separate YRGB maximum since M6.3.
		{
			FTrueScopeHistogramDrawParameters* DrawParameters =
				GraphBuilder.AllocParameters<FTrueScopeHistogramDrawParameters>();
			FillHistogramDrawParameters(*DrawParameters, HistogramSRV, ClipThresholdCount, HistogramYrgbSize);
			static const TCHAR* YrgbNames[FTrueScopeAnalysisResult::NumYrgbCurves] = {
				TEXT("TrueScope.HistogramYrgbY"), TEXT("TrueScope.HistogramYrgbR"),
				TEXT("TrueScope.HistogramYrgbG"), TEXT("TrueScope.HistogramYrgbB")};
			FRDGTextureUAVRef YrgbUAV[FTrueScopeAnalysisResult::NumYrgbCurves];
			for (int32 Slot = 0; Slot < FTrueScopeAnalysisResult::NumYrgbCurves; ++Slot)
			{
				YrgbUAV[Slot] = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RegisterExternalTexture(
					GraphBuilder, HistogramYrgbTextures[Slot], YrgbNames[Slot])));
			}
			DrawParameters->YrgbYOut = YrgbUAV[0];
			DrawParameters->YrgbROut = YrgbUAV[1];
			DrawParameters->YrgbGOut = YrgbUAV[2];
			DrawParameters->YrgbBOut = YrgbUAV[3];

			TShaderMapRef<FTrueScopeHistogramDrawYrgbCS> YrgbShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FComputeShaderUtils::AddPass(GraphBuilder,
				RDG_EVENT_NAME("TrueScope.HistogramDrawYrgb %dx%d", HistogramYrgbSize.X, HistogramYrgbSize.Y),
				YrgbShader, DrawParameters,
				FComputeShaderUtils::GetGroupCount(HistogramYrgbSize, ThreadGroupSize));
		}

		// Overlay set: all three channels composited into one texture.
		{
			FTrueScopeHistogramDrawParameters* DrawParameters =
				GraphBuilder.AllocParameters<FTrueScopeHistogramDrawParameters>();
			FillHistogramDrawParameters(*DrawParameters, HistogramSRV, ClipThresholdCount, HistogramOverlaySize);
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
	// no CPU copy of anything on this path, by design. The raster sizes tell
	// each widget which region of its (bucket-allocated) target this capture
	// actually filled.
	TSharedRef<FTrueScopeAnalysisResult, ESPMode::ThreadSafe> Result =
		MakeShared<FTrueScopeAnalysisResult, ESPMode::ThreadSafe>();
	Result->SourceSize = InputSize;
	Result->ParadeImageSize = BinGridSize;
	Result->ParadeRasterSize = ParadeSize;
	Result->WaveformRasterSize = WaveformSize;
	Result->VectorscopeRasterSize = VectorscopeSize;
	Result->HistogramStackedRasterSize = HistogramStackedSize;
	Result->HistogramYrgbRasterSize = HistogramYrgbSize;
	Result->HistogramOverlayRasterSize = HistogramOverlaySize;
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
	for (int32 Slot = 0; Slot < FTrueScopeAnalysisResult::NumYrgbCurves; ++Slot)
	{
		Result->HistogramYrgbTarget[Slot] = HistogramYrgbTargetWeak[Slot];
	}
	Result->HistogramOverlayTarget = HistogramOverlayTargetWeak;

	if (Analyzer)
	{
		Analyzer->PublishResult(Result);
	}
}
