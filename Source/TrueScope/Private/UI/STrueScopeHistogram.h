// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Analysis/TrueScopeAnalysisResult.h"
#include "CoreMinimal.h"
#include "UI/STrueScopeWidget.h"

class FTrueScopeAnalyzer;

/** How the histogram lays out its three channel curves. */
enum class ETrueScopeHistogramMode : int32
{
	/**
	 * Default: R, G and B stacked in separate sub-plots, as in DaVinci Resolve
	 * and the film/video tools generally. Channel imbalances are readable
	 * because the curves cannot hide behind each other.
	 */
	SeparateChannels = 0,

	/**
	 * All three curves superimposed in one plot — the photo-editing convention
	 * (Photoshop) and TrueScope's M3 look. Same data, denser presentation.
	 */
	RgbOverlay = 1,
};

/**
 * RGB histogram scope: per-channel pixel-count curves over the normalized
 * 10-bit 0-1023 value axis (display-referred), drawn as translucent
 * anti-aliased filled curves (vector, not texture). Channels share one
 * normalization so their relative heights stay truthful in both modes.
 *
 * Responsive: fills its panel. Right-click switches the display mode.
 */
class STrueScopeHistogram : public STrueScopeWidget
{
public:
	SLATE_BEGIN_ARGS(STrueScopeHistogram)
		{
		}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& InAnalyzer);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	/**
	 * Sub-rect of the plot area a channel's curve occupies. Separate mode
	 * stacks the channels top-down (R, G, B) with a gap between them; overlay
	 * mode gives every channel the whole plot. Pure and static so the layout
	 * rule is testable without Slate.
	 */
	static FSlateRect GetChannelPlotRect(const FSlateRect& Plot, ETrueScopeHistogramMode Mode, int32 Channel);

protected:
	virtual TArrayView<const FTrueScopeDisplayMode> GetDisplayModes() const override;
	virtual int32 GetDisplayModeId() const override;
	virtual void SetDisplayModeId(int32 InModeId) override;

private:
	ETrueScopeHistogramMode Mode = ETrueScopeHistogramMode::SeparateChannels;
};
