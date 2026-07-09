// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Analysis/TrueScopeAnalysisResult.h"
#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "UI/STrueScopeWidget.h"
#include "UObject/StrongObjectPtr.h"

class FTrueScopeAnalyzer;
class UTexture2D;

/**
 * Vectorscope: density of the frame's pixels over the BT.709 (Cb, Cr) plane,
 * with the standard graticule and its six 75 % color-bar targets
 * (R/Mg/B/Cy/G/Yl) drawn anti-aliased on top.
 *
 * Aspect-locked (architecture guardrail): the plot is always a centered
 * square, so the graticule circle stays a circle and never an ellipse — an
 * ellipse would falsify hue angle and saturation. Graticule, targets and
 * labels are fixed-size UI overlays, not part of the scaled graphic.
 */
class STrueScopeVectorscope : public STrueScopeWidget
{
public:
	SLATE_BEGIN_ARGS(STrueScopeVectorscope)
		{
		}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& InAnalyzer);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

protected:
	virtual void OnAnalysisResultChanged() override;

private:
	/** (Cb, Cr) density texture, NumVectorBins squared, updated per result. */
	TStrongObjectPtr<UTexture2D> TraceTexture;

	/** Brush wrapping the density texture for MakeBox. */
	FSlateBrush TraceBrush;
};
