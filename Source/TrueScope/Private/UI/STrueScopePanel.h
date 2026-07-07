// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SCompoundWidget.h"

/**
 * Root content widget of the TrueScope tab: a 2x2 splitter grid holding the
 * four scope panels — Waveform, Parade (top) and Vectorscope, Histogram
 * (bottom). Phase 2: the Waveform slot temporarily shows the raw capture
 * preview proving the M2 capture pipeline; the other three remain
 * placeholders until M3+.
 */
class STrueScopePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STrueScopePanel)
		{
		}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	static TSharedRef<SWidget> MakePlaceholderPanel(const FText& ScopeName);

	/** Throwaway M2 debug panel: shows the raw capture render target. */
	TSharedRef<SWidget> MakeCapturePreviewPanel();

	/** Brush wrapping the capture render target for the preview SImage. */
	FSlateBrush CapturePreviewBrush;
};
