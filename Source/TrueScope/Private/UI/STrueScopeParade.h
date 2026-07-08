// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "UI/STrueScopeWidget.h"
#include "UObject/StrongObjectPtr.h"

class FTrueScopeAnalyzer;
class UTexture2D;

/**
 * RGB parade scope: three side-by-side sections (R, G, B), each plotting the
 * channel's value distribution per image column on a 10-bit 0-1023 scale.
 *
 * The density data (columns x bins, log-mapped intensity — see
 * FTrueScopeAnalysisResult::ParadeImage) is uploaded into a dynamic texture
 * and drawn bilinearly scaled per section with the channel tinted out;
 * graticule lines and the fixed-size scale labels are anti-aliased Slate
 * elements on top (architecture "anti-pixelation recipe"). Responsive: fills
 * its panel.
 */
class STrueScopeParade : public STrueScopeWidget
{
public:
	SLATE_BEGIN_ARGS(STrueScopeParade)
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
	/** Density texture, recreated on size change, updated per result. */
	TStrongObjectPtr<UTexture2D> ParadeTexture;

	/** Brush wrapping ParadeTexture for MakeBox. */
	FSlateBrush ParadeBrush;
};
