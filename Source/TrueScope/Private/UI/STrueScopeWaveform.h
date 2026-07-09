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
 * Waveform scope: the parade's mechanism without channel separation. One
 * full-width plot of value distribution per image column on the normalized
 * 10-bit 0-1023 scale, with the three channel densities superimposed in a
 * single trace (Resolve's default waveform) — equal densities read white, a
 * channel excursion shows in its own colour.
 *
 * Shares the parade's analysis data; only the presentation differs. Graticule
 * and the fixed-size scale labels are anti-aliased Slate elements on top.
 * Responsive: fills its panel.
 */
class STrueScopeWaveform : public STrueScopeWidget
{
public:
	SLATE_BEGIN_ARGS(STrueScopeWaveform)
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
	/** Combined RGB density texture, recreated on size change, updated per result. */
	TStrongObjectPtr<UTexture2D> TraceTexture;

	/** Brush wrapping the density texture for MakeBox. */
	FSlateBrush TraceBrush;
};
