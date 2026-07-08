// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "UI/STrueScopeWidget.h"

#include "Analysis/TrueScopeAnalyzer.h"

namespace
{
	/**
	 * UI poll rate for new results. Faster than the ~2.5 Hz capture rate so a
	 * fresh result appears without perceptible extra latency; polling is a
	 * serial compare, so an idle tick costs nothing.
	 */
	constexpr float ResultPollPeriodSeconds = 0.1f;
}

void STrueScopeWidget::InitializeScopeWidget(const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& InAnalyzer,
	bool bInMaintainSquareAspect)
{
	Analyzer = InAnalyzer;
	bMaintainSquareAspect = bInMaintainSquareAspect;

	if (Analyzer)
	{
		RegisterActiveTimer(ResultPollPeriodSeconds,
			FWidgetActiveTimerDelegate::CreateSP(this, &STrueScopeWidget::PollAnalyzer));
	}
}

EActiveTimerReturnType STrueScopeWidget::PollAnalyzer(double InCurrentTime, float InDeltaTime)
{
	const uint64 Serial = Analyzer->GetResultSerial();
	if (Serial != LastSeenSerial)
	{
		LastSeenSerial = Serial;
		CachedResult = Analyzer->GetLatestResult();
		OnAnalysisResultChanged();
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	return EActiveTimerReturnType::Continue;
}

FVector2D STrueScopeWidget::ComputeDesiredSize(float) const
{
	// Scopes live in splitter panels that stretch them; the desired size only
	// matters as a lower bound.
	return FVector2D(64.0, 64.0);
}

FSlateRect STrueScopeWidget::GetScopeContentRect(const FGeometry& AllottedGeometry) const
{
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	if (!bMaintainSquareAspect)
	{
		return FSlateRect(0.0f, 0.0f, Size.X, Size.Y);
	}

	// Aspect-locked (vectorscope, M4): largest centered square — uniform
	// scale, never distorted, no matter how narrow or wide the panel.
	const float Side = FMath::Min(Size.X, Size.Y);
	const float Left = (Size.X - Side) * 0.5f;
	const float Top = (Size.Y - Side) * 0.5f;
	return FSlateRect(Left, Top, Left + Side, Top + Side);
}
