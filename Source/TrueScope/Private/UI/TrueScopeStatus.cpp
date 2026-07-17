// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "UI/TrueScopeStatus.h"

#include "Editor/UnrealEdTypes.h"
#include "GameFramework/Actor.h"
#include "LevelEditorViewport.h"
#include "Modules/ModuleManager.h"
#include "SLevelViewport.h"
#include "TrueScopeModule.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "TrueScopeStatus"

namespace TrueScope
{

namespace
{
	/** The view type of the measured viewport, in the editor's own words. */
	FText GetViewportTypeName(ELevelViewportType Type)
	{
		switch (Type)
		{
		case LVT_Perspective:        return LOCTEXT("ViewPerspective", "Perspective");
		case LVT_OrthoXY:            return LOCTEXT("ViewTop", "Top");
		case LVT_OrthoNegativeXY:    return LOCTEXT("ViewBottom", "Bottom");
		case LVT_OrthoXZ:            return LOCTEXT("ViewLeft", "Left");
		case LVT_OrthoNegativeXZ:    return LOCTEXT("ViewRight", "Right");
		case LVT_OrthoYZ:            return LOCTEXT("ViewBack", "Back");
		case LVT_OrthoNegativeYZ:    return LOCTEXT("ViewFront", "Front");
		case LVT_OrthoFreelook:      return LOCTEXT("ViewOrtho", "Orthographic");
		default:                     return LOCTEXT("ViewUnknown", "Viewport");
		}
	}

	FText GetPathName(bool bUseGpu)
	{
		return bUseGpu ? LOCTEXT("PathGpu", "GPU") : LOCTEXT("PathCpu", "CPU");
	}

	/** A string UPROPERTY inside a struct UPROPERTY, or empty on any miss. */
	FString ReadNestedString(const FStructProperty* StructProp, const void* Container,
		const TCHAR* FieldName)
	{
		const FStrProperty* Field =
			CastField<FStrProperty>(StructProp->Struct->FindPropertyByName(FieldName));
		return Field
			? Field->GetPropertyValue_InContainer(StructProp->ContainerPtrToValuePtr<void>(Container))
			: FString();
	}

	/**
	 * The destination name out of one FOpenColorIODisplayConfiguration value,
	 * or empty when the configuration is disabled/unset. Reflection-only.
	 */
	FString ReadOcioConfigurationName(const FStructProperty* ConfigProp, const void* Owner)
	{
		const FBoolProperty* EnabledProp =
			CastField<FBoolProperty>(ConfigProp->Struct->FindPropertyByName(TEXT("bIsEnabled")));
		const FStructProperty* ConversionProp =
			CastField<FStructProperty>(ConfigProp->Struct->FindPropertyByName(TEXT("ColorConfiguration")));
		if (!EnabledProp || !ConversionProp)
		{
			return FString();
		}

		const void* Config = ConfigProp->ContainerPtrToValuePtr<void>(Owner);
		if (!EnabledProp->GetPropertyValue_InContainer(Config))
		{
			return FString();
		}

		const FStructProperty* DisplayViewProp = CastField<FStructProperty>(
			ConversionProp->Struct->FindPropertyByName(TEXT("DestinationDisplayView")));
		const FStructProperty* DestSpaceProp = CastField<FStructProperty>(
			ConversionProp->Struct->FindPropertyByName(TEXT("DestinationColorSpace")));
		if (!DisplayViewProp || !DestSpaceProp)
		{
			return FString();
		}

		const void* Conversion = ConversionProp->ContainerPtrToValuePtr<void>(Config);
		return ComposeOcioDestinationName(
			ReadNestedString(DisplayViewProp, Conversion, TEXT("Display")),
			ReadNestedString(DisplayViewProp, Conversion, TEXT("View")),
			ReadNestedString(DestSpaceProp, Conversion, TEXT("ColorSpaceName")));
	}

	/**
	 * The active OCIO view transform of the focused level viewport, read with
	 * NO build dependency on the OpenColorIO plugin (work order 13 Fix 3 —
	 * soft path, human-approved 2026-07-17). The source is the OCIO editor
	 * module's own per-viewport store — UOpenColorIOLevelViewportSettings,
	 * keyed by SLevelViewport::GetConfigKey(), falling back to
	 * UOpenColorIODefaultViewportSettings when enabled — i.e. exactly what
	 * UOpenColorIOLevelViewportSettings::GetViewportSettings() returns to the
	 * OCIO module itself, which writes the store on every viewport-menu change
	 * (SetViewportSettings + SaveConfig), so the label stays current. Every
	 * step degrades to "no name" (plain encoding) when the plugin is absent
	 * or a future engine version renames a field — a silently missing label,
	 * never a crash. Empty result = OCIO inactive for this viewport.
	 */
	FString GetOcioDestinationName(const FLevelEditorViewportClient* Viewport)
	{
		// The settings classes live in the OCIO *editor* module; not loaded
		// means the OCIO plugin is disabled — the common case, exit cheaply.
		if (!FModuleManager::Get().IsModuleLoaded(TEXT("OpenColorIOEditor")))
		{
			return FString();
		}

		const TSharedPtr<SLevelViewport> LevelViewport =
			StaticCastSharedPtr<SLevelViewport>(Viewport->GetEditorViewportWidget());
		if (!LevelViewport.IsValid())
		{
			return FString();
		}
		const FName ConfigKey = LevelViewport->GetConfigKey();

		// Per-viewport store first (mirrors GetViewportSettings()).
		if (const UClass* SettingsClass = FindObject<UClass>(nullptr,
			TEXT("/Script/OpenColorIOEditor.OpenColorIOLevelViewportSettings")))
		{
			const UObject* Settings = SettingsClass->GetDefaultObject();
			const FArrayProperty* ArrayProp = CastField<FArrayProperty>(
				SettingsClass->FindPropertyByName(TEXT("ViewportsSettings")));
			const FStructProperty* PairProp =
				ArrayProp ? CastField<FStructProperty>(ArrayProp->Inner) : nullptr;
			if (Settings && PairProp)
			{
				const FNameProperty* IdProp = CastField<FNameProperty>(
					PairProp->Struct->FindPropertyByName(TEXT("ViewportIdentifier")));
				const FStructProperty* ConfigProp = CastField<FStructProperty>(
					PairProp->Struct->FindPropertyByName(TEXT("DisplayConfiguration")));
				if (IdProp && ConfigProp)
				{
					FScriptArrayHelper Array(ArrayProp,
						ArrayProp->ContainerPtrToValuePtr<void>(Settings));
					for (int32 Index = 0; Index < Array.Num(); ++Index)
					{
						const void* Pair = Array.GetRawPtr(Index);
						if (IdProp->GetPropertyValue_InContainer(Pair) == ConfigKey)
						{
							// An entry exists: its state decides, enabled or not
							// (a disabled entry must NOT fall through to the
							// default — GetViewportSettings returns it as-is).
							return ReadOcioConfigurationName(ConfigProp, Pair);
						}
					}
				}
			}
		}

		// No entry for this viewport: the enabled project-wide default applies.
		if (const UClass* DefaultClass = FindObject<UClass>(nullptr,
			TEXT("/Script/OpenColorIOEditor.OpenColorIODefaultViewportSettings")))
		{
			const UObject* Defaults = DefaultClass->GetDefaultObject();
			const FStructProperty* ConfigProp = CastField<FStructProperty>(
				DefaultClass->FindPropertyByName(TEXT("DefaultDisplayConfiguration")));
			if (Defaults && ConfigProp)
			{
				return ReadOcioConfigurationName(ConfigProp, Defaults);
			}
		}

		return FString();
	}
}

FString ComposeOcioDestinationName(const FString& Display, const FString& View,
	const FString& ColorSpaceName)
{
	if (!Display.IsEmpty() && !View.IsEmpty())
	{
		// FOpenColorIODisplayView::ToString's exact format.
		return FString::Printf(TEXT("%s - %s"), *Display, *View);
	}
	return ColorSpaceName;
}

FText GetDefaultDisplayEncoding()
{
	// V1 measures the display-referred SDR signal and its percentages and
	// graticule are calibrated for Rec.709/sRGB (architecture.md, "Calibration
	// note"). Naming it here is the honest thing: it tells the user what the
	// numbers on the scales mean.
	return LOCTEXT("EncodingSrgb", "sRGB / Rec.709");
}

FText FormatStatusLine(const FMeasurementStatus& Status)
{
	switch (Status.State)
	{
	case EMeasurementState::Measuring:
		return FText::Format(
			LOCTEXT("StatusMeasuring", "Measuring {0}  •  {1}  •  {2} path"),
			Status.Source, Status.DisplayEncoding, GetPathName(Status.bUseGpu));

	case EMeasurementState::PausedInPie:
		// The scopes still show the held frame, so the path stays relevant.
		return FText::Format(
			LOCTEXT("StatusPaused", "Paused — measured viewport in Play (PIE); showing the last frame  •  {0} path"),
			GetPathName(Status.bUseGpu));

	case EMeasurementState::Orthographic:
		// The logged M4.1 gap: an ortho viewport never delivers a frame, and the
		// scopes used to stay blank without explanation.
		return FText::Format(
			LOCTEXT("StatusOrtho", "No signal — orthographic view ({0}); switch the viewport to Perspective"),
			Status.Source);

	case EMeasurementState::NoViewport:
	default:
		return LOCTEXT("StatusNoViewport", "No signal — no Level Editor viewport focused");
	}
}

FMeasurementStatus GatherMeasurementStatus(bool bPlayInEditorActive, bool bResultsFlowing)
{
	check(IsInGameThread());

	FMeasurementStatus Status;
	Status.DisplayEncoding = GetDefaultDisplayEncoding();
	Status.bUseGpu = FTrueScopeModule::Get().GetEffectiveUseGpuAnalysis();

	// Exactly the viewport the capture extension measures: the last focused
	// Level Editor viewport (IsActiveThisFrame_Internal). Reading the same
	// global is what keeps the status line from claiming a different source
	// than the one that produced the numbers.
	const FLevelEditorViewportClient* Viewport = GCurrentLevelEditingViewportClient;
	if (!Viewport)
	{
		Status.State = EMeasurementState::NoViewport;
		return Status;
	}

	// A piloted actor is the truthful name of the source (piloting a Cine Camera
	// is how a camera gets measured — architecture.md, "Image source").
	const AActor* LockedActor = Viewport->GetActiveActorLock().Get();
	Status.Source = LockedActor
		? FText::FromString(LockedActor->GetActorLabel())
		: GetViewportTypeName(Viewport->GetViewportType());

	// With an OCIO view transform active on this viewport, name it — the
	// measured signal is post-OCIO (WYSIWYG), so the transform is part of what
	// the numbers mean. The V1 calibration caveat stands either way
	// (architecture.md, "Calibration note").
	const FString OcioName = GetOcioDestinationName(Viewport);
	if (!OcioName.IsEmpty())
	{
		Status.DisplayEncoding = FText::Format(
			LOCTEXT("EncodingOcio", "OCIO: {0}"), FText::FromString(OcioName));
	}

	if (!Viewport->IsPerspective())
	{
		Status.State = EMeasurementState::Orthographic;
		return Status;
	}

	// Paused is the same conjunction the scopes' hint uses (PIE running AND
	// results stopped), so status line and in-panel hint can never disagree.
	Status.State = (bPlayInEditorActive && !bResultsFlowing)
		? EMeasurementState::PausedInPie
		: EMeasurementState::Measuring;
	return Status;
}

} // namespace TrueScope

#undef LOCTEXT_NAMESPACE
