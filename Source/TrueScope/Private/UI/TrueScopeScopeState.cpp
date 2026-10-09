// Copyright 2026 Michael Herber
// SPDX-License-Identifier: Apache-2.0

#include "UI/TrueScopeScopeState.h"

namespace TrueScope
{

EScopeState DetermineScopeState(bool bPlayInEditorActive, double SecondsSinceLastResult,
	double CaptureIntervalSeconds)
{
	if (!bPlayInEditorActive)
	{
		return EScopeState::Live;
	}

	const double StaleAfterSeconds = PausedAfterMissedIntervals * CaptureIntervalSeconds;
	return SecondsSinceLastResult > StaleAfterSeconds ? EScopeState::PausedInPie : EScopeState::Live;
}

} // namespace TrueScope
