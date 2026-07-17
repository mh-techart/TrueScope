// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

using UnrealBuildTool;

public class TrueScope : ModuleRules
{
	public TrueScope(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"InputCore",
				// SLevelViewport::GetConfigKey — the key of the OCIO plugin's
				// per-viewport store, read by reflection with no OCIO
				// dependency (work order 13 Fix 3). Engine editor module,
				// present in every editor build.
				"LevelEditor",
				"Projects",
				"RHI",
				"RenderCore",
				"Renderer",
				"Slate",
				"SlateCore",
				"UnrealEd",
				"WorkspaceMenuStructure",
			}
		);
	}
}
