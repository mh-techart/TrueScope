// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/**
 * Root content widget of the TrueScope tab: a 2x2 splitter grid holding four
 * scope panels — Waveform, Parade (top) and Vectorscope, Histogram (bottom),
 * built through the scope-type factory. M3: Parade and Histogram are live
 * (driven by the CPU analyzer); Waveform and Vectorscope remain factory
 * placeholders until M4.
 */
class STrueScopePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STrueScopePanel)
		{
		}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
