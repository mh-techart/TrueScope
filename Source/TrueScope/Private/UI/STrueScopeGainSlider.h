// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Input/SSlider.h"

/**
 * The gain slider used by the toolbar (M6.3): a plain SSlider in slider-POSITION
 * space [0,1], plus the three behaviours the phase spec asks for and SSlider
 * does not provide.
 *
 *  - The centre is MARKED (a tick drawn under the handle) and SNAPS, because
 *    the centre is not a taste value: it is the scope's calibrated default —
 *    the one position at which TrueScope agrees with the reference. A user who
 *    has dragged the slider must be able to get exactly back to it, and "near
 *    the middle" is not the same setting as the middle.
 *  - Double-click resets to that centre, the conventional gesture for "back to
 *    default" in an editor slider.
 *  - No numeric readout, deliberately (settled decision, architecture.md "Gain
 *    slider law"): the number would be a brightness multiplier with no unit an
 *    artist could act on, and printing one invites reading it as a measurement.
 *    The scale marks stay the only numbers on a scope.
 *
 * The position -> gain mapping itself is NOT here: it lives in
 * TrueScope::SliderPositionToGain so both the toolbar and the tests use one
 * definition of the law.
 */
class STrueScopeGainSlider : public SSlider
{
public:
	SLATE_BEGIN_ARGS(STrueScopeGainSlider)
		: _Value(0.5f)
		{
		}

		/** Slider position in [0,1]; 0.5 is the calibrated default. */
		SLATE_ATTRIBUTE(float, Value)

		/** Called with the (already centre-snapped) position while dragging. */
		SLATE_EVENT(FOnFloatValueChanged, OnValueChanged)

		/** Called on double-click; the toolbar restores the default gain. */
		SLATE_EVENT(FSimpleDelegate, OnResetRequested)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	//~ Begin SWidget interface
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	//~ End SWidget interface

	/** Position the centre detent sits at, and the half-width of its capture zone. */
	static constexpr float CentrePosition = 0.5f;
	static constexpr float CentreSnapRadius = 0.02f;

	/** Snap to the exact centre inside the detent; identity elsewhere. */
	static float SnapToCentre(float Position);

private:
	FOnFloatValueChanged OnUserValueChanged;
	FSimpleDelegate OnResetRequested;
};
