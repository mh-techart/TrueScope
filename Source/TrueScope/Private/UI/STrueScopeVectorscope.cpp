// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "UI/STrueScopeVectorscope.h"

#include "Analysis/TrueScopeColor.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "UI/TrueScopeDraw.h"

namespace
{
	/** Fixed-size margin inside the square, leaving room for the target labels. */
	constexpr float GraticulePadding = 14.0f;

	/** Below this the graticule stops being readable; draw background only. */
	constexpr float MinPlotSide = 48.0f;

	/** Fixed-size graticule chrome, independent of the scope's scale. */
	constexpr float TargetBoxHalfSize = 5.0f;
	constexpr float TargetLabelOffset = 9.0f;
	constexpr int32 CircleSegments = 96;

	const FLinearColor TargetColor(0.75f, 0.75f, 0.75f, 0.55f);

	/**
	 * Chroma (Cb, Cr) -> local widget space. Both axes span [-0.5, 0.5], the
	 * tight bounding box of the BT.709 gamut, so the plot square maps exactly
	 * one chroma unit per side and the inscribed circle has radius 0.5.
	 */
	FVector2f ChromaToLocal(const TrueScope::FChroma& Chroma, const FVector2f& Center, float PlotSide)
	{
		return FVector2f(Center.X + Chroma.Cb * PlotSide, Center.Y - Chroma.Cr * PlotSide);
	}

	void DrawPolyline(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& AllottedGeometry,
		const TArray<FVector2f>& Points, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(),
			Points, ESlateDrawEffect::None, Color, /*bAntialias*/ true, 1.0f);
	}
}

void STrueScopeVectorscope::Construct(const FArguments& InArgs,
	const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& InAnalyzer)
{
	TraceBrush.DrawAs = ESlateBrushDrawType::Image;
	TraceBrush.Tiling = ESlateBrushTileType::NoTile;
	InitializeScopeWidget(InAnalyzer, /*bMaintainSquareAspect*/ true);
}

void STrueScopeVectorscope::OnAnalysisResultChanged()
{
	constexpr int32 NumBins = FTrueScopeAnalysisResult::NumVectorBins;
	constexpr int32 NumTexels = NumBins * NumBins;

	const TSharedPtr<const FTrueScopeAnalysisResult, ESPMode::ThreadSafe>& Result = GetCachedResult();
	if (!Result.IsValid())
	{
		return;
	}

	// GPU path (phase-5.1): the result carries the producer's density render
	// target; point the brush at it — no texture build, no upload, no readback
	// (same pattern as the parade widget).
	if (Result->bHasGpuVectorscope)
	{
		UTextureRenderTarget2D* Target = Result->VectorscopeTarget.Get();
		if (Target && TraceBrush.GetResourceObject() != Target)
		{
			TraceBrush.SetResourceObject(Target);
			TraceBrush.ImageSize = FVector2f(NumBins, NumBins);
			TraceTexture.Reset(); // CPU-path texture no longer drawn
		}
		return;
	}

	if (Result->VectorscopeIntensity.Num() != NumTexels)
	{
		return;
	}

	if (!TraceTexture.IsValid())
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(NumBins, NumBins, PF_B8G8R8A8);
		// Raw intensity values: no sRGB decode on sampling (drawn with NoGamma,
		// so the computed display mapping arrives 1:1 on screen).
		Texture->SRGB = false;
		Texture->Filter = TF_Bilinear;
		Texture->UpdateResource();
		TraceTexture.Reset(Texture);
		TraceBrush.SetResourceObject(Texture);
		TraceBrush.ImageSize = FVector2f(NumBins, NumBins);
	}

	// White texels carrying the density in alpha, so empty chroma coordinates
	// stay transparent and the graticule shows through instead of a black
	// square. Freed by the async-upload cleanup.
	FColor* Texels = static_cast<FColor*>(FMemory::Malloc(NumTexels * sizeof(FColor)));
	const uint8* Intensity = Result->VectorscopeIntensity.GetData();
	for (int32 Index = 0; Index < NumTexels; ++Index)
	{
		Texels[Index] = FColor(255, 255, 255, Intensity[Index]);
	}

	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, NumBins, NumBins);
	TraceTexture->UpdateTextureRegions(0, 1, Region, NumBins * sizeof(FColor), sizeof(FColor),
		reinterpret_cast<uint8*>(Texels),
		[](uint8* SrcData, const FUpdateTextureRegion2D* InRegion)
		{
			FMemory::Free(SrcData);
			delete InRegion;
		});
}

bool STrueScopeVectorscope::HasDrawableData() const
{
	const TSharedPtr<const FTrueScopeAnalysisResult, ESPMode::ThreadSafe>& Result = GetCachedResult();
	if (!Result.IsValid())
	{
		return false;
	}
	return Result->bHasGpuVectorscope
		|| Result->VectorscopeIntensity.Num()
			== FTrueScopeAnalysisResult::NumVectorBins * FTrueScopeAnalysisResult::NumVectorBins;
}

int32 STrueScopeVectorscope::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace TrueScope::Draw;

	// Background fills the whole panel; the graphic only ever uses the centered
	// square, so wide or narrow panels letterbox instead of distorting.
	const FVector2f PanelSize = AllottedGeometry.GetLocalSize();
	LayerId = DrawBackground(OutDrawElements, LayerId, AllottedGeometry,
		FSlateRect(0.0f, 0.0f, PanelSize.X, PanelSize.Y));

	const FSlateRect Content = GetScopeContentRect(AllottedGeometry);
	const float PlotSide = Content.GetSize().X - 2.0f * GraticulePadding;
	if (PlotSide < MinPlotSide)
	{
		return DrawPausedHint(OutDrawElements, LayerId, AllottedGeometry, Content);
	}

	const FVector2f Center(Content.Left + Content.GetSize().X * 0.5f, Content.Top + Content.GetSize().Y * 0.5f);
	const float Radius = PlotSide * 0.5f;

	// Density plot: the whole [-0.5, 0.5] chroma square, so the out-of-circle
	// corners (100 % green, magenta) remain visible instead of being clipped.
	// The brush holds whichever producer's texture is current: the CPU path's
	// transient texture or the GPU path's render target.
	if (TraceBrush.GetResourceObject())
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(FVector2f(PlotSide, PlotSide),
				FSlateLayoutTransform(FVector2f(Center.X - Radius, Center.Y - Radius))),
			&TraceBrush, ESlateDrawEffect::NoGamma, FLinearColor::White);
	}
	++LayerId;

	// Graticule circle: 100 % chroma amplitude, the tight bound of Cb and Cr.
	{
		TArray<FVector2f> Circle;
		Circle.Reserve(CircleSegments + 1);
		for (int32 Segment = 0; Segment <= CircleSegments; ++Segment)
		{
			const float Angle = 2.0f * UE_PI * Segment / CircleSegments;
			Circle.Add(FVector2f(Center.X + Radius * FMath::Cos(Angle), Center.Y + Radius * FMath::Sin(Angle)));
		}
		DrawPolyline(OutDrawElements, LayerId, AllottedGeometry, Circle, GraticuleColor);

		DrawPolyline(OutDrawElements, LayerId, AllottedGeometry,
			{FVector2f(Center.X - Radius, Center.Y), FVector2f(Center.X + Radius, Center.Y)}, GraticuleColor);
		DrawPolyline(OutDrawElements, LayerId, AllottedGeometry,
			{FVector2f(Center.X, Center.Y - Radius), FVector2f(Center.X, Center.Y + Radius)}, GraticuleColor);
	}
	++LayerId;

	// The six 75 % targets: fixed-size boxes at their BT.709 positions, each
	// labelled just outside the box, radially away from center.
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FSlateFontInfo LabelFont = GetLabelFont();

	for (const TrueScope::FVectorscopeTarget& Target : TrueScope::GetVectorscopeTargets())
	{
		const FVector2f Position = ChromaToLocal(Target.Chroma, Center, PlotSide);

		const float Left = Position.X - TargetBoxHalfSize;
		const float Right = Position.X + TargetBoxHalfSize;
		const float Top = Position.Y - TargetBoxHalfSize;
		const float Bottom = Position.Y + TargetBoxHalfSize;
		DrawPolyline(OutDrawElements, LayerId, AllottedGeometry,
			{FVector2f(Left, Top), FVector2f(Right, Top), FVector2f(Right, Bottom), FVector2f(Left, Bottom),
				FVector2f(Left, Top)},
			TargetColor);

		FVector2f Direction = Position - Center;
		Direction.Normalize();

		const FString LabelString(Target.Label);
		const FVector2f LabelSize = FVector2f(FontMeasure->Measure(LabelString, LabelFont));
		const FVector2f LabelCenter = Position + Direction * (TargetBoxHalfSize + TargetLabelOffset);
		FSlateDrawElement::MakeText(OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(LabelSize, FSlateLayoutTransform(LabelCenter - LabelSize * 0.5f)),
			LabelString, LabelFont, ESlateDrawEffect::None, LabelColor);
	}
	++LayerId;

	return DrawScopeHints(OutDrawElements, LayerId, AllottedGeometry, Content);
}
