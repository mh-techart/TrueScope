// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Containers/ArrayView.h"
#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Layout/Geometry.h"
#include "Layout/SlateRect.h"

class FSlateWindowElementList;

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
