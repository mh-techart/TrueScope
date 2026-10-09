// Copyright 2026 Michael Herber
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "UI/TrueScopeStatus.h"
#include "Widgets/SCompoundWidget.h"

class FTrueScopeAnalyzer;

/**
 * The tab's measurement status line (phase-6.1 Task 4, architecture.md M6):
 * what is being measured, in which display encoding, on which analysis path —
 * and, when nothing is being measured, why.
 *
 * The "why" is the point. Before this, a viewport in an orthographic view
 * produced blank scopes with no explanation (the capture hook sits after the
 * tonemap pass, which the ortho path never runs), a PIE session left the scopes
 * frozen on a held frame, and an unfocused viewport looked like a broken
 * plugin. All three now say what they are. The states line up with the scopes'
 * own in-panel hints (phase-4.1 / phase-5.2), because both are derived from the
 * same conditions.
 */
class STrueScopeStatusLine : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STrueScopeStatusLine)
		{
		}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& InAnalyzer);

	/** The status shown right now. Test seam and the source of the widget's text. */
	const TrueScope::FMeasurementStatus& GetStatus() const
	{
		return Status;
	}

protected:
	/** Is a PIE session (not Simulate) running? Virtual so a test can drive it. */
	virtual bool IsPlayInEditorActive() const;

private:
	EActiveTimerReturnType Poll(double InCurrentTime, float InDeltaTime);

	/** Results still arriving, i.e. the capture has not stalled. */
	bool AreResultsFlowing(double NowSeconds) const;

	FText GetStatusText() const;
	FSlateColor GetStatusColor() const;

	TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe> Analyzer;
	TrueScope::FMeasurementStatus Status;

	uint64 LastSeenSerial = 0;

	/** When the last result arrived; negative until the staleness clock starts. */
	double LastResultTimeSeconds = -1.0;
};
