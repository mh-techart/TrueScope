// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "UI/TrueScopeStatus.h"

#include "Editor/UnrealEdTypes.h"
#include "GameFramework/Actor.h"
#include "LevelEditorViewport.h"
#include "TrueScopeModule.h"

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
