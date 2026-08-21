// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "UI/STrueScopeMainBar.h"

#include "Capture/TrueScopeViewExtension.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"
#include "TrueScopeModule.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "STrueScopeMainBar"

namespace
{
	FText GetMeasurementRateLabel(float RateHz)
	{
		// "2.5 Hz" reads as a rate; the exactness matters to nobody, the cadence does.
		return FText::AsNumber(RateHz, &FNumberFormattingOptions()
			.SetMinimumFractionalDigits(0)
			.SetMaximumFractionalDigits(1));
	}
}

void STrueScopeMainBar::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
		.Padding(FMargin(8.0f, 2.0f))
		[
			SNew(SHorizontalBox)

			// The bar is otherwise empty on purpose: everything a single panel
			// owns now lives in that panel's header.
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNullWidget::NullWidget
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				// Same engine pattern as the panel header's menu button (the
				// details panel's "View Options"), with the engine's own gear.
				SNew(SComboButton)
				.HasDownArrow(false)
				.ContentPadding(0.0f)
				.ForegroundColor(FSlateColor::UseForeground())
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("SettingsTooltip", "TrueScope settings"))
				.OnGetMenuContent(FOnGetContent::CreateSP(this, &STrueScopeMainBar::BuildSettingsMenu))
				.ButtonContent()
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.Settings"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
		]
	];
}

TSharedRef<SWidget> STrueScopeMainBar::BuildSettingsMenu()
{
	FMenuBuilder MenuBuilder(/*bShouldCloseWindowAfterMenuSelection*/ true, nullptr);

	// ONE switch selects the path for all four scopes: a per-scope path would
	// make the user set the same option four times (settled decision,
	// architecture.md). Neither entry is labelled "default" — performance vs.
	// compatibility is the user-facing contrast, and both must produce
	// identical values.
	MenuBuilder.BeginSection(TEXT("TrueScopeAnalysisPath"),
		LOCTEXT("AnalysisPathSection", "Analysis Path (All Scopes)"));
	MenuBuilder.AddMenuEntry(LOCTEXT("AnalysisPathGpu", "GPU (performance)"),
		LOCTEXT("AnalysisPathGpuTooltip",
			"All four scopes are analysed on the GPU. Selects the path for every scope at once."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &STrueScopeMainBar::HandleSetAnalysisPath, true),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &STrueScopeMainBar::IsAnalysisPathSelected, true)),
		NAME_None, EUserInterfaceActionType::RadioButton);
	MenuBuilder.AddMenuEntry(LOCTEXT("AnalysisPathCpu", "CPU (compatibility)"),
		LOCTEXT("AnalysisPathCpuTooltip",
			"All four scopes are analysed on the CPU. Same values as the GPU path; use it if the GPU path misbehaves on your driver."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &STrueScopeMainBar::HandleSetAnalysisPath, false),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &STrueScopeMainBar::IsAnalysisPathSelected, false)),
		NAME_None, EUserInterfaceActionType::RadioButton);
	MenuBuilder.EndSection();

	// The cadences are the shortlist declared in TrueScopeViewExtension.h, read
	// from there rather than re-listed, so the UI cannot offer a rate the
	// capture side does not implement.
	MenuBuilder.BeginSection(TEXT("TrueScopeCaptureRate"), LOCTEXT("CaptureRateSection", "Capture Rate"));
	for (float RateHz : TrueScope::MeasurementRatesHz)
	{
		MenuBuilder.AddMenuEntry(
			FText::Format(LOCTEXT("RateFormat", "{0} Hz"), GetMeasurementRateLabel(RateHz)),
			FText::Format(LOCTEXT("RateTooltip",
				"Measure at {0} Hz. A faster rate costs more; on the CPU path every measurement briefly blocks the render thread."),
				GetMeasurementRateLabel(RateHz)),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &STrueScopeMainBar::HandleSetMeasurementRate, RateHz),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &STrueScopeMainBar::IsMeasurementRateSelected, RateHz)),
			NAME_None, EUserInterfaceActionType::RadioButton);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void STrueScopeMainBar::HandleSetAnalysisPath(bool bUseGpu)
{
	FTrueScopeModule::Get().SetUseGpuAnalysis(bUseGpu);
}

bool STrueScopeMainBar::IsAnalysisPathSelected(bool bGpu) const
{
	// The EFFECTIVE path, so the popup shows what actually produced the numbers
	// on screen even while a diagnostic CVar override is in force.
	return FTrueScopeModule::Get().GetEffectiveUseGpuAnalysis() == bGpu;
}

void STrueScopeMainBar::HandleSetMeasurementRate(float RateHz)
{
	FTrueScopeModule::Get().SetMeasurementRateHz(RateHz);
}

bool STrueScopeMainBar::IsMeasurementRateSelected(float RateHz) const
{
	return FMath::IsNearlyEqual(FTrueScopeModule::Get().GetMeasurementRateHz(), RateHz);
}

#undef LOCTEXT_NAMESPACE
