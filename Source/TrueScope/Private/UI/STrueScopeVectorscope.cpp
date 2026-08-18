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
	 * Chroma (Cb, Cr) -> local widget space: SquareSide is the on-screen side
	 * of the [-0.5, 0.5] chroma square (one chroma unit per side). Since work
	 * order 15 that square is SMALLER than the circle's diameter — the circle
	 * is a fixed chroma radius (TrueScope::GetVectorscopeCircleChroma()), and the
	 * caller derives SquareSide from it, one shared scale for density and
	 * markers.
	 */
	FVector2f ChromaToLocal(const TrueScope::FChroma& Chroma, const FVector2f& Center, float SquareSide)
	{
		return FVector2f(Center.X + Chroma.Cb * SquareSide, Center.Y - Chroma.Cr * SquareSide);
	}

	void DrawPolyline(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& AllottedGeometry,
		const TArray<FVector2f>& Points, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(),
			Points, ESlateDrawEffect::None, Color, /*bAntialias*/ true, 1.0f);
	}
}

const FName STrueScopeVectorscope::SkinToneLineKey(TEXT("ShowSkinToneLine"));
const FName STrueScopeVectorscope::MarkersKey(TEXT("Markers"));

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
	// The hue ring and the 5-degree ticks are deliberately NOT here: they are
	// permanent orientation (work order 15, like Resolve), not options.
	static const TArray<FTrueScopeToggleOption> Toggles = {
		FTrueScopeToggleOption{SkinToneLineKey, LOCTEXT("ShowSkinToneLine", "Show Skin Tone Line"),
			LOCTEXT("ShowSkinToneLineTooltip",
				"Show the skin tone line: human skin of every ethnicity clusters at this hue angle.")},
	};
	return Toggles;
}

bool STrueScopeVectorscope::GetToggleOptionValue(FName Key) const
{
	return Key == SkinToneLineKey ? bShowSkinToneLine : false;
}

void STrueScopeVectorscope::SetToggleOptionValue(FName Key, bool bValue)
{
	if (Key == SkinToneLineKey)
	{
		bShowSkinToneLine = bValue;
	}
}

TArrayView<const FTrueScopeOptionGroup> STrueScopeVectorscope::GetOptionGroups() const
{
	// See ETrueScopeVectorscopeMarkers for the choices and the 75 % default.
	static const TArray<FTrueScopeOptionGroup> Groups = {
		FTrueScopeOptionGroup{MarkersKey,
			{
				FTrueScopeDisplayMode{static_cast<int32>(ETrueScopeVectorscopeMarkers::Bars75),
					LOCTEXT("Markers75", "75%")},
				FTrueScopeDisplayMode{static_cast<int32>(ETrueScopeVectorscopeMarkers::Bars100),
					LOCTEXT("Markers100", "100%")},
				FTrueScopeDisplayMode{static_cast<int32>(ETrueScopeVectorscopeMarkers::Both),
					LOCTEXT("MarkersBoth", "Both")},
			}},
	};
	return Groups;
}

int32 STrueScopeVectorscope::GetOptionGroupValue(FName Key) const
{
	return Key == MarkersKey ? static_cast<int32>(Markers) : 0;
}

void STrueScopeVectorscope::SetOptionGroupValue(FName Key, int32 ValueId)
{
	if (Key == MarkersKey)
	{
		Markers = static_cast<ETrueScopeVectorscopeMarkers>(ValueId);
	}
}


const FTrueScopeGainOption* STrueScopeVectorscope::GetGainOption() const
{
	// Function-local: LOCTEXT must not run before localization is initialized.
	static const FTrueScopeGainOption Option{
		TrueScope::TraceBrightnessGainKey,
		LOCTEXT("TraceBrightness", "Trace Brightness"),
		LOCTEXT("TraceBrightnessTooltip",
			"How bright a given pixel count draws. Centre is this scope's calibrated default; the ends are 1/8x and 8x. Double-click to reset."),
	};
	return &Option;
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

	// Radial calibration (work order 16): the circle is the DERIVED maximum
	// 100 % chroma magnitude (TrueScope::GetVectorscopeCircleChroma), so the
	// [-0.5, 0.5] chroma square — density AND all markers — spans this
	// fraction of the circle's diameter. One scale for everything: the trace
	// stays in register with the boxes, no 100 % marker can exceed the rim by
	// construction (magenta/green sit exactly ON it), and zoom multiplies
	// DrawnSide, so square and markers scale together.
	const float ChromaSquareSide = DrawnSide * (0.5f / TrueScope::GetVectorscopeCircleChroma());

	// The GPU producer rasterizes the density square at exactly this size in
	// device pixels, so it is drawn 1:1 rather than upscaled (phase-6.1 Task 1).
	// Under zoom the request is the zoomed size — the visible part still lands
	// one texel on one device pixel.
	RequestRasterSize(TrueScope::ERasterTarget::Vectorscope, AllottedGeometry,
		FVector2f(ChromaSquareSide, ChromaSquareSide));

	// Everything scaled draws inside the content square only (matters under
	// zoom, where trace and graticule extend far beyond it).
	OutDrawElements.PushClip(FSlateClippingZone(AllottedGeometry.ToPaintGeometry(
		FVector2f(Content.GetSize()), FSlateLayoutTransform(FVector2f(Content.GetTopLeft())))));

	// Density plot: the whole [-0.5, 0.5] chroma square. Its empty corners
	// reach past the rim (0.707/0.5957 = 1.19 R), which is harmless: chroma is
	// linear in R'G'B', so the gamut's Cb/Cr projection is the hexagon whose
	// vertices are the six 100 % points — no real colour exists beyond the
	// circle, and the graticule draws above the square. The brush holds
	// whichever producer's texture is current: the CPU path's transient
	// texture or the GPU path's render target.
	if (TraceBrush.GetResourceObject())
	{
		const float HalfSquare = ChromaSquareSide * 0.5f;
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(FVector2f(ChromaSquareSide, ChromaSquareSide),
				FSlateLayoutTransform(FVector2f(Center.X - HalfSquare, Center.Y - HalfSquare))),
			&TraceBrush, ESlateDrawEffect::NoGamma, FLinearColor::White);
	}
	++LayerId;

	// Hue ring: permanent orientation (work order 15, like Resolve) — its
	// inner edge IS the circle. Drawn before the line work so ticks and
	// targets stay on top.
	LayerId = DrawHueRing(OutDrawElements, LayerId, AllottedGeometry, Center, Radius);

	// Graticule center cross.
	DrawPolyline(OutDrawElements, LayerId, AllottedGeometry,
		{FVector2f(Center.X - Radius, Center.Y), FVector2f(Center.X + Radius, Center.Y)}, GraticuleColor);
	DrawPolyline(OutDrawElements, LayerId, AllottedGeometry,
		{FVector2f(Center.X, Center.Y - Radius), FVector2f(Center.X, Center.Y + Radius)}, GraticuleColor);
	++LayerId;

	// 5-degree ticks: permanent orientation as well, drawn INWARD from the
	// circle (Resolve's layout: ticks inward, hue ring outward — they cannot
	// compete). NOTE: the ticks do NOT line up with the six targets, and that
	// is CORRECT — the target angles (102.9°, 49.7°, ...) are not multiples
	// of 5. It will look like a bug; do not rotate the ticks to "fix" it
	// (phase-6.2 spec).
	for (int32 Tick = 0; Tick < NumTicks; ++Tick)
	{
		const float AngleDegrees = Tick * (360.0f / NumTicks);
		const FVector2f Direction = AngleToDirection(AngleDegrees);
		// Every second tick long (ticks at multiples of 10 degrees).
		const float Length = Radius * (Tick % 2 == 0 ? LongTickFraction : ShortTickFraction);
		DrawPolyline(OutDrawElements, LayerId, AllottedGeometry,
			{Center + Direction * (Radius - Length), Center + Direction * Radius}, TickColor);
	}

	// Skin tone line, behind its own toggle since work order 15: desaturated
	// centre to the rim at the conventional angle (see
	// TrueScope::SkinToneLineAngleDegrees for the derivation and the three
	// recorded confirmations — the constant is not to be "corrected").
	if (bShowSkinToneLine)
	{
		DrawPolyline(OutDrawElements, LayerId, AllottedGeometry,
			{Center, Center + AngleToDirection(TrueScope::SkinToneLineAngleDegrees) * Radius}, SkinToneLineColor);
	}
	++LayerId;

	// Target boxes per the Markers option: fixed-size boxes at their BT.709
	// positions on the shared radial scale — per-hue chroma-derived radii,
	// never a common circle (phase-6.2 caution; 100 % further out than 75 %,
	// both inside the rim). Letters go on the outermost displayed set, once.
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FSlateFontInfo LabelFont = GetLabelFont();

	auto DrawTargetBox = [&](const TrueScope::FVectorscopeTarget& Target)
	{
		const FVector2f Position = ChromaToLocal(Target.Chroma, Center, ChromaSquareSide);

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

	auto DrawTargetLabel = [&](const TrueScope::FVectorscopeTarget& Target, const FVector2f& Position)
	{
		FVector2f Direction = Position - Center;
		Direction.Normalize();

		const FString LabelString(Target.Label);
		const FVector2f LabelSize = FVector2f(FontMeasure->Measure(LabelString, LabelFont));
		const FVector2f LabelCenter = Position + Direction * (TargetBoxHalfSize + TargetLabelOffset);
		FSlateDrawElement::MakeText(OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(LabelSize, FSlateLayoutTransform(LabelCenter - LabelSize * 0.5f)),
			LabelString, LabelFont, ESlateDrawEffect::None, LabelColor);
	};

	const bool bShow75 = Markers != ETrueScopeVectorscopeMarkers::Bars100;
	const bool bShow100 = Markers != ETrueScopeVectorscopeMarkers::Bars75;
	if (bShow75)
	{
		for (const TrueScope::FVectorscopeTarget& Target : TrueScope::GetVectorscopeTargets())
		{
			const FVector2f Position = DrawTargetBox(Target);
			if (!bShow100)
			{
				DrawTargetLabel(Target, Position);
			}
		}
	}
	if (bShow100)
	{
		for (const TrueScope::FVectorscopeTarget& Target : TrueScope::GetVectorscopeTargets100())
		{
			DrawTargetLabel(Target, DrawTargetBox(Target));
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
	// circle is the scope's fixed chroma reference
	// (TrueScope::GetVectorscopeCircleChroma()) and the reading must not change
	// (phase-6.2 Task 1b).
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
