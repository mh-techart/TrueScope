// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "TrueScopeSettings.h"

#include "UI/TrueScopeType.h"

UTrueScopeSettings& UTrueScopeSettings::Get()
{
	// The class-default object IS the settings instance for a config class —
	// the engine's own tool settings are read exactly this way.
	return *GetMutableDefault<UTrueScopeSettings>();
}

namespace TrueScope
{

TArrayView<const ETrueScopeType> GetDefaultPanelTypes()
{
	static const ETrueScopeType Types[NumPanels] = {
		ETrueScopeType::Waveform, ETrueScopeType::Parade,
		ETrueScopeType::Vectorscope, ETrueScopeType::Histogram};
	return Types;
}

FTrueScopePanelState MakeDefaultPanelState(ETrueScopeType Type)
{
	FTrueScopePanelState State;
	State.ScopeType = GetScopeTypeKey(Type);
	return State;
}

TArray<FTrueScopePanelState> MakeDefaultPanelStates()
{
	// Read off STrueScopePanel's grid, not assumed: top row Waveform | Parade,
	// bottom row Vectorscope | Histogram.
	TArray<FTrueScopePanelState> States;
	States.Reserve(NumPanels);
	for (const ETrueScopeType Type : GetDefaultPanelTypes())
	{
		States.Add(MakeDefaultPanelState(Type));
	}
	return States;
}

} // namespace TrueScope

void UTrueScopeSettings::EnsurePanelStates()
{
	if (PanelStates.Num() == TrueScope::NumPanels)
	{
		return;
	}

	const TArray<FTrueScopePanelState> Defaults = TrueScope::MakeDefaultPanelStates();
	if (PanelStates.Num() > TrueScope::NumPanels)
	{
		PanelStates.SetNum(TrueScope::NumPanels);
	}
	while (PanelStates.Num() < TrueScope::NumPanels)
	{
		PanelStates.Add(Defaults[PanelStates.Num()]);
	}

	// A state whose type key the ini never carried (or carried misspelled) would
	// otherwise leave the panel typeless; give it the default type of its slot.
	for (int32 Index = 0; Index < TrueScope::NumPanels; ++Index)
	{
		if (PanelStates[Index].ScopeType.IsNone())
		{
			PanelStates[Index].ScopeType = Defaults[Index].ScopeType;
		}
	}
}

const FTrueScopePanelState& UTrueScopeSettings::GetPanelState(int32 PanelIndex)
{
	EnsurePanelStates();
	return PanelStates[FMath::Clamp(PanelIndex, 0, TrueScope::NumPanels - 1)];
}

ETrueScopeType UTrueScopeSettings::GetPanelScopeType(int32 PanelIndex)
{
	const FName Key = GetPanelState(PanelIndex).ScopeType;
	for (const ETrueScopeType Type : TrueScope::GetDefaultPanelTypes())
	{
		if (TrueScope::GetScopeTypeKey(Type) == Key)
		{
			return Type;
		}
	}

	// An unknown key can only come from a hand-edited or newer ini; fall back to
	// the type this slot has by default rather than refusing to build the panel.
	return TrueScope::GetDefaultPanelTypes()[FMath::Clamp(PanelIndex, 0, TrueScope::NumPanels - 1)];
}

namespace
{
	/**
	 * The one place the widgets' keyed option API meets the panel state's named
	 * fields. A key the panel state has no field for is ignored on write and
	 * reads back as the field's default — a scope can therefore offer an option
	 * this store does not know without corrupting anything, and adding the field
	 * later is a one-line change here.
	 */
	bool* FindTogglePtr(FTrueScopePanelState& State, FName Key)
	{
		return Key == TrueScope::SkinToneLineOptionKey ? &State.bShowSkinToneLine : nullptr;
	}

	int32* FindOptionGroupPtr(FTrueScopePanelState& State, FName Key)
	{
		return Key == TrueScope::VectorscopeMarkersOptionKey ? &State.VectorscopeMarkers : nullptr;
	}

	float* FindGainPtr(FTrueScopePanelState& State, FName Key)
	{
		if (Key == TrueScope::TraceBrightnessGainKey)
		{
			return &State.TraceBrightness;
		}
		if (Key == TrueScope::HistogramGainKey)
		{
			return &State.HistogramGain;
		}
		return nullptr;
	}
}

int32 UTrueScopeSettings::GetPanelDisplayMode(int32 PanelIndex)
{
	return GetPanelState(PanelIndex).DisplayMode;
}

void UTrueScopeSettings::SetPanelDisplayMode(int32 PanelIndex, int32 ModeId)
{
	EnsurePanelStates();
	FTrueScopePanelState& State = PanelStates[FMath::Clamp(PanelIndex, 0, TrueScope::NumPanels - 1)];
	if (State.DisplayMode == ModeId)
	{
		return; // unchanged; do not rewrite the ini on every repaint-triggering click
	}

	State.DisplayMode = ModeId;
	SaveConfig();
}

bool UTrueScopeSettings::GetPanelToggleOption(int32 PanelIndex, FName Key)
{
	FTrueScopePanelState State = GetPanelState(PanelIndex);
	const bool* Value = FindTogglePtr(State, Key);
	return Value ? *Value : false;
}

void UTrueScopeSettings::SetPanelToggleOption(int32 PanelIndex, FName Key, bool bValue)
{
	EnsurePanelStates();
	FTrueScopePanelState& State = PanelStates[FMath::Clamp(PanelIndex, 0, TrueScope::NumPanels - 1)];
	bool* Field = FindTogglePtr(State, Key);
	if (!Field || *Field == bValue)
	{
		return;
	}

	*Field = bValue;
	SaveConfig();
}

int32 UTrueScopeSettings::GetPanelOptionGroupValue(int32 PanelIndex, FName Key)
{
	FTrueScopePanelState State = GetPanelState(PanelIndex);
	const int32* Value = FindOptionGroupPtr(State, Key);
	return Value ? *Value : 0;
}

void UTrueScopeSettings::SetPanelOptionGroupValue(int32 PanelIndex, FName Key, int32 ValueId)
{
	EnsurePanelStates();
	FTrueScopePanelState& State = PanelStates[FMath::Clamp(PanelIndex, 0, TrueScope::NumPanels - 1)];
	int32* Field = FindOptionGroupPtr(State, Key);
	if (!Field || *Field == ValueId)
	{
		return;
	}

	*Field = ValueId;
	SaveConfig();
}

float UTrueScopeSettings::GetPanelGainMultiplier(int32 PanelIndex, FName GainKey)
{
	FTrueScopePanelState State = GetPanelState(PanelIndex);
	const float* Value = FindGainPtr(State, GainKey);
	return Value ? *Value : TrueScope::DefaultGainMultiplier;
}

void UTrueScopeSettings::SetPanelGainMultiplier(int32 PanelIndex, FName GainKey, float Value)
{
	EnsurePanelStates();
	FTrueScopePanelState& State = PanelStates[FMath::Clamp(PanelIndex, 0, TrueScope::NumPanels - 1)];
	float* Field = FindGainPtr(State, GainKey);
	// A slider drags through many values, so an exact-equality guard would let
	// nearly every mouse move rewrite the ini; compare with a tolerance.
	if (!Field || FMath::IsNearlyEqual(*Field, Value, UE_KINDA_SMALL_NUMBER))
	{
		return;
	}

	*Field = Value;
	SaveConfig();
}

void UTrueScopeSettings::ResetPanelToDefaults(int32 PanelIndex)
{
	EnsurePanelStates();
	FTrueScopePanelState& State = PanelStates[FMath::Clamp(PanelIndex, 0, TrueScope::NumPanels - 1)];

	// The scope type is what the panel IS, not one of its display options, so a
	// reset keeps it — and with it, which menu the user is looking at.
	const FName KeptType = State.ScopeType;
	State = FTrueScopePanelState();
	State.ScopeType = KeptType;
	SaveConfig();
}
