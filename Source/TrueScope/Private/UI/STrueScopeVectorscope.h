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
 * Which target-box sets the vectorscope shows (work order 15, Fix 2).
 * Default is 75 % only: the SDR broadcast convention and the M4 state; the
 * 100 % boxes matter for HDR / cinema / streaming targets, so those users opt
 * in — offering both keeps TrueScope useful beyond pure broadcast.
 */
enum class ETrueScopeVectorscopeMarkers : int32
{
	Bars75 = 0,
	Bars100 = 1,
	Both = 2,
};

/**
 * Vectorscope: density of the frame's pixels over the BT.709 (Cb, Cr) plane,
 * with the standard graticule drawn anti-aliased on top. The hue ring and the
 * 5-degree ticks are permanent orientation (work order 15, like Resolve); the
 * skin tone line has its own "Show Skin Tone Line" toggle (default on); the
 * target-box sets are selected via the Markers option (75 % / 100 % / Both,
 * default 75 %). The radial scale puts the circle at the DERIVED maximum
 * 100 % chroma (TrueScope::GetVectorscopeCircleChroma), so no marker can
 * exceed the rim by construction — magenta/green 100 % sit exactly ON it,
 * everything else inside, matching Resolve.
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

	virtual TArrayView<const FTrueScopeDisplayMode> GetDisplayModes() const override;
	virtual FText GetDisplayModeSectionLabel() const override;
	virtual int32 GetDisplayModeId() const override;
	virtual void SetDisplayModeId(int32 InModeId) override;

	virtual TArrayView<const FTrueScopeToggleOption> GetToggleOptions() const override;
	virtual bool GetToggleOptionValue(FName Key) const override;
	virtual void SetToggleOptionValue(FName Key, bool bValue) override;

	virtual TArrayView<const FTrueScopeOptionGroup> GetOptionGroups() const override;
	virtual int32 GetOptionGroupValue(FName Key) const override;
	virtual void SetOptionGroupValue(FName Key, int32 ValueId) override;

	virtual const FTrueScopeGainOption* GetGainOption() const override;

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

	/** See ETrueScopeVectorscopeMarkers for why the default is 75 % only. */
	ETrueScopeVectorscopeMarkers Markers = ETrueScopeVectorscopeMarkers::Bars75;

	/**
	 * Skin line on by default (work order 15): it was the shipped default as
	 * part of the former Show Guides toggle, and it is a useful plausibility
	 * guide for the MetaHuman-in-viewport case.
	 */
	bool bShowSkinToneLine = true;

	/**
	 * CPU path: (Cb, Cr) density texture, NumVectorBins squared, updated per
	 * result. Unused (reset) while a GPU result is displayed — there the brush
	 * points directly at the producer's render target (phase-5.1).
	 */
	TStrongObjectPtr<UTexture2D> TraceTexture;

	/** Brush wrapping the current producer's density texture for MakeBox. */
	FSlateBrush TraceBrush;
};
