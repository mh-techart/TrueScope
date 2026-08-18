// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "UI/STrueScopeGainSlider.h"

#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"

float STrueScopeGainSlider::SnapToCentre(float Position)
{
	return FMath::Abs(Position - CentrePosition) <= CentreSnapRadius ? CentrePosition : Position;
}

void STrueScopeGainSlider::Construct(const FArguments& InArgs)
{
	OnUserValueChanged = InArgs._OnValueChanged;
	OnResetRequested = InArgs._OnResetRequested;

	SSlider::Construct(SSlider::FArguments()
		.Value(InArgs._Value)
		.IndentHandle(false)
		.OnValueChanged(FOnFloatValueChanged::CreateLambda([this](float NewPosition)
		{
			OnUserValueChanged.ExecuteIfBound(SnapToCentre(NewPosition));
		})));
}

FReply STrueScopeGainSlider::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		OnResetRequested.ExecuteIfBound();
		return FReply::Handled();
	}
	return SSlider::OnMouseButtonDoubleClick(InMyGeometry, InMouseEvent);
}

int32 STrueScopeGainSlider::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// The centre tick goes UNDER the slider (drawn first, base paint returns a
	// higher layer), so the handle sitting on the default hides nothing.
	const FVector2f LocalSize = AllottedGeometry.GetLocalSize();
	constexpr float TickWidth = 1.0f;
	const FVector2f TickSize(TickWidth, LocalSize.Y);
	const FVector2f TickOffset(FMath::RoundToFloat(LocalSize.X * CentrePosition - 0.5f * TickWidth), 0.0f);

	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
		AllottedGeometry.ToPaintGeometry(TickSize, FSlateLayoutTransform(TickOffset)),
		FAppStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None,
		FLinearColor(1.0f, 1.0f, 1.0f, 0.35f));

	return SSlider::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 1,
		InWidgetStyle, bParentEnabled);
}
