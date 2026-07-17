// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Analysis/TrueScopeAnalysisResult.h"
#include "Containers/ArrayView.h"
#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "UI/STrueScopeWidget.h"

class FTrueScopeAnalyzer;

/** How the histogram lays out its channel curves. */
enum class ETrueScopeHistogramMode : int32
{
	/**
	 * Default: R, G and B stacked in separate sub-plots, as in DaVinci Resolve
	 * and the film/video tools generally. Channel imbalances are readable
	 * because the curves cannot hide behind each other.
	 *
	 * The default stays RGB, not YRGB, although Resolve defaults to YRGB
	 * (decided in phase-6.2): a four-way split reduces per-curve height and
	 * gets cramped on small panels, so the fourth curve is opt-in.
	 */
	SeparateChannels = 0,

	/**
	 * All three curves superimposed in one plot — the photo-editing convention
	 * (Photoshop) and TrueScope's M3 look. Same data, denser presentation.
	 */
	RgbOverlay = 1,

	/**
	 * Four stacked bands — Luma (top), R, G, B — as Resolve's YRGB histogram
	 * (phase-6.2 Task 2). The luma curve derives from the SAME integer-exact
	 * BT.709 luma bins the waveform plots (phase-5.2); there is no second luma
	 * computation. Opt-in; see SeparateChannels for why it is not the default.
	 */
	SeparateChannelsYrgb = 2,
};

/**
 * RGB histogram scope: per-channel pixel-count curves over the normalized
 * 10-bit 0-1023 value axis (display-referred), drawn as translucent
 * anti-aliased filled curves (vector, not texture). The vertical axis is a
 * linear pixel count on one scale shared by the three channels, so their
 * relative heights stay truthful in both modes.
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
	 * Number of curves a display mode draws: 3 for the RGB modes, 4 for YRGB.
	 * Curves are addressed by SLOT below — in YRGB, slot 0 is the luma band
	 * and slots 1..3 are R, G, B; in the RGB modes slot == channel.
	 */
	static int32 GetNumCurveSlots(ETrueScopeHistogramMode Mode);

	/**
	 * The colour channel a display slot draws, or INDEX_NONE for the YRGB
	 * mode's luma slot. The single place the slot order (Y on top, then R, G,
	 * B) is defined, so drawing and tests cannot disagree about it.
	 */
	static int32 SlotToChannel(ETrueScopeHistogramMode Mode, int32 Slot);

	/**
	 * Sub-rect of the plot area a display slot's curve occupies. Separate
	 * modes stack the slots top-down with a gap between them (3 bands for RGB,
	 * 4 for YRGB); overlay mode gives every slot the whole plot. Pure and
	 * static so the layout rule is testable without Slate.
	 */
	static FSlateRect GetChannelPlotRect(const FSlateRect& Plot, ETrueScopeHistogramMode Mode, int32 Slot);

	/**
	 * The bins drawn as Channel's curve, in Channel's colour — the single place
	 * the analysis arrays are bound to the three curves, and the same binding in
	 * both display modes. Pulled out of OnPaint so a test can assert that the
	 * red-tinted curve really carries the red channel.
	 */
	static TArrayView<const uint32> GetChannelBins(const FTrueScopeAnalysisResult& Result, int32 Channel);

	/**
	 * The luma histogram bins of the YRGB mode (phase-6.2): the column sums of
	 * the result's LumaParade — the SAME integer-exact BT.709 luma bins the
	 * waveform plots, merely summed over the image columns (exactly how the
	 * R/G/B histograms relate to the parade). No second luma computation, no
	 * float binning. Empty result on a GPU result (its bins never exist in CPU
	 * memory; the GPU path reduces the same column bins on the GPU).
	 */
	static TArray<uint32> BuildLumaHistogramBins(const FTrueScopeAnalysisResult& Result);

	/** Tint of Channel's curve; the component of Channel dominates. */
	static FLinearColor GetChannelFillColor(int32 Channel);
	static FLinearColor GetChannelLineColor(int32 Channel);

	/**
	 * Largest bin count across all three colour channels — one shared vertical
	 * scale, so the curves' relative heights stay comparable.
	 */
	static uint32 GetSharedMaxCount(const FTrueScopeAnalysisResult& Result);

	/**
	 * The YRGB mode's shared scale: the maximum over all FOUR displayed curves
	 * — luma participates (phase-6.2 decision). Rationale: the shared-scale
	 * rule exists to preserve the true ratio between the displayed curves
	 * (architecture.md, "Histogram vertical scaling"), and once the luma curve
	 * is displayed that argument covers it too; scaling it independently would
	 * inflate or crush it relative to its own channels. Matches Resolve, where
	 * a neutral frame's four YRGB peaks read at equal height.
	 */
	static uint32 GetYrgbSharedMaxCount(const FTrueScopeAnalysisResult& Result,
		TArrayView<const uint32> LumaBins);

	/**
	 * Curve height in [0,1] for a bin count. **Linear**: the histogram plots how
	 * many pixels hold a value, so the height must be proportional to that count.
	 *
	 * Deliberately NOT TrueScope::CountToIntensity — that log curve maps a count
	 * to the *brightness* of a density texel (parade, waveform, vectorscope),
	 * where lifting sparse cells into visibility is the point. Used as a curve
	 * height it draws a bin holding 0.04 % of the frame at ~60 % height, which
	 * flat-tops every curve and makes a channel appear to reach whatever its
	 * rarest outlier pixel is. That inverted the apparent R/B order against
	 * DaVinci Resolve on a ColorCalibrator frame (M4 review).
	 */
	static float GetCurveHeight(uint32 Count, uint32 MaxCount);

	virtual bool HasDrawableData() const override;

protected:
	virtual void OnAnalysisResultChanged() override;

	virtual TArrayView<const FTrueScopeDisplayMode> GetDisplayModes() const override;
	virtual int32 GetDisplayModeId() const override;
	virtual void SetDisplayModeId(int32 InModeId) override;

private:
	ETrueScopeHistogramMode Mode = ETrueScopeHistogramMode::SeparateChannels;

	/**
	 * CPU path: the YRGB mode's luma histogram bins, rebuilt per result from
	 * the result's LumaParade (BuildLumaHistogramBins). Empty while a GPU
	 * result is displayed — the GPU path's luma band arrives pre-rasterized.
	 */
	TArray<uint32> LumaHistogramBins;

	/**
	 * GPU path (phase-5.2): brushes pointing at the producer's curve raster
	 * targets — per-channel textures for Separate Channels, one combined
	 * texture for RGB Overlay. Colours and layer order are baked by the draw
	 * pass from the same shared constants the CPU vector draw uses; the
	 * count -> height mapping stays linear on the shared scale (the raster
	 * only replaces the rasterizer, never the mapping). Unused (no resource)
	 * while a CPU result is displayed — there OnPaint draws the vector curves
	 * from the result's bin arrays as before.
	 */
	FSlateBrush StackedBrush[FTrueScopeAnalysisResult::NumChannels];
	FSlateBrush OverlayBrush;

	/** YRGB band rasters, display order Y, R, G, B (phase-6.2 Task 2). */
	FSlateBrush YrgbBrush[FTrueScopeAnalysisResult::NumYrgbCurves];
};
