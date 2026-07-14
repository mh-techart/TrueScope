// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "UI/STrueScopeWidget.h"

#include "Analysis/TrueScopeAnalyzer.h"
#include "Capture/TrueScopeViewExtension.h"
#include "Editor.h"
#include "Framework/Application/MenuStack.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Layout/WidgetPath.h"
#include "Textures/SlateIcon.h"
#include "TrueScopeModule.h"
#include "UI/TrueScopeDraw.h"

#define LOCTEXT_NAMESPACE "STrueScopeWidget"

namespace
{
	/**
	 * UI poll rate for new results. Faster than the ~2.5 Hz capture rate so a
	 * fresh result appears without perceptible extra latency; polling is a
	 * serial compare, so an idle tick costs nothing.
	 */
	constexpr float ResultPollPeriodSeconds = 0.1f;

	/**
	 * The three things the user needs: that the scopes are paused, why, and how to
	 * resume. Deliberately says "another viewport", not "another *suitable*
	 * viewport" — whether the next one is in a scope-friendly view mode is the
	 * user's call, and TrueScope never switches source on its own
	 * (architecture.md, "No automatic fallback").
	 */
	FText GetPausedHintText()
	{
		// — is the em dash: written as an escape so the source stays ASCII and
		// cannot be mangled by a compiler's source charset.
		return LOCTEXT("PausedInPie",
			"Paused \u2014 viewport in Play (PIE).\nYou may also focus another viewport.");
	}

	/**
	 * The unified empty-state (phase-5.2): identical on all four scopes when a
	 * scope has no data to draw \u2014 no source measured yet, or the current
	 * result carries nothing for this scope.
	 */
	FText GetEmptyHintText()
	{
		return LOCTEXT("NoData",
			"No data \u2014 nothing measured yet.\nFocus a Level Editor viewport to measure.");
	}
}

void STrueScopeWidget::InitializeScopeWidget(const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& InAnalyzer,
	bool bInMaintainSquareAspect)
{
	Analyzer = InAnalyzer;
	RasterSizes = FTrueScopeModule::Get().GetRasterSizes();
	bMaintainSquareAspect = bInMaintainSquareAspect;

	if (Analyzer)
	{
		RegisterActiveTimer(ResultPollPeriodSeconds,
			FWidgetActiveTimerDelegate::CreateSP(this, &STrueScopeWidget::PollAnalyzer));
	}
}

void STrueScopeWidget::RequestRasterSize(TrueScope::ERasterTarget Target, const FGeometry& AllottedGeometry,
	FVector2f LocalSize) const
{
	if (!RasterSizes)
	{
		return; // no producer (commandlet / test); nothing rasterizes on the GPU
	}

	const FVector2f DeviceSize = LocalSize * AllottedGeometry.Scale;
	RasterSizes->Request(Target, TrueScope::ClampRasterSize(DeviceSize));
}

EActiveTimerReturnType STrueScopeWidget::PollAnalyzer(double InCurrentTime, float InDeltaTime)
{
	const uint64 Serial = Analyzer->GetResultSerial();
	if (Serial != LastSeenSerial)
	{
		LastSeenSerial = Serial;
		CachedResult = Analyzer->GetLatestResult();
		LastResultTimeSeconds = InCurrentTime;
		OnAnalysisResultChanged();
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	else if (LastResultTimeSeconds < 0.0)
	{
		// No result yet: start the staleness clock on the first tick, so a scope
		// that has never measured anything does not count as stale from time zero.
		LastResultTimeSeconds = InCurrentTime;
	}

	if (UpdateScopeState(InCurrentTime))
	{
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	return EActiveTimerReturnType::Continue;
}

bool STrueScopeWidget::IsPlayInEditorActive() const
{
	// Simulate drives runtime objects while the scopes keep measuring, so it is
	// not a paused state (architecture.md, "Behaviour during PIE").
	return GEditor != nullptr && GEditor->IsPlayingSessionInEditor() && !GEditor->IsSimulatingInEditor();
}

bool STrueScopeWidget::UpdateScopeState(double NowSeconds)
{
	const double SecondsSinceLastResult =
		LastResultTimeSeconds >= 0.0 ? NowSeconds - LastResultTimeSeconds : 0.0;

	const TrueScope::EScopeState NewState = TrueScope::DetermineScopeState(
		IsPlayInEditorActive(), SecondsSinceLastResult, TrueScope::CaptureIntervalSeconds);

	if (NewState == ScopeState)
	{
		return false;
	}

	ScopeState = NewState;
	return true;
}

int32 STrueScopeWidget::DrawPausedHint(FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FGeometry& AllottedGeometry, const FSlateRect& Content) const
{
	if (!ShouldDrawPausedHint())
	{
		return LayerId;
	}

	return TrueScope::Draw::DrawCenteredHint(OutDrawElements, LayerId, AllottedGeometry, Content,
		GetPausedHintText());
}

int32 STrueScopeWidget::DrawScopeHints(FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FGeometry& AllottedGeometry, const FSlateRect& Content) const
{
	// Paused wins: it means values are being HELD (data exists and stays
	// readable under the hint), which is more specific than "no data".
	if (ShouldDrawPausedHint())
	{
		return DrawPausedHint(OutDrawElements, LayerId, AllottedGeometry, Content);
	}

	if (!HasDrawableData())
	{
		return TrueScope::Draw::DrawCenteredHint(OutDrawElements, LayerId, AllottedGeometry, Content,
			GetEmptyHintText());
	}

	return LayerId;
}

FVector2D STrueScopeWidget::ComputeDesiredSize(float) const
{
	// Scopes live in splitter panels that stretch them; the desired size only
	// matters as a lower bound.
	return FVector2D(64.0, 64.0);
}

FSlateRect STrueScopeWidget::GetScopeContentRect(const FGeometry& AllottedGeometry) const
{
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	if (!bMaintainSquareAspect)
	{
		return FSlateRect(0.0f, 0.0f, Size.X, Size.Y);
	}

	// Aspect-locked (vectorscope, M4): largest centered square — uniform
	// scale, never distorted, no matter how narrow or wide the panel.
	const float Side = FMath::Min(Size.X, Size.Y);
	const float Left = (Size.X - Side) * 0.5f;
	const float Top = (Size.Y - Side) * 0.5f;
	return FSlateRect(Left, Top, Left + Side, Top + Side);
}

FReply STrueScopeWidget::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Capture on press so the release lands here and opens the menu.
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton && HasContextMenu())
	{
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	return FReply::Unhandled();
}

FReply STrueScopeWidget::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::RightMouseButton || !HasContextMenu())
	{
		return FReply::Unhandled();
	}

	const FWidgetPath OwnerPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
	FSlateApplication::Get().PushMenu(AsShared(), OwnerPath, BuildContextMenu(),
		MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect::ContextMenu);

	return FReply::Handled().ReleaseMouseCapture();
}

TSharedRef<SWidget> STrueScopeWidget::BuildContextMenu()
{
	FMenuBuilder MenuBuilder(/*bShouldCloseWindowAfterMenuSelection*/ true, nullptr);
	if (!GetDisplayModes().IsEmpty())
	{
		MenuBuilder.BeginSection(TEXT("TrueScopeDisplayModes"), LOCTEXT("DisplaySection", "Display"));
		for (const FTrueScopeDisplayMode& Mode : GetDisplayModes())
		{
			MenuBuilder.AddMenuEntry(Mode.Label, FText::GetEmpty(), FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &STrueScopeWidget::HandleSetDisplayMode, Mode.Id),
					FCanExecuteAction(),
					FIsActionChecked::CreateSP(this, &STrueScopeWidget::IsDisplayModeActive, Mode.Id)),
				NAME_None, EUserInterfaceActionType::RadioButton);
		}
		MenuBuilder.EndSection();
	}

	// Global analysis-path selection (phase-5.2): offered on every scope, and
	// ONE switch selects the path for all four scopes at once — a per-scope
	// path would make the user set the same option four times (decision
	// recorded in architecture.md). Neither entry is labelled "default"
	// (architecture, "Analysis path selection"): performance vs. compatibility
	// is the user-facing contrast, and both must produce identical values.
	MenuBuilder.BeginSection(TEXT("TrueScopeAnalysisPath"), LOCTEXT("AnalysisPathSection", "Analysis Path (all scopes)"));
	MenuBuilder.AddMenuEntry(LOCTEXT("AnalysisPathGpu", "GPU (performance)"), FText::GetEmpty(), FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &STrueScopeWidget::HandleSetUseGpuAnalysis, true),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &STrueScopeWidget::IsAnalysisPathActive, true)),
		NAME_None, EUserInterfaceActionType::RadioButton);
	MenuBuilder.AddMenuEntry(LOCTEXT("AnalysisPathCpu", "CPU (compatibility)"), FText::GetEmpty(), FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &STrueScopeWidget::HandleSetUseGpuAnalysis, false),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &STrueScopeWidget::IsAnalysisPathActive, false)),
		NAME_None, EUserInterfaceActionType::RadioButton);
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void STrueScopeWidget::HandleSetUseGpuAnalysis(bool bUseGpu)
{
	FTrueScopeModule::Get().SetUseGpuAnalysis(bUseGpu);
}

bool STrueScopeWidget::IsAnalysisPathActive(bool bGpu) const
{
	return FTrueScopeModule::Get().GetUseGpuAnalysis() == bGpu;
}

void STrueScopeWidget::HandleSetDisplayMode(int32 ModeId)
{
	SetDisplayModeId(ModeId);
	Invalidate(EInvalidateWidgetReason::Paint);
}

bool STrueScopeWidget::IsDisplayModeActive(int32 ModeId) const
{
	return GetDisplayModeId() == ModeId;
}

#undef LOCTEXT_NAMESPACE
