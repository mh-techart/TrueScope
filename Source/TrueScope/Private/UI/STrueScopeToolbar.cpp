// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "UI/STrueScopeToolbar.h"

#include "Capture/TrueScopeViewExtension.h"
#include "Styling/AppStyle.h"
#include "TrueScopeModule.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "STrueScopeToolbar"

namespace
{
	/** Editor-theme spacing; the toolbar is chrome, so nothing here scales. */
	constexpr float GroupSpacing = 12.0f;
	constexpr float ButtonSpacing = 2.0f;

	FText GetMeasurementRateLabel(float RateHz)
	{
		// "2.5 Hz" reads as a rate; the exactness matters to nobody, the cadence does.
		return FText::AsNumber(RateHz, &FNumberFormattingOptions()
			.SetMinimumFractionalDigits(0)
			.SetMaximumFractionalDigits(1));
	}
}

void STrueScopeToolbar::Construct(const FArguments& InArgs)
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);

	Row->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, GroupSpacing, 0.0f)
		[
			MakeGroup(LOCTEXT("AnalysisLabel", "Analysis"), MakeAnalysisPathControl())
		];

	Row->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			MakeGroup(LOCTEXT("RateLabel", "Rate"), MakeMeasurementRateControl())
		];

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
		.Padding(FMargin(8.0f, 4.0f))
		[
			Row
		]
	];
}

TSharedRef<SWidget> STrueScopeToolbar::MakeGroup(const FText& Label, TSharedRef<SWidget> Content)
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(Label)
			.TextStyle(FAppStyle::Get(), "SmallText")
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			Content
		];
}

TSharedRef<SWidget> STrueScopeToolbar::MakeOptionButton(const FText& Label, const FText& Tooltip,
	FSimpleDelegate OnSelected, TAttribute<bool> IsSelected)
{
	return SNew(SCheckBox)
		.Style(FAppStyle::Get(), "DetailsView.SectionButton")
		.ToolTipText(Tooltip)
		.IsChecked_Lambda([IsSelected]()
		{
			return IsSelected.Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([OnSelected](ECheckBoxState NewState)
		{
			// Radio behaviour: clicking the active option is a no-op rather than
			// a way to end up with no analysis path or no display mode at all.
			if (NewState == ECheckBoxState::Checked)
			{
				OnSelected.ExecuteIfBound();
			}
		})
		[
			SNew(STextBlock)
			.Text(Label)
			.TextStyle(FAppStyle::Get(), "SmallText")
		];
}

TSharedRef<SWidget> STrueScopeToolbar::MakeAnalysisPathControl()
{
	// Neither entry is labelled "default" (architecture.md, "Analysis path
	// selection"): performance vs. compatibility is the user-facing contrast,
	// and both paths must produce identical values.
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, ButtonSpacing, 0.0f)
		[
			MakeOptionButton(LOCTEXT("AnalysisGpu", "GPU"),
				LOCTEXT("AnalysisGpuTooltip",
					"GPU (performance): all four scopes are analysed on the GPU. Selects the path for every scope at once."),
				FSimpleDelegate::CreateSP(this, &STrueScopeToolbar::HandleSetAnalysisPath, true),
				TAttribute<bool>::CreateSP(this, &STrueScopeToolbar::IsAnalysisPathSelected, true))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			MakeOptionButton(LOCTEXT("AnalysisCpu", "CPU"),
				LOCTEXT("AnalysisCpuTooltip",
					"CPU (compatibility): all four scopes are analysed on the CPU. Same values as the GPU path; use it if the GPU path misbehaves on your driver."),
				FSimpleDelegate::CreateSP(this, &STrueScopeToolbar::HandleSetAnalysisPath, false),
				TAttribute<bool>::CreateSP(this, &STrueScopeToolbar::IsAnalysisPathSelected, false))
		];
}

TSharedRef<SWidget> STrueScopeToolbar::MakeMeasurementRateControl()
{
	TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);

	for (float RateHz : TrueScope::MeasurementRatesHz)
	{
		Box->AddSlot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, ButtonSpacing, 0.0f)
			[
				MakeOptionButton(FText::Format(LOCTEXT("RateFormat", "{0} Hz"), GetMeasurementRateLabel(RateHz)),
					FText::Format(LOCTEXT("RateTooltip",
						"Measure at {0} Hz. A faster rate costs more; on the CPU path every measurement briefly blocks the render thread."),
						GetMeasurementRateLabel(RateHz)),
					FSimpleDelegate::CreateSP(this, &STrueScopeToolbar::HandleSetMeasurementRate, RateHz),
					TAttribute<bool>::CreateSP(this, &STrueScopeToolbar::IsMeasurementRateSelected, RateHz))
			];
	}

	return Box;
}

void STrueScopeToolbar::HandleSetAnalysisPath(bool bUseGpu)
{
	FTrueScopeModule::Get().SetUseGpuAnalysis(bUseGpu);
}

bool STrueScopeToolbar::IsAnalysisPathSelected(bool bGpu) const
{
	// The EFFECTIVE path, so the button shows what actually produced the numbers
	// on screen even while a diagnostic CVar override is in force.
	return FTrueScopeModule::Get().GetEffectiveUseGpuAnalysis() == bGpu;
}

void STrueScopeToolbar::HandleSetMeasurementRate(float RateHz)
{
	FTrueScopeModule::Get().SetMeasurementRateHz(RateHz);
}

bool STrueScopeToolbar::IsMeasurementRateSelected(float RateHz) const
{
	return FMath::IsNearlyEqual(FTrueScopeModule::Get().GetMeasurementRateHz(), RateHz);
}

#undef LOCTEXT_NAMESPACE
