// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "UI/TrueScopeType.h"
#include "Widgets/SCompoundWidget.h"

class FTrueScopeAnalyzer;
class STrueScopeWidget;

/**
 * One panel of the TrueScope tab (M6.4): a panel header above a scope canvas.
 *
 * Terminology (M6.4, docs/phase-6.4.md): the *panel* is one scope area of the
 * tab — four of them in today's fixed 2x2 grid; the *panel header* is the small
 * always-visible row at its top; the *panel menu* opens from that header and
 * carries the options of this one panel; the *scope canvas* is the drawing
 * surface below.
 *
 * The header's label and the panel menu's contents both come from the panel's
 * stored scope type rather than from anything hardcoded here, so the type
 * switcher a later phase adds to the header is an addition, not a rewrite.
 */
class STrueScopePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STrueScopePanel)
		: _PanelIndex(0)
		{
		}

		/** Slot of the 2x2 grid; what this panel's settings are keyed by. */
		SLATE_ARGUMENT(int32, PanelIndex)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& InAnalyzer);

	/** The scope this panel shows; null for a type without a widget yet. */
	TSharedPtr<STrueScopeWidget> GetScopeWidget() const
	{
		return ScopeWidget;
	}

	ETrueScopeType GetScopeType() const
	{
		return ScopeType;
	}

	/**
	 * The panel menu, built by the scope itself so the header's menu button and
	 * the canvas's right-click menu can never list different options. Public so
	 * a test can compare the two entry points.
	 */
	TSharedRef<SWidget> BuildPanelMenu();

private:
	TSharedPtr<STrueScopeWidget> ScopeWidget;
	ETrueScopeType ScopeType = ETrueScopeType::Waveform;
	int32 PanelIndex = 0;
};
