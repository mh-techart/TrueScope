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

int32 UTrueScopeSettings::GetScopeDisplayMode(ETrueScopeType Type) const
{
	const int32* Mode = ScopeDisplayModes.Find(TrueScope::GetScopeTypeKey(Type));
	return Mode ? *Mode : INDEX_NONE;
}

void UTrueScopeSettings::SetScopeDisplayMode(ETrueScopeType Type, int32 ModeId)
{
	const FName Key = TrueScope::GetScopeTypeKey(Type);
	if (const int32* Existing = ScopeDisplayModes.Find(Key); Existing && *Existing == ModeId)
	{
		return; // unchanged; do not rewrite the ini on every repaint-triggering click
	}

	ScopeDisplayModes.Add(Key, ModeId);
	SaveConfig();
}

namespace
{
	FName MakeToggleKey(ETrueScopeType Type, FName Key)
	{
		return FName(TrueScope::GetScopeTypeKey(Type).ToString() + TEXT(".") + Key.ToString());
	}
}

bool UTrueScopeSettings::GetScopeToggleOption(ETrueScopeType Type, FName Key, bool& OutValue) const
{
	if (const bool* Value = ScopeToggleOptions.Find(MakeToggleKey(Type, Key)))
	{
		OutValue = *Value;
		return true;
	}
	return false;
}

void UTrueScopeSettings::SetScopeToggleOption(ETrueScopeType Type, FName Key, bool bValue)
{
	const FName FullKey = MakeToggleKey(Type, Key);
	if (const bool* Existing = ScopeToggleOptions.Find(FullKey); Existing && *Existing == bValue)
	{
		return; // unchanged; do not rewrite the ini on every repaint-triggering click
	}

	ScopeToggleOptions.Add(FullKey, bValue);
	SaveConfig();
}

bool UTrueScopeSettings::GetScopeOptionGroupValue(ETrueScopeType Type, FName Key, int32& OutValue) const
{
	if (const int32* Value = ScopeOptionGroupValues.Find(MakeToggleKey(Type, Key)))
	{
		OutValue = *Value;
		return true;
	}
	return false;
}

void UTrueScopeSettings::SetScopeOptionGroupValue(ETrueScopeType Type, FName Key, int32 ValueId)
{
	const FName FullKey = MakeToggleKey(Type, Key);
	if (const int32* Existing = ScopeOptionGroupValues.Find(FullKey); Existing && *Existing == ValueId)
	{
		return; // unchanged; do not rewrite the ini on every repaint-triggering click
	}

	ScopeOptionGroupValues.Add(FullKey, ValueId);
	SaveConfig();
}
