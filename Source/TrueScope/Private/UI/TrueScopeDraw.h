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
	inline const FLinearColor BackgroundColor(0.008f, 0.008f, 0.008f);
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
	 * Outline thickness of the histogram curves, shared for the same reason as
	 * the colours: the widget passes it to Slate's MakeLines, the GPU raster
	 * pass receives it as a shader parameter and reproduces Slate's own
	 * antialiased-line coverage from it, so neither path can drift into a
	 * heavier stroke than the other.
	 */
	inline constexpr float HistogramLineThickness = 1.0f;

	/** Fixed-size chrome around the plot area. */
	inline constexpr float PlotPadding = 6.0f;
	inline constexpr float ValueLabelStripWidth = 30.0f;
	inline constexpr float ValueLabelStripHeight = 14.0f;

	/** Breathing room between the hint text and the edge of its plate. */
	inline constexpr float HintPlatePaddingX = 10.0f;
	inline constexpr float HintPlatePaddingY = 6.0f;

	/**
	 * Full scale of the normalized 10-bit value axis shared by waveform, parade
	 * and histogram (architecture.md, "Scales") — a convention, not a claim
	 * about the source's bit depth.
	 */
	inline constexpr int32 ScaleMax = 1023;

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
