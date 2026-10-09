// Copyright 2026 Michael Herber
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
 * RGB parade scope: three side-by-side sections (R, G, B), each plotting the
 * channel's value distribution per image column on a 10-bit 0-1023 scale.
 *
 * Each section has its own single-channel density texture, fed from that
 * channel's intensity array (FTrueScopeAnalysisResult::ParadeChannelIntensity)
 * and drawn bilinearly scaled in the channel's colour — so a block can only
 * ever show its own channel, never bleed from the other two. Graticule lines
 * and the fixed-size scale labels are anti-aliased Slate elements on top
 * (architecture "anti-pixelation recipe"). Responsive: fills its panel.
 */
class STrueScopeParade : public STrueScopeWidget
{
public:
	SLATE_BEGIN_ARGS(STrueScopeParade)
			: _PanelIndex(0)
		{
		}
		/** Panel slot this scope occupies; what its settings are keyed by (M6.4). */
		SLATE_ARGUMENT(int32, PanelIndex)


	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& InAnalyzer);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual bool HasDrawableData() const override;

protected:
	virtual void OnAnalysisResultChanged() override;

	virtual const FTrueScopeGainOption* GetGainOption() const override;

private:
	/**
	 * CPU path: per-channel density textures built from the result's intensity
	 * arrays, recreated on size change, updated per result. Unused (reset)
	 * while a GPU result is displayed — there the brushes point directly at
	 * the producer's render targets.
	 */
	TStrongObjectPtr<UTexture2D> ChannelTexture[FTrueScopeAnalysisResult::NumChannels];

	/** Brushes wrapping the per-channel textures for MakeBox. */
	FSlateBrush ChannelBrush[FTrueScopeAnalysisResult::NumChannels];
};
