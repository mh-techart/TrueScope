// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"
#include "TrueScopeSettings.h"
#include "UI/TrueScopeType.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * M6.4 Task 2: settings are owned by the PANEL, not by the scope type.
 *
 * The distinction is invisible today because each type appears exactly once in
 * the default 2x2 grid — which is precisely why it needs a test. If the storage
 * silently reverted to being keyed by scope type, the grid would still look
 * right, and the first time it would be noticed is when a later phase lets two
 * panels show the same scope and they moved together.
 *
 * Everything below writes into the real settings object, so each block restores
 * what it found: the automation suite must not rewrite the user's ini.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopePanelStateIndependenceTest,
	"TrueScope.UI.PanelStateIndependence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopePanelStateIndependenceTest::RunTest(const FString&)
{
	UTrueScopeSettings& Settings = UTrueScopeSettings::Get();

	// The default arrangement is the M4 grid, stated once and read here rather
	// than re-listed: Waveform | Parade over Vectorscope | Histogram.
	const TArrayView<const ETrueScopeType> DefaultTypes = TrueScope::GetDefaultPanelTypes();
	TestEqual(TEXT("The default arrangement covers every panel"), DefaultTypes.Num(), TrueScope::NumPanels);
	for (int32 PanelIndex = 0; PanelIndex < TrueScope::NumPanels; ++PanelIndex)
	{
		TestTrue(TEXT("Each panel's type is the default arrangement's"),
			Settings.GetPanelScopeType(PanelIndex) == DefaultTypes[PanelIndex]);
	}

	// --- The independence itself ------------------------------------------
	// Panels 0 and 1 both show a density scope, so both carry a Trace
	// Brightness. Writing one must leave the other exactly where it was. Panel 1
	// is deliberately given a value FIRST, so "unchanged" means "still the value
	// we put there", not "still the default" — a shared store would show up
	// either way, but this way the assertion also fails if the write goes to the
	// wrong panel.
	constexpr int32 PanelA = 0;
	constexpr int32 PanelB = 1;
	const float SavedA = Settings.GetPanelGainMultiplier(PanelA, TrueScope::TraceBrightnessGainKey);
	const float SavedB = Settings.GetPanelGainMultiplier(PanelB, TrueScope::TraceBrightnessGainKey);

	constexpr float ValueB = 0.5f;
	constexpr float ValueA = 4.0f;
	Settings.SetPanelGainMultiplier(PanelB, TrueScope::TraceBrightnessGainKey, ValueB);
	Settings.SetPanelGainMultiplier(PanelA, TrueScope::TraceBrightnessGainKey, ValueA);

	TestEqual(TEXT("Panel A holds what was written to panel A"),
		Settings.GetPanelGainMultiplier(PanelA, TrueScope::TraceBrightnessGainKey), ValueA, 1e-6f);
	TestEqual(TEXT("Panel B is untouched by panel A's write"),
		Settings.GetPanelGainMultiplier(PanelB, TrueScope::TraceBrightnessGainKey), ValueB, 1e-6f);

	// The same for the other kinds of per-panel option, so a regression in the
	// key-to-field mapping cannot hide in the ones the gain test does not reach.
	const int32 SavedModeA = Settings.GetPanelDisplayMode(PanelA);
	const int32 SavedModeB = Settings.GetPanelDisplayMode(PanelB);
	Settings.SetPanelDisplayMode(PanelB, 0);
	Settings.SetPanelDisplayMode(PanelA, 1);
	TestEqual(TEXT("Panel A holds its own display mode"), Settings.GetPanelDisplayMode(PanelA), 1);
	TestEqual(TEXT("Panel B keeps its display mode"), Settings.GetPanelDisplayMode(PanelB), 0);

	// Toggles and secondary radio groups live on the vectorscope panel; panel 3
	// carries the same fields unused, which is exactly what makes them a useful
	// second address for the mapping.
	constexpr int32 PanelC = 2;
	constexpr int32 PanelD = 3;
	const bool bSavedSkinC = Settings.GetPanelToggleOption(PanelC, TrueScope::SkinToneLineOptionKey);
	const bool bSavedSkinD = Settings.GetPanelToggleOption(PanelD, TrueScope::SkinToneLineOptionKey);
	Settings.SetPanelToggleOption(PanelD, TrueScope::SkinToneLineOptionKey, true);
	Settings.SetPanelToggleOption(PanelC, TrueScope::SkinToneLineOptionKey, false);
	TestTrue(TEXT("Panel C holds its own toggle"),
		Settings.GetPanelToggleOption(PanelC, TrueScope::SkinToneLineOptionKey) == false);
	TestTrue(TEXT("Panel D keeps its toggle"),
		Settings.GetPanelToggleOption(PanelD, TrueScope::SkinToneLineOptionKey) == true);

	const int32 SavedMarkersC = Settings.GetPanelOptionGroupValue(PanelC, TrueScope::VectorscopeMarkersOptionKey);
	const int32 SavedMarkersD = Settings.GetPanelOptionGroupValue(PanelD, TrueScope::VectorscopeMarkersOptionKey);
	Settings.SetPanelOptionGroupValue(PanelD, TrueScope::VectorscopeMarkersOptionKey, 0);
	Settings.SetPanelOptionGroupValue(PanelC, TrueScope::VectorscopeMarkersOptionKey, 2);
	TestEqual(TEXT("Panel C holds its own marker set"),
		Settings.GetPanelOptionGroupValue(PanelC, TrueScope::VectorscopeMarkersOptionKey), 2);
	TestEqual(TEXT("Panel D keeps its marker set"),
		Settings.GetPanelOptionGroupValue(PanelD, TrueScope::VectorscopeMarkersOptionKey), 0);

	// --- Reset to default (M6.4 Task 6) reads the central defaults ---------
	// Panel C above has been moved off every default; resetting it must bring
	// all of them back at once, keep its scope type, and touch nothing else.
	const FTrueScopePanelState Defaults = TrueScope::MakeDefaultPanelState(Settings.GetPanelScopeType(PanelC));
	Settings.SetPanelGainMultiplier(PanelC, TrueScope::TraceBrightnessGainKey, 8.0f);
	Settings.ResetPanelToDefaults(PanelC);

	TestTrue(TEXT("Reset keeps the panel's scope type"),
		Settings.GetPanelScopeType(PanelC) == TrueScope::GetDefaultPanelTypes()[PanelC]);
	TestEqual(TEXT("Reset restores the default display mode"),
		Settings.GetPanelDisplayMode(PanelC), Defaults.DisplayMode);
	TestEqual(TEXT("Reset restores the default trace brightness"),
		Settings.GetPanelGainMultiplier(PanelC, TrueScope::TraceBrightnessGainKey), Defaults.TraceBrightness, 1e-6f);
	TestEqual(TEXT("Reset restores the default marker set"),
		Settings.GetPanelOptionGroupValue(PanelC, TrueScope::VectorscopeMarkersOptionKey), Defaults.VectorscopeMarkers);
	TestTrue(TEXT("Reset restores the default skin tone line"),
		Settings.GetPanelToggleOption(PanelC, TrueScope::SkinToneLineOptionKey) == Defaults.bShowSkinToneLine);
	TestEqual(TEXT("Reset leaves the neighbouring panel alone"),
		Settings.GetPanelOptionGroupValue(PanelD, TrueScope::VectorscopeMarkersOptionKey), 0);

	// Leave the user's configuration as it was found.
	Settings.SetPanelGainMultiplier(PanelA, TrueScope::TraceBrightnessGainKey, SavedA);
	Settings.SetPanelGainMultiplier(PanelB, TrueScope::TraceBrightnessGainKey, SavedB);
	Settings.SetPanelDisplayMode(PanelA, SavedModeA);
	Settings.SetPanelDisplayMode(PanelB, SavedModeB);
	Settings.SetPanelToggleOption(PanelC, TrueScope::SkinToneLineOptionKey, bSavedSkinC);
	Settings.SetPanelToggleOption(PanelD, TrueScope::SkinToneLineOptionKey, bSavedSkinD);
	Settings.SetPanelOptionGroupValue(PanelC, TrueScope::VectorscopeMarkersOptionKey, SavedMarkersC);
	Settings.SetPanelOptionGroupValue(PanelD, TrueScope::VectorscopeMarkersOptionKey, SavedMarkersD);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
