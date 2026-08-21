// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"

#include <atomic>

/**
 * Gates capture on TrueScope tab visibility (M5): the panel reports every
 * paint, and capture is allowed only while a panel has painted recently. Paint
 * is the truthful "visible and drawn" signal — a closed tab, a tab hidden
 * behind another docked tab and a minimized floating window all stop painting
 * (a dock stack paints only its foreground tab's content), while re-showing
 * the tab paints immediately, so capture resumes within one throttle interval.
 * No deadlock while visible: the scope widgets' active timers keep Slate awake
 * and executing paints (timers run inside SWidget::Paint).
 *
 * Shared by the module between the panel (writer, game thread paint) and the
 * capture view extension (reader, game thread IsActiveThisFrame); atomic so a
 * future caller from another thread cannot race.
 */
class FTrueScopeCaptureGate
{
public:
	/**
	 * How long after the last panel paint capture stays allowed. Generously
	 * above one editor frame at low FPS, well below "user left the tab": worst
	 * case one extra capture after hiding the tab.
	 */
	static constexpr double VisibilityWindowSeconds = 1.0;

	/** Called by every STrueScopeTab::OnPaint. */
	void NotifyPanelPainted(double NowSeconds)
	{
		LastPaintSeconds.store(NowSeconds, std::memory_order_relaxed);
	}

	/** True while some TrueScope panel painted within the visibility window. */
	bool ShouldCapture(double NowSeconds) const
	{
		const double LastPaint = LastPaintSeconds.load(std::memory_order_relaxed);
		return LastPaint >= 0.0 && NowSeconds - LastPaint <= VisibilityWindowSeconds;
	}

private:
	/** Time of the most recent panel paint; negative until the first paint. */
	std::atomic<double> LastPaintSeconds{-1.0};
};
