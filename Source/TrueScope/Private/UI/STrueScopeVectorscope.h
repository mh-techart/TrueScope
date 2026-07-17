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

/** Vectorscope display modes: plot magnification (phase-6.2 Task 1d). */
enum class ETrueScopeVectorscopeMode : int32
{
	/** Default: the full [-0.5, 0.5] chroma square fills the plot. */
	Normal = 0,

	/**
	 * 2x magnification around the neutral centre, for reading low-saturation
	 * material. Density and graticule scale together; the aspect lock and
	 * circularity are untouched (everything outside the plot square clips).
	 */
	Zoom2x = 1,
};

/**
 * Vectorscope: density of the frame's pixels over the BT.709 (Cb, Cr) plane,
 * with the standard graticule and its six 75 % color-bar targets
 * (R/Mg/B/Cy/G/Yl) drawn anti-aliased on top. "Show Guides" (phase-6.2 Task 1,
 * default on) adds the skin tone line, the hue ring, the 5-degree ticks and
 * the 100 % targets; off reverts to the plain M4 graticule.
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

	virtual bool HasDrawableData() const override;

	/** Settings key of the Show Guides toggle (phase-6.2). */
	static const FName ShowGuidesKey;

protected:
	virtual void OnAnalysisResultChanged() override;

	virtual TArrayView<const FTrueScopeDisplayMode> GetDisplayModes() const override;
	virtual int32 GetDisplayModeId() const override;
	virtual void SetDisplayModeId(int32 InModeId) override;

	virtual TArrayView<const FTrueScopeToggleOption> GetToggleOptions() const override;
	virtual bool GetToggleOptionValue(FName Key) const override;
	virtual void SetToggleOptionValue(FName Key, bool bValue) override;

private:
	/**
	 * The hue ring (phase-6.2 Task 1b): a colour-graded strip whose inner edge
	 * sits exactly on the graticule circle, growing outward only. Hues are
	 * generated from the BT.709 Cb/Cr formula (TrueScope::BuildHueRingStops),
	 * never hand-placed. Returns the next free layer.
	 */
	int32 DrawHueRing(FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FGeometry& AllottedGeometry, const FVector2f& Center, float Radius) const;

	ETrueScopeVectorscopeMode Mode = ETrueScopeVectorscopeMode::Normal;

	/** Guides on by default: the graticule is the scope's reading aid. */
	bool bShowGuides = true;

	/**
	 * CPU path: (Cb, Cr) density texture, NumVectorBins squared, updated per
	 * result. Unused (reset) while a GPU result is displayed — there the brush
	 * points directly at the producer's render target (phase-5.1).
	 */
	TStrongObjectPtr<UTexture2D> TraceTexture;

	/** Brush wrapping the current producer's density texture for MakeBox. */
	FSlateBrush TraceBrush;
};
