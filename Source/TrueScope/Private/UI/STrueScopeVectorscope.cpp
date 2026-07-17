// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "UI/STrueScopeVectorscope.h"

#include "Analysis/TrueScopeColor.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "UI/TrueScopeDraw.h"

#define LOCTEXT_NAMESPACE "STrueScopeVectorscope"

namespace
{
	/** Fixed-size margin inside the square, leaving room for the target labels. */
	constexpr float GraticulePadding = 14.0f;

	/** Fixed-size graticule chrome, independent of the scope's scale. */
	constexpr float TargetBoxHalfSize = 5.0f;
	constexpr float TargetLabelOffset = 9.0f;
	constexpr int32 CircleSegments = 96;

	const FLinearColor TargetColor(0.75f, 0.75f, 0.75f, 0.55f);

	// --- Show Guides extensions (phase-6.2 Task 1) --------------------------

	/**
	 * Skin tone line: neutral white as Resolve draws it, translucent enough not
	 * to compete with the density trace.
	 */
	const FLinearColor SkinToneLineColor(0.85f, 0.85f, 0.85f, 0.6f);

	/** 5-degree ticks: quiet gray, brighter than the gridlines but chrome-like. */
	const FLinearColor TickColor(0.6f, 0.6f, 0.6f, 0.4f);

	/**
	 * Tick geometry, measured from Resolve (phase-6.2 spec): every 5 degrees,
	 * drawn INWARD from the circle, alternating long/short so they are
	 * countable — long ~2.7 % of the radius, short ~1.5 %.
	 */
	constexpr int32 NumTicks = 72;
	constexpr float LongTickFraction = 0.027f;
	constexpr float ShortTickFraction = 0.015f;

	/**
	 * Hue ring sizing: about half a 75 % target box at the default panel size,
	 * expressed as a fraction of the radius with a pixel min/max so it neither
	 * vanishes on small panels nor balloons on large ones (value by eye).
	 */
	constexpr float HueRingWidthFraction = 0.03f;
	constexpr float HueRingWidthMin = 3.0f;
	constexpr float HueRingWidthMax = 8.0f;
	constexpr int32 HueRingSteps = 180;

	/**
	 * Muting of the ring's fully saturated hues: a full-saturation ring is
	 * visually loud next to the density trace on the dark editor theme, so the
	 * colours are scaled down and slightly translucent (values by eye).
	 */
	constexpr float HueRingBrightness = 0.72f;
	constexpr float HueRingAlpha = 0.85f;

	/** Graticule angle (CCW from +Cb, +Cr up) -> local-space direction. */
	FVector2f AngleToDirection(float AngleDegrees)
	{
		const float Radians = FMath::DegreesToRadians(AngleDegrees);
		// Local space has y down, so +Cr (up) maps to -y.
		return FVector2f(FMath::Cos(Radians), -FMath::Sin(Radians));
	}

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

const FName STrueScopeVectorscope::ShowGuidesKey(TEXT("ShowGuides"));

void STrueScopeVectorscope::Construct(const FArguments& InArgs,
	const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& InAnalyzer)
{
	TraceBrush.DrawAs = ESlateBrushDrawType::Image;
	TraceBrush.Tiling = ESlateBrushTileType::NoTile;
	InitializeScopeWidget(InAnalyzer, ETrueScopeType::Vectorscope, /*bMaintainSquareAspect*/ true);
}

TArrayView<const FTrueScopeDisplayMode> STrueScopeVectorscope::GetDisplayModes() const
{
	// Function-local: LOCTEXT must not run before localization is initialized.
	static const TArray<FTrueScopeDisplayMode> Modes = {
		FTrueScopeDisplayMode{static_cast<int32>(ETrueScopeVectorscopeMode::Normal), LOCTEXT("Normal", "Normal")},
		// × is the multiplication sign, ° (below) the degree sign —
		// escapes so the source stays ASCII (same rationale as the hint texts).
		FTrueScopeDisplayMode{static_cast<int32>(ETrueScopeVectorscopeMode::Zoom2x), LOCTEXT("Zoom2x", "2\u00d7 Zoom")},
	};
	return Modes;
}

int32 STrueScopeVectorscope::GetDisplayModeId() const
{
	return static_cast<int32>(Mode);
}

void STrueScopeVectorscope::SetDisplayModeId(int32 InModeId)
{
	Mode = static_cast<ETrueScopeVectorscopeMode>(InModeId);
}

TArrayView<const FTrueScopeToggleOption> STrueScopeVectorscope::GetToggleOptions() const
{
	static const TArray<FTrueScopeToggleOption> Toggles = {
		FTrueScopeToggleOption{ShowGuidesKey, LOCTEXT("ShowGuides", "Show Guides"),
			LOCTEXT("ShowGuidesTooltip",
				"Show the graticule extensions: skin tone line, hue ring, 5\u00b0 ticks and 100% targets.")},
	};
	return Toggles;
}

bool STrueScopeVectorscope::GetToggleOptionValue(FName Key) const
{
	return Key == ShowGuidesKey ? bShowGuides : false;
}

void STrueScopeVectorscope::SetToggleOptionValue(FName Key, bool bValue)
{
	if (Key == ShowGuidesKey)
	{
		bShowGuides = bValue;
	}
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
	// (same pattern as the parade widget). The raster fills the square plot rect
	// this widget asked for in device pixels (phase-6.1).
	if (Result->bHasGpuVectorscope)
	{
		UTextureRenderTarget2D* Target = Result->VectorscopeTarget.Get();
		TrueScope::Draw::BindRasterBrush(TraceBrush, Target, Result->VectorscopeRasterSize);
		if (Target)
		{
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
		TrueScope::Draw::BindTextureBrush(TraceBrush, Texture, FIntPoint(NumBins, NumBins));
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

	// Too small to read: the shared hint mechanism says so (phase-6.1 Task 5).
	// The vectorscope hits this first of all scopes — its content rect is the
	// largest centered SQUARE, so a wide but flat panel shrinks it in both axes.
	if (IsTooSmallToRead(Content.GetSize()))
	{
		return DrawScopeHints(OutDrawElements, LayerId, AllottedGeometry, Content);
	}

	const float PlotSide = Content.GetSize().X - 2.0f * GraticulePadding;

	const FVector2f Center(Content.Left + Content.GetSize().X * 0.5f, Content.Top + Content.GetSize().Y * 0.5f);

	// 2x zoom (phase-6.2 Task 1d) magnifies the plotted density AND the
	// graticule around the neutral centre: everything below simply uses the
	// zoomed side/radius, and whatever falls outside the content square clips.
	// The content square itself — and with it the aspect lock — is untouched.
	const float ZoomFactor = Mode == ETrueScopeVectorscopeMode::Zoom2x ? 2.0f : 1.0f;
	const float DrawnSide = PlotSide * ZoomFactor;
	const float Radius = DrawnSide * 0.5f;

	// The GPU producer rasterizes the density square at exactly this size in
	// device pixels, so it is drawn 1:1 rather than upscaled (phase-6.1 Task 1).
	// Under zoom the request is the zoomed size — the visible quarter still
	// lands one texel on one device pixel.
	RequestRasterSize(TrueScope::ERasterTarget::Vectorscope, AllottedGeometry, FVector2f(DrawnSide, DrawnSide));

	// Everything scaled draws inside the content square only (matters under
	// zoom, where trace and graticule extend far beyond it).
	OutDrawElements.PushClip(FSlateClippingZone(AllottedGeometry.ToPaintGeometry(
		FVector2f(Content.GetSize()), FSlateLayoutTransform(FVector2f(Content.GetTopLeft())))));

	// Density plot: the whole [-0.5, 0.5] chroma square, so the out-of-circle
	// corners (100 % green, magenta) remain visible instead of being clipped.
	// The brush holds whichever producer's texture is current: the CPU path's
	// transient texture or the GPU path's render target.
	if (TraceBrush.GetResourceObject())
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(FVector2f(DrawnSide, DrawnSide),
				FSlateLayoutTransform(FVector2f(Center.X - Radius, Center.Y - Radius))),
			&TraceBrush, ESlateDrawEffect::NoGamma, FLinearColor::White);
	}
	++LayerId;

	// Guides on: the hue ring takes the plain circle's place — its inner edge
	// IS the 100 % chroma radius. Drawn before the line work so ticks and
	// targets stay on top.
	if (bShowGuides)
	{
		LayerId = DrawHueRing(OutDrawElements, LayerId, AllottedGeometry, Center, Radius);
	}

	// Graticule center cross; with guides off also the plain circle at 100 %
	// chroma amplitude, the tight bound of Cb and Cr.
	{
		if (!bShowGuides)
		{
			TArray<FVector2f> Circle;
			Circle.Reserve(CircleSegments + 1);
			for (int32 Segment = 0; Segment <= CircleSegments; ++Segment)
			{
				const float Angle = 2.0f * UE_PI * Segment / CircleSegments;
				Circle.Add(FVector2f(Center.X + Radius * FMath::Cos(Angle), Center.Y + Radius * FMath::Sin(Angle)));
			}
			DrawPolyline(OutDrawElements, LayerId, AllottedGeometry, Circle, GraticuleColor);
		}

		DrawPolyline(OutDrawElements, LayerId, AllottedGeometry,
			{FVector2f(Center.X - Radius, Center.Y), FVector2f(Center.X + Radius, Center.Y)}, GraticuleColor);
		DrawPolyline(OutDrawElements, LayerId, AllottedGeometry,
			{FVector2f(Center.X, Center.Y - Radius), FVector2f(Center.X, Center.Y + Radius)}, GraticuleColor);
	}
	++LayerId;

	if (bShowGuides)
	{
		// 5-degree ticks, drawn INWARD from the circle (Resolve's layout: ticks
		// inward, hue ring outward — they cannot compete). NOTE: the ticks do
		// NOT line up with the six targets, and that is CORRECT — the target
		// angles (102.9°, 49.7°, ...) are not multiples of 5. It will look
		// like a bug; do not rotate the ticks to "fix" it (phase-6.2 spec).
		for (int32 Tick = 0; Tick < NumTicks; ++Tick)
		{
			const float AngleDegrees = Tick * (360.0f / NumTicks);
			const FVector2f Direction = AngleToDirection(AngleDegrees);
			// Every second tick long (ticks at multiples of 10 degrees).
			const float Length = Radius * (Tick % 2 == 0 ? LongTickFraction : ShortTickFraction);
			DrawPolyline(OutDrawElements, LayerId, AllottedGeometry,
				{Center + Direction * (Radius - Length), Center + Direction * Radius}, TickColor);
		}

		// Skin tone line: desaturated centre to the rim at the conventional
		// angle (see TrueScope::SkinToneLineAngleDegrees for the derivation and
		// the three recorded confirmations — the constant is not to be
		// "corrected").
		DrawPolyline(OutDrawElements, LayerId, AllottedGeometry,
			{Center, Center + AngleToDirection(TrueScope::SkinToneLineAngleDegrees) * Radius}, SkinToneLineColor);
		++LayerId;
	}

	// The six 75 % targets: fixed-size boxes at their BT.709 positions, each
	// labelled just outside the box, radially away from center. With guides on,
	// also the 100 % targets (unlabelled) — each at its OWN chroma-derived
	// position; the six do not share a radius (phase-6.2 spec, measured).
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FSlateFontInfo LabelFont = GetLabelFont();

	auto DrawTargetBox = [&](const TrueScope::FVectorscopeTarget& Target)
	{
		const FVector2f Position = ChromaToLocal(Target.Chroma, Center, DrawnSide);

		const float Left = Position.X - TargetBoxHalfSize;
		const float Right = Position.X + TargetBoxHalfSize;
		const float Top = Position.Y - TargetBoxHalfSize;
		const float Bottom = Position.Y + TargetBoxHalfSize;
		DrawPolyline(OutDrawElements, LayerId, AllottedGeometry,
			{FVector2f(Left, Top), FVector2f(Right, Top), FVector2f(Right, Bottom), FVector2f(Left, Bottom),
				FVector2f(Left, Top)},
			TargetColor);
		return Position;
	};

	for (const TrueScope::FVectorscopeTarget& Target : TrueScope::GetVectorscopeTargets())
	{
		const FVector2f Position = DrawTargetBox(Target);

		FVector2f Direction = Position - Center;
		Direction.Normalize();

		const FString LabelString(Target.Label);
		const FVector2f LabelSize = FVector2f(FontMeasure->Measure(LabelString, LabelFont));
		const FVector2f LabelCenter = Position + Direction * (TargetBoxHalfSize + TargetLabelOffset);
		FSlateDrawElement::MakeText(OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(LabelSize, FSlateLayoutTransform(LabelCenter - LabelSize * 0.5f)),
			LabelString, LabelFont, ESlateDrawEffect::None, LabelColor);
	}

	if (bShowGuides)
	{
		for (const TrueScope::FVectorscopeTarget& Target : TrueScope::GetVectorscopeTargets100())
		{
			DrawTargetBox(Target);
		}
	}
	++LayerId;

	OutDrawElements.PopClip();

	return DrawScopeHints(OutDrawElements, LayerId, AllottedGeometry, Content);
}

int32 STrueScopeVectorscope::DrawHueRing(FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FGeometry& AllottedGeometry, const FVector2f& Center, float Radius) const
{
	// The ring's INNER edge sits exactly on the circle radius and it grows
	// OUTWARD only — it must never intrude into the plotting area, because the
	// circle is the 100 % saturation reference and the reading of the scope
	// must not change (phase-6.2 Task 1b).
	const float RingWidth = FMath::Clamp(Radius * HueRingWidthFraction, HueRingWidthMin, HueRingWidthMax);

	static const TArray<TrueScope::FHueRingStop> Stops = []
	{
		TArray<TrueScope::FHueRingStop> Built;
		TrueScope::BuildHueRingStops(HueRingSteps, Built);
		return Built;
	}();

	TArray<FSlateVertex> Vertices;
	Vertices.Reserve((Stops.Num() + 1) * 2);
	TArray<SlateIndex> Indices;
	Indices.Reserve(Stops.Num() * 6);

	for (int32 Index = 0; Index <= Stops.Num(); ++Index)
	{
		const TrueScope::FHueRingStop& Stop = Stops[Index % Stops.Num()];
		// Local y is down, +Cr is up (the AngleToDirection convention).
		const FVector2f Direction(FMath::Cos(Stop.AngleRadians), -FMath::Sin(Stop.AngleRadians));

		// Muted, and packed as display-encoded bytes: custom-vert colours pass
		// through Slate unencoded, so the byte value is the on-screen value —
		// and the stop colours ARE display-encoded sRGB values already.
		const FLinearColor Muted(Stop.Color.R * HueRingBrightness, Stop.Color.G * HueRingBrightness,
			Stop.Color.B * HueRingBrightness, HueRingAlpha);
		const FColor VertexColor = Muted.ToFColor(/*bSRGB*/ false);

		FSlateVertex Inner;
		Inner.Position = FVector2f(AllottedGeometry.LocalToAbsolute(FVector2D(Center + Direction * Radius)));
		Inner.TexCoords[0] = Inner.TexCoords[1] = Inner.TexCoords[2] = Inner.TexCoords[3] = 0.0f;
		Inner.Color = VertexColor;
		Vertices.Add(Inner);

		FSlateVertex Outer = Inner;
		Outer.Position =
			FVector2f(AllottedGeometry.LocalToAbsolute(FVector2D(Center + Direction * (Radius + RingWidth))));
		Vertices.Add(Outer);

		if (Index > 0)
		{
			const SlateIndex Base = static_cast<SlateIndex>((Index - 1) * 2);
			Indices.Append({Base, Base + 1, Base + 2, Base + 1, Base + 3, Base + 2});
		}
	}

	const FSlateBrush* WhiteBrush = FAppStyle::Get().GetBrush("WhiteBrush");
	FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId, WhiteBrush->GetRenderingResource(),
		Vertices, Indices, nullptr, 0, 0);
	return LayerId + 1;
}

#undef LOCTEXT_NAMESPACE
