// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FTrueScopeCaptureGate;

/**
 * Root content widget of the TrueScope tab: a 2x2 splitter grid holding the
 * four scope widgets — Waveform, Parade (top) and Vectorscope, Histogram
 * (bottom), built through the scope-type factory.
 *
 * Every paint is reported to the capture gate (M5): paint is the truthful
 * "this tab is visible and drawn" signal, so capture runs only while a panel
 * paints and a closed/hidden tab pays no capture cost.
 */
class STrueScopePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STrueScopePanel)
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
