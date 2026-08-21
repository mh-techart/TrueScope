// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Containers/ArrayView.h"
#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Layout/Geometry.h"
#include "Layout/SlateRect.h"

class FSlateWindowElementList;
class UTexture;
class UTextureRenderTarget2D;
struct FSlateBrush;

/**
 * Chrome shared by the scope widgets: one background, one set of colors and one
 * value axis, so the same reading means the same thing in every scope.
 *
 * All chrome is fixed-size and never scales with the panel (architecture
 * guardrail); the drawing helpers are the only place the 0-1023 scale is turned
 * into pixels.
 */
namespace TrueScope::Draw
{
	/**
	 * Black (M6.4 Task 7). The near-black it replaced (0.008) was invisible
	 * behind the waveform's and parade's opaque trace rasters but not behind the
	 * vectorscope's and histogram's transparent ones, so two scopes read as
	 * black and two as dark grey. Black is also the reference tools' background,
	 * and it is the value a viewer's eye adapts to when judging a trace's
	 * lowest visible density.
	 */
	inline const FLinearColor BackgroundColor(0.0f, 0.0f, 0.0f);
	inline const FLinearColor GraticuleColor(0.5f, 0.5f, 0.5f, 0.22f);
	inline const FLinearColor LabelColor(0.6f, 0.6f, 0.6f, 0.9f);

	/** Paused hint: amber text on a translucent plate — readable over a dense trace. */
	inline const FLinearColor HintTextColor(0.98f, 0.78f, 0.30f, 1.0f);
	inline const FLinearColor HintPlateColor(0.02f, 0.02f, 0.02f, 0.78f);

	/**
	 * Histogram curve colours: translucent fill under a stronger outline, per
	 * channel. Shared between the widget's CPU vector draw and the phase-5.2
	 * GPU raster pass (which receives them as shader parameters), so both
	 * paths draw pixel-identical colours by construction.
	 */
	inline const FLinearColor HistogramFillColors[3] = {
		FLinearColor(0.9f, 0.08f, 0.08f, 0.38f),
		FLinearColor(0.08f, 0.9f, 0.08f, 0.38f),
		FLinearColor(0.15f, 0.35f, 1.0f, 0.38f),
	};
	inline const FLinearColor HistogramLineColors[3] = {
		FLinearColor(1.0f, 0.25f, 0.25f, 0.9f),
		FLinearColor(0.25f, 1.0f, 0.25f, 0.9f),
		FLinearColor(0.35f, 0.55f, 1.0f, 0.9f),
	};

	/**
	 * The luma curve of the histogram's YRGB mode (phase-6.2 Task 2): neutral,
	 * as Resolve draws its Y histogram — a colour would suggest a channel that
	 * does not exist. Same fill/line split and alphas as the colour channels.
	 */
	inline const FLinearColor HistogramLumaFillColor(0.75f, 0.75f, 0.75f, 0.38f);
	inline const FLinearColor HistogramLumaLineColor(0.85f, 0.85f, 0.85f, 0.9f);

	/**
	 * Outline thickness of the histogram curves, shared for the same reason as
	 * the colours: the widget passes it to Slate's MakeLines, the GPU raster
	 * pass receives it as a shader parameter and reproduces Slate's own
	 * antialiased-line coverage from it, so neither path can drift into a
	 * heavier stroke than the other.
	 */
	inline constexpr float HistogramLineThickness = 1.0f;

	/**
	 * The histogram outline colour as the GPU raster shader must receive it to
	 * match the CPU line's brightness (work order 13 Fix 2). Slate packs
	 * MakeLines tints into 8-bit vertex colours with an sRGB ENCODE
	 * (FSlateElementBatcher::PackVertexColor — bSRGBVertexColor is true for
	 * the RHI renderer, SlateRHIRenderingPolicy::IsVertexColorInLinearSpace
	 * returns false) and its shaders then use those bytes without decoding, so
	 * the encoded value IS what reaches the screen. The GPU raster is shown
	 * through NoGamma, value-preserving as well — but it was fed the LINEAR
	 * colour, which for mid-tones lands visibly darker (0.25 linear encodes to
	 * ~0.537). Fills never disagreed because the widget packs them with
	 * ToFColor(false), numerically the same linear values the shader gets.
	 *
	 * One function derived from the one shared constant, so the two paths
	 * cannot drift apart again. The round-trip through FColor quantizes to
	 * 8 bits exactly like Slate's vertex path does; alpha stays linear, also
	 * like Slate's.
	 */
	inline FLinearColor EncodeSlateLineColor(const FLinearColor& LineColor)
	{
		const FColor Encoded = LineColor.ToFColor(/*bSRGB*/ true);
		return FLinearColor(
			Encoded.R / 255.0f, Encoded.G / 255.0f, Encoded.B / 255.0f, Encoded.A / 255.0f);
	}

	inline FLinearColor GetHistogramLineRasterColor(int32 Channel)
	{
		return EncodeSlateLineColor(HistogramLineColors[Channel]);
	}

	/** The luma curve's outline, encoded like the channel lines (same rationale). */
	inline FLinearColor GetHistogramLumaLineRasterColor()
	{
		return EncodeSlateLineColor(HistogramLumaLineColor);
	}

	/** Fixed-size chrome around the plot area. */
	inline constexpr float PlotPadding = 6.0f;
	inline constexpr float ValueLabelStripWidth = 30.0f;
	inline constexpr float ValueLabelStripHeight = 14.0f;

	/** Breathing room between the hint text and the edge of its plate. */
	inline constexpr float HintPlatePaddingX = 10.0f;
	inline constexpr float HintPlatePaddingY = 6.0f;

	/**
	 * Below this content size a scope stops being readable — its fixed chrome
	 * (label strip, padding, graticule) eats the plot, and what is left conveys
	 * nothing (phase-6.1 Task 5). Scopes show a short hint there instead of a
	 * broken render; below the size where even the hint plate fits,
	 * DrawCenteredHint draws nothing rather than clipped text.
	 *
	 * The vectorscope reaches this first because it is aspect-locked: its content
	 * rect is the largest centered SQUARE, so a wide, flat panel shrinks it in
	 * both axes at once.
	 */
	inline constexpr float MinReadableScopeWidth = 180.0f;
	inline constexpr float MinReadableScopeHeight = 120.0f;

	/** The readable-size rule, pure so it can be tested without Slate. */
	inline bool IsTooSmallToRead(FVector2f ContentSize)
	{
		return ContentSize.X < MinReadableScopeWidth || ContentSize.Y < MinReadableScopeHeight;
	}

	/**
	 * Full scale of the normalized 10-bit value axis shared by waveform, parade
	 * and histogram (architecture.md, "Scales") — a convention, not a claim
	 * about the source's bit depth.
	 */
	inline constexpr int32 ScaleMax = 1023;

	/**
	 * Minor scale ticks (phase-6.2 Task 3, Resolve's scales): three short
	 * intermediate ticks between each pair of labelled majors, as fixed-size
	 * chrome next to the label strip. Pure value helper so the rule is
	 * testable without Slate.
	 */
	inline constexpr int32 MinorTicksPerDivision = 3;

	/** The axis values of the minor ticks between two labelled majors. */
	inline void GetMinorTickValues(int32 LowerLevel, int32 UpperLevel, TArray<float>& OutValues)
	{
		for (int32 Tick = 1; Tick <= MinorTicksPerDivision; ++Tick)
		{
			OutValues.Add(LowerLevel
				+ (UpperLevel - LowerLevel) * Tick / static_cast<float>(MinorTicksPerDivision + 1));
		}
	}

	/**
	 * Points a scope brush at a GPU raster target and restricts sampling to the
	 * region the producer rasterized this capture (phase-6.1 Task 1).
	 *
	 * The targets are ALLOCATED in size buckets, so a target is usually a little
	 * larger than the raster inside it; the UV region hands Slate exactly the
	 * rasterized part. Drawn into the plot rect it was rasterized for, that keeps
	 * one texel on one device pixel — the bilinear filter becomes an identity
	 * instead of an upscale that thickens and smears the trace.
	 */
	void BindRasterBrush(FSlateBrush& Brush, UTextureRenderTarget2D* Target, FIntPoint RasterSize);

	/** Points a scope brush at a CPU-path texture: the whole texture, no region. */
	void BindTextureBrush(FSlateBrush& Brush, UTexture* Texture, FIntPoint Size);

	/** The one font every scope labels with — fixed size, never scaled. */
	FSlateFontInfo GetLabelFont();

	/** Hint font — one step above the labels, still fixed size. */
	FSlateFontInfo GetHintFont();

	/** Waveform / parade value gridlines: every 128, i.e. 8 divisions. */
	TArrayView<const int32> GetWaveformGraticuleLevels();

	/** Histogram value gridlines: 4 divisions of the same 0-1023 axis. */
	TArrayView<const int32> GetHistogramGraticuleLevels();

	/** Recessed scope background. Returns the next free layer. */
	int32 DrawBackground(FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FGeometry& AllottedGeometry, const FSlateRect& Rect);

	/**
	 * Value axis on Y (waveform, parade): anti-aliased horizontal gridlines
	 * across Plot, with fixed-size labels in the strip left of it. Returns the
	 * next free layer.
	 */
	int32 DrawValueAxisY(FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FGeometry& AllottedGeometry, const FSlateRect& Plot, const FSlateRect& Content,
		TArrayView<const int32> Levels);

	/**
	 * Value axis on X (histogram): anti-aliased vertical gridlines across Plot,
	 * with fixed-size labels in the strip below it. Returns the next free layer.
	 */
	int32 DrawValueAxisX(FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FGeometry& AllottedGeometry, const FSlateRect& Plot, const FSlateRect& Content,
		TArrayView<const int32> Levels);

	/**
	 * Fixed-size hint centered in Rect, on a translucent plate: newline-separated
	 * lines, each centered. Drawn *over* the scope, never instead of it — the held
	 * values stay readable around it (M4.1 paused indicator). Returns the next
	 * free layer; draws nothing if Rect cannot fit the text.
	 */
	int32 DrawCenteredHint(FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FGeometry& AllottedGeometry, const FSlateRect& Rect, const FText& Hint);
}
