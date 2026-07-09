// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "UI/STrueScopeWaveform.h"

#include "Analysis/TrueScopeAnalysis.h"
#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"
#include "UI/TrueScopeDraw.h"

void STrueScopeWaveform::Construct(const FArguments& InArgs,
	const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& InAnalyzer)
{
	TraceBrush.DrawAs = ESlateBrushDrawType::Image;
	TraceBrush.Tiling = ESlateBrushTileType::NoTile;
	InitializeScopeWidget(InAnalyzer, /*bMaintainSquareAspect*/ false);
}

void STrueScopeWaveform::OnAnalysisResultChanged()
{
	const TSharedPtr<const FTrueScopeAnalysisResult, ESPMode::ThreadSafe>& Result = GetCachedResult();
	if (!Result.IsValid() || Result->ParadeImageSize.X <= 0)
	{
		return;
	}
	const FIntPoint Size = Result->ParadeImageSize;
	const int32 NumTexels = Size.X * Size.Y;

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
	TrueScope::BuildWaveformTexels(*Result, TArrayView<FColor>(Texels, NumTexels));

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
		return LayerId;
	}

	// The trace already carries its colour per texel; a white tint passes it
	// through unchanged.
	if (TraceTexture.IsValid())
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(FVector2f(Plot.GetSize()), FSlateLayoutTransform(FVector2f(Plot.GetTopLeft()))),
			&TraceBrush, ESlateDrawEffect::NoGamma, FLinearColor::White);
	}
	++LayerId;

	// Graticule + fixed-size 10-bit scale labels, every 128 code values.
	LayerId = DrawValueAxisY(OutDrawElements, LayerId, AllottedGeometry, Plot, Content, GetWaveformGraticuleLevels());

	return LayerId;
}
