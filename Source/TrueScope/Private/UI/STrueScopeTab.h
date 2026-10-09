// Copyright 2026 Michael Herber
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FTrueScopeCaptureGate;

/**
 * Root content widget of the TrueScope tab: the main bar, a 2x2 splitter grid
 * of four panels — Waveform, Parade (top) and Vectorscope, Histogram (bottom) —
 * and the status line.
 *
 * The tab owns the grid; a panel (STrueScopePanel) owns its header, its scope
 * canvas and its own settings. Named for what it is since M6.4, so "panel" can
 * mean one scope area throughout the code, as it does in the docs.
 *
 * Every paint is reported to the capture gate (M5): paint is the truthful
 * "this tab is visible and drawn" signal, so capture runs only while a panel
 * paints and a closed/hidden tab pays no capture cost.
 */
class STrueScopeTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STrueScopeTab)
		{
		}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TSharedPtr<FTrueScopeCaptureGate, ESPMode::ThreadSafe> CaptureGate;
};
