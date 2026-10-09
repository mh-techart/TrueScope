// Copyright 2026 Michael Herber
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
				// Live per-viewport OCIO display config for the status line
				// (FOpenColorIODisplayManager). Human-decided hard dependency
				// (2026-07-17): the live state has no reflection-reachable
				// mirror, and the persisted store lags in-session changes —
				// see architecture.md, "Status line: OCIO view-transform
				// name". The OCIO plugin ships with every engine install;
				// TrueScope.uplugin declares the plugin reference.
				"OpenColorIO",
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
