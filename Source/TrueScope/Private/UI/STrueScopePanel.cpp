// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "UI/STrueScopePanel.h"

#include "Capture/TrueScopeViewExtension.h"
#include "Engine/TextureRenderTarget2D.h"
#include "LevelEditorViewport.h"
#include "Styling/AppStyle.h"
#include "TrueScopeModule.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "STrueScopePanel"

/**
 * Draws the capture render target without Slate's gamma correction
 * (ESlateDrawEffect::NoGamma), the same way the editor draws its scene
 * viewports: the capture already holds display-encoded values, and a regular
 * SImage would get Slate's linear-to-display encode on top, washing it out.
 * M2 throwaway together with the rest of the preview panel.
 */
class STrueScopeCapturePreview : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(STrueScopeCapturePreview)
		{
		}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FSlateBrush* InBrush)
	{
		Brush = InBrush;
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(),
			Brush, ESlateDrawEffect::NoGamma);
		return LayerId;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(Brush->ImageSize);
	}

private:
	const FSlateBrush* Brush = nullptr;
};

void STrueScopePanel::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Vertical)

		+ SSplitter::Slot()
		.Value(0.5f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			+ SSplitter::Slot()
			.Value(0.5f)
			[
				// M2: temporary raw capture preview in the Waveform slot.
				MakeCapturePreviewPanel()
			]

			+ SSplitter::Slot()
			.Value(0.5f)
			[
				MakePlaceholderPanel(LOCTEXT("ParadeLabel", "Parade"))
			]
		]

		+ SSplitter::Slot()
		.Value(0.5f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			+ SSplitter::Slot()
			.Value(0.5f)
			[
				MakePlaceholderPanel(LOCTEXT("VectorscopeLabel", "Vectorscope"))
			]

			+ SSplitter::Slot()
			.Value(0.5f)
			[
				MakePlaceholderPanel(LOCTEXT("HistogramLabel", "Histogram"))
			]
		]
	];
}

TSharedRef<SWidget> STrueScopePanel::MakePlaceholderPanel(const FText& ScopeName)
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(ScopeName)
		];
}

TSharedRef<SWidget> STrueScopePanel::MakeCapturePreviewPanel()
{
	// Wire the brush to the capture render target. The extension owns the RT
	// (and keeps it alive); it exists whenever the editor UI does.
	UTextureRenderTarget2D* CaptureRT = nullptr;
	if (TSharedPtr<FTrueScopeViewExtension> Extension = FTrueScopeModule::Get().GetViewExtension())
	{
		CaptureRT = Extension->GetCaptureRenderTarget();
	}
	CapturePreviewBrush.SetResourceObject(CaptureRT);
	CapturePreviewBrush.ImageSize = FVector2f(TrueScope::CaptureWidth, TrueScope::CaptureHeight);

	// Whole-widget states: image when a level viewport is being measured,
	// "no viewport" text otherwise.
	const auto HasActiveViewport = []() { return GCurrentLevelEditingViewportClient != nullptr; };

	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
		.Padding(0.0f)
		[
			SNew(SOverlay)

			+ SOverlay::Slot()
			[
				// The capture is stretched to the fixed working resolution;
				// the preview shows it at that fixed 16:9 aspect (a viewport
				// with a different aspect appears correspondingly squeezed).
				SNew(SScaleBox)
				.Stretch(EStretch::ScaleToFit)
				.Visibility_Lambda([HasActiveViewport]()
					{ return HasActiveViewport() ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					SNew(STrueScopeCapturePreview, &CapturePreviewBrush)
				]
			]

			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NoViewport", "No viewport"))
				.Visibility_Lambda([HasActiveViewport]()
					{ return HasActiveViewport() ? EVisibility::Collapsed : EVisibility::Visible; })
			]
		];
}

#undef LOCTEXT_NAMESPACE
