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
