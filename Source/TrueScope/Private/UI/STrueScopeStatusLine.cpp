// Copyright 2026 Michael Herber
// SPDX-License-Identifier: Apache-2.0

#include "UI/STrueScopeStatusLine.h"

#include "Analysis/TrueScopeAnalyzer.h"
#include "Editor.h"
#include "Styling/AppStyle.h"
#include "TrueScopeModule.h"
#include "UI/TrueScopeDraw.h"
#include "UI/TrueScopeScopeState.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "STrueScopeStatusLine"

namespace
{
	/** Same cadence as the scopes' result poll: cheap, and no visible lag. */
	constexpr float StatusPollPeriodSeconds = 0.1f;
}

void STrueScopeStatusLine::Construct(const FArguments& InArgs,
	const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& InAnalyzer)
{
	Analyzer = InAnalyzer;

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
		.Padding(FMargin(8.0f, 3.0f))
		[
			SNew(STextBlock)
			.Text(this, &STrueScopeStatusLine::GetStatusText)
			.ColorAndOpacity(this, &STrueScopeStatusLine::GetStatusColor)
			.TextStyle(FAppStyle::Get(), "SmallText")
		]
	];

	RegisterActiveTimer(StatusPollPeriodSeconds,
		FWidgetActiveTimerDelegate::CreateSP(this, &STrueScopeStatusLine::Poll));
}

bool STrueScopeStatusLine::IsPlayInEditorActive() const
{
	// Simulate keeps measuring, so it is not a paused state (architecture.md).
	return GEditor != nullptr && GEditor->IsPlayingSessionInEditor() && !GEditor->IsSimulatingInEditor();
}

bool STrueScopeStatusLine::AreResultsFlowing(double NowSeconds) const
{
	if (LastResultTimeSeconds < 0.0)
	{
		return false;
	}

	// The scopes' own staleness rule, in the same units (capture intervals), so
	// the status line flips to Paused exactly when the in-panel hints do.
	const double StaleAfter =
		TrueScope::PausedAfterMissedIntervals * FTrueScopeModule::Get().GetCaptureIntervalSeconds();
	return NowSeconds - LastResultTimeSeconds < StaleAfter;
}

EActiveTimerReturnType STrueScopeStatusLine::Poll(double InCurrentTime, float InDeltaTime)
{
	if (Analyzer)
	{
		const uint64 Serial = Analyzer->GetResultSerial();
		if (Serial != LastSeenSerial)
		{
			LastSeenSerial = Serial;
			LastResultTimeSeconds = InCurrentTime;
		}
	}

	Status = TrueScope::GatherMeasurementStatus(IsPlayInEditorActive(), AreResultsFlowing(InCurrentTime));
	return EActiveTimerReturnType::Continue;
}

FText STrueScopeStatusLine::GetStatusText() const
{
	return TrueScope::FormatStatusLine(Status);
}

FSlateColor STrueScopeStatusLine::GetStatusColor() const
{
	// Measuring is the quiet, normal case; anything else is the amber the
	// in-panel hints already use, so "not measuring" reads the same everywhere.
	return TrueScope::IsMeasuring(Status)
		? FSlateColor::UseSubduedForeground()
		: FSlateColor(TrueScope::Draw::HintTextColor);
}

#undef LOCTEXT_NAMESPACE
