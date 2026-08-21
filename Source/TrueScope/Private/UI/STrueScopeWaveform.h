// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Analysis/TrueScopeAnalysisResult.h"
#include "Containers/ArrayView.h"
#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "UI/STrueScopeWidget.h"
#include "UObject/StrongObjectPtr.h"

class FTrueScopeAnalyzer;
class UTexture2D;

/** Which signal the waveform traces. Both are standard; neither is "the" waveform. */
enum class ETrueScopeWaveformMode : int32
{
	/**
	 * Default: the three channel densities superimposed in one trace, as in
	 * DaVinci Resolve. Equal densities read white, a channel excursion shows in
	 * its own colour — the view for judging colour balance.
	 */
	Rgb = 0,

	/**
	 * BT.709 luma only, drawn as a neutral trace — the classic waveform, and the
	 * view for judging exposure without colour distracting from it.
	 */
	Luma = 1,
};

/**
 * Waveform scope: the parade's mechanism without channel separation. One
 * full-width plot of value distribution per image column on the normalized
 * 10-bit 0-1023 scale.
 *
 * Shares the parade's analysis data (and, in Luma mode, the vectorscope's own
 * BT.709 conversion); only the presentation differs. Graticule and the
 * fixed-size scale labels are anti-aliased Slate elements on top. Responsive:
 * fills its panel. Right-click switches the display mode.
 */
class STrueScopeWaveform : public STrueScopeWidget
{
public:
	SLATE_BEGIN_ARGS(STrueScopeWaveform)
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

	/** Texels the trace texture holds for a result; 0 when there is nothing to draw. */
	static int32 GetTexelCount(const FTrueScopeAnalysisResult& Result);

	/**
	 * Fills the trace texels for a display mode — the single place a mode is
	 * bound to the analysis data it draws. Pulled out of the widget so a test
	 * can assert that Luma mode really traces luma and not a colour channel
	 * (the "which data lands in which curve" layer the M4 histogram bug lived
	 * in). OutTexels must hold exactly GetTexelCount(Result) entries.
	 *
	 * WaveformGain is the ABSOLUTE gain of the RGB trace (M6.3) — the scope's
	 * calibrated default times the user's multiplier. Luma mode ignores it:
	 * its trace is built by the analysis producer, which applied the same gain
	 * there, so passing it in twice would square it.
	 */
	static void BuildTexels(const FTrueScopeAnalysisResult& Result, ETrueScopeWaveformMode Mode,
		float WaveformGain, TArrayView<FColor> OutTexels);

	virtual bool HasDrawableData() const override;

protected:
	virtual void OnAnalysisResultChanged() override;

	virtual TArrayView<const FTrueScopeDisplayMode> GetDisplayModes() const override;
	virtual int32 GetDisplayModeId() const override;
	virtual void SetDisplayModeId(int32 InModeId) override;

	virtual const FTrueScopeGainOption* GetGainOption() const override;

private:
	/** Re-uploads the trace from the cached result; on a new result or a mode switch. */
	void UpdateTraceTexture();

	/** Density texture, recreated on size change, updated per result and per mode. */
	TStrongObjectPtr<UTexture2D> TraceTexture;

	/** Brush wrapping the density texture for MakeBox. */
	FSlateBrush TraceBrush;

	ETrueScopeWaveformMode Mode = ETrueScopeWaveformMode::Rgb;
};
