// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "UI/STrueScopeWaveform.h"

#include "Analysis/TrueScopeAnalysis.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Rendering/DrawElements.h"
#include "UI/TrueScopeDraw.h"

#define LOCTEXT_NAMESPACE "STrueScopeWaveform"

void STrueScopeWaveform::Construct(const FArguments& InArgs,
	const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& InAnalyzer)
{
	TraceBrush.DrawAs = ESlateBrushDrawType::Image;
	TraceBrush.Tiling = ESlateBrushTileType::NoTile;
	InitializeScopeWidget(InAnalyzer, /*bMaintainSquareAspect*/ false);
}

int32 STrueScopeWaveform::GetTexelCount(const FTrueScopeAnalysisResult& Result)
{
	return Result.ParadeImageSize.X * Result.ParadeImageSize.Y;
}

void STrueScopeWaveform::BuildTexels(const FTrueScopeAnalysisResult& Result, ETrueScopeWaveformMode InMode,
	TArrayView<FColor> OutTexels)
{
	switch (InMode)
	{
	case ETrueScopeWaveformMode::Luma:
		TrueScope::BuildLumaWaveformTexels(Result, OutTexels);
		break;
	default:
		TrueScope::BuildRgbWaveformTexels(Result, OutTexels);
		break;
	}
}

TArrayView<const FTrueScopeDisplayMode> STrueScopeWaveform::GetDisplayModes() const
{
	// Function-local: LOCTEXT must not run before localization is initialized.
	static const TArray<FTrueScopeDisplayMode> Modes = {
		FTrueScopeDisplayMode{static_cast<int32>(ETrueScopeWaveformMode::Rgb), LOCTEXT("Rgb", "RGB")},
		FTrueScopeDisplayMode{static_cast<int32>(ETrueScopeWaveformMode::Luma), LOCTEXT("Luma", "Luma")},
	};
	return Modes;
}

int32 STrueScopeWaveform::GetDisplayModeId() const
{
	return static_cast<int32>(Mode);
}

void STrueScopeWaveform::SetDisplayModeId(int32 InModeId)
{
	Mode = static_cast<ETrueScopeWaveformMode>(InModeId);

	// The trace lives in a texture, so a mode switch has to re-upload it rather
	// than wait for the next analysis result (~2.5 Hz away).
	UpdateTraceTexture();
}

void STrueScopeWaveform::OnAnalysisResultChanged()
{
	UpdateTraceTexture();
}

void STrueScopeWaveform::UpdateTraceTexture()
{
	const TSharedPtr<const FTrueScopeAnalysisResult, ESPMode::ThreadSafe>& Result = GetCachedResult();
	if (!Result.IsValid())
	{
		return;
	}
	const FIntPoint Size = Result->ParadeImageSize;
	const int32 NumTexels = GetTexelCount(*Result);

	// GPU path (phase-5.2): the result carries the producer's trace render
	// targets; point the brush at the one matching the display mode — no
	// texture build, no upload, no readback (same pattern as the parade
	// widget). A mode switch re-enters here and just swaps the brush.
	if (Result->bHasGpuWaveform)
	{
		UTextureRenderTarget2D* Target = Mode == ETrueScopeWaveformMode::Luma
			? Result->WaveformLumaTarget.Get()
			: Result->WaveformRgbTarget.Get();
		if (Target && TraceBrush.GetResourceObject() != Target)
		{
			TraceBrush.SetResourceObject(Target);
			TraceBrush.ImageSize = FVector2f(Size.X, Size.Y);
			TraceTexture.Reset(); // CPU-path texture no longer drawn
		}
		return;
	}

	// Both intensity images come out of the same analysis pass; if either is
	// missing, keep the last good trace rather than upload a half-built one.
	if (NumTexels <= 0 || Result->LumaIntensity.Num() != NumTexels
		|| Result->ParadeChannelIntensity[FTrueScopeAnalysisResult::ChannelR].Num() != NumTexels)
	{
		return;
	}

	if (!TraceTexture.IsValid() || TraceTexture->GetSizeX() != Size.X || TraceTexture->GetSizeY() != Size.Y)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(Size.X, Size.Y, PF_B8G8R8A8);
		// Raw intensity values: no sRGB decode on sampling (drawn with NoGamma,
		// so the computed display mapping arrives 1:1 on screen).
		Texture->SRGB = false;
		Texture->Filter = TF_Bilinear;
		Texture->UpdateResource();
		TraceTexture.Reset(Texture);
		TraceBrush.SetResourceObject(Texture);
		TraceBrush.ImageSize = FVector2f(Size.X, Size.Y);
	}

	// Private BGRA buffer, freed by the async-upload cleanup once the render
	// thread has consumed it.
	FColor* Texels = static_cast<FColor*>(FMemory::Malloc(NumTexels * sizeof(FColor)));
	BuildTexels(*Result, Mode, TArrayView<FColor>(Texels, NumTexels));

	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Size.X, Size.Y);
	TraceTexture->UpdateTextureRegions(0, 1, Region, Size.X * sizeof(FColor), sizeof(FColor),
		reinterpret_cast<uint8*>(Texels),
		[](uint8* SrcData, const FUpdateTextureRegion2D* InRegion)
		{
			FMemory::Free(SrcData);
			delete InRegion;
		});
}

int32 STrueScopeWaveform::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace TrueScope::Draw;

	const FSlateRect Content = GetScopeContentRect(AllottedGeometry);
	LayerId = DrawBackground(OutDrawElements, LayerId, AllottedGeometry, Content);

	// Plot area: fixed-size left label strip and padding.
	const FSlateRect Plot(Content.Left + ValueLabelStripWidth, Content.Top + PlotPadding,
		Content.Right - PlotPadding, Content.Bottom - PlotPadding);
	if (Plot.GetSize().X < 32.0f || Plot.GetSize().Y < 16.0f)
	{
		return DrawPausedHint(OutDrawElements, LayerId, AllottedGeometry, Content);
	}

	// The trace already carries its colour per texel; a white tint passes it
	// through unchanged. The brush holds whichever producer's texture is
	// current: the CPU path's transient texture or the GPU path's render
	// target (phase-5.2).
	if (TraceBrush.GetResourceObject())
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(FVector2f(Plot.GetSize()), FSlateLayoutTransform(FVector2f(Plot.GetTopLeft()))),
			&TraceBrush, ESlateDrawEffect::NoGamma, FLinearColor::White);
	}
	++LayerId;

	// Graticule + fixed-size 10-bit scale labels, every 128 code values.
	LayerId = DrawValueAxisY(OutDrawElements, LayerId, AllottedGeometry, Plot, Content, GetWaveformGraticuleLevels());

	return DrawPausedHint(OutDrawElements, LayerId, AllottedGeometry, Content);
}

#undef LOCTEXT_NAMESPACE
