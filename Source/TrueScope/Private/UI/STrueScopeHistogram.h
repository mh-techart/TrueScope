// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "UI/STrueScopeWidget.h"

class FTrueScopeAnalyzer;

/**
 * RGB histogram scope: per-channel pixel-count curves over value bins,
 * x axis 0-100 % (display-referred), drawn as translucent anti-aliased
 * filled curves (vector, not texture). Responsive: fills its panel.
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
};
