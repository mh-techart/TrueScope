// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "UI/STrueScopeHistogram.h"

#include "Analysis/TrueScopeAnalysis.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "UI/TrueScopeDraw.h"

#define LOCTEXT_NAMESPACE "STrueScopeHistogram"

namespace
{
	/** Fixed-size gap between the stacked channel sub-plots. */
	constexpr float ChannelGap = 3.0f;

	const FLinearColor ChannelFillColors[FTrueScopeAnalysisResult::NumChannels] = {
		FLinearColor(0.9f, 0.08f, 0.08f, 0.38f),
		FLinearColor(0.08f, 0.9f, 0.08f, 0.38f),
		FLinearColor(0.15f, 0.35f, 1.0f, 0.38f),
	};
	const FLinearColor ChannelLineColors[FTrueScopeAnalysisResult::NumChannels] = {
		FLinearColor(1.0f, 0.25f, 0.25f, 0.9f),
		FLinearColor(0.25f, 1.0f, 0.25f, 0.9f),
		FLinearColor(0.35f, 0.55f, 1.0f, 0.9f),
	};
}

void STrueScopeHistogram::Construct(const FArguments& InArgs,
	const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& InAnalyzer)
{
	InitializeScopeWidget(InAnalyzer, /*bMaintainSquareAspect*/ false);
}

FSlateRect STrueScopeHistogram::GetChannelPlotRect(const FSlateRect& Plot, ETrueScopeHistogramMode InMode, int32 Channel)
{
	if (InMode == ETrueScopeHistogramMode::RgbOverlay)
	{
		return Plot;
	}

	// Clamped so a plot too small for the gaps degenerates to zero-height bands
	// instead of inverted rects. OnPaint never draws that small.
	constexpr int32 NumChannels = FTrueScopeAnalysisResult::NumChannels;
	const float SectionHeight = FMath::Max(0.0f, (Plot.GetSize().Y - ChannelGap * (NumChannels - 1)) / NumChannels);
	const float Top = Plot.Top + Channel * (SectionHeight + ChannelGap);
	return FSlateRect(Plot.Left, Top, Plot.Right, Top + SectionHeight);
}

TArrayView<const FTrueScopeDisplayMode> STrueScopeHistogram::GetDisplayModes() const
{
	// Function-local: LOCTEXT must not run before localization is initialized.
	static const TArray<FTrueScopeDisplayMode> Modes = {
		FTrueScopeDisplayMode{static_cast<int32>(ETrueScopeHistogramMode::SeparateChannels),
			LOCTEXT("SeparateChannels", "Separate Channels")},
		FTrueScopeDisplayMode{static_cast<int32>(ETrueScopeHistogramMode::RgbOverlay),
			LOCTEXT("RgbOverlay", "RGB Overlay")},
	};
	return Modes;
}

int32 STrueScopeHistogram::GetDisplayModeId() const
{
	return static_cast<int32>(Mode);
}

void STrueScopeHistogram::SetDisplayModeId(int32 InModeId)
{
	Mode = static_cast<ETrueScopeHistogramMode>(InModeId);
}

int32 STrueScopeHistogram::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace TrueScope::Draw;

	const FSlateRect Content = GetScopeContentRect(AllottedGeometry);
	LayerId = DrawBackground(OutDrawElements, LayerId, AllottedGeometry, Content);

	// Plot area: fixed-size padding and bottom label strip.
	const FSlateRect Plot(Content.Left + PlotPadding, Content.Top + PlotPadding,
		Content.Right - PlotPadding, Content.Bottom - PlotPadding - ValueLabelStripHeight);
	if (Plot.GetSize().X < 16.0f || Plot.GetSize().Y < 16.0f)
	{
		return LayerId;
	}

	// One 0-1023 value axis spanning all channel sub-plots.
	LayerId = DrawValueAxisX(OutDrawElements, LayerId, AllottedGeometry, Plot, Content, GetHistogramGraticuleLevels());

	const TSharedPtr<const FTrueScopeAnalysisResult, ESPMode::ThreadSafe>& Result = GetCachedResult();
	if (!Result.IsValid() || Result->SourceSize.X <= 0)
	{
		return LayerId;
	}

	// Shared normalization across channels so relative channel heights stay
	// truthful; log mapping for the smooth density look.
	uint32 MaxCount = 0;
	for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
	{
		for (uint32 Count : Result->Histogram[Channel])
		{
			MaxCount = FMath::Max(MaxCount, Count);
		}
	}
	if (MaxCount == 0)
	{
		return LayerId;
	}

	const FSlateBrush* WhiteBrush = FAppStyle::Get().GetBrush("WhiteBrush");
	constexpr int32 NumBins = FTrueScopeAnalysisResult::NumBins;

	for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
	{
		const FSlateRect ChannelPlot = GetChannelPlotRect(Plot, Mode, Channel);
		const FColor FillColor = ChannelFillColors[Channel].ToFColor(false);

		// Filled curve as one triangle strip: bottom/top vertex pairs per bin.
		TArray<FSlateVertex> Vertices;
		Vertices.Reserve(NumBins * 2);
		TArray<SlateIndex> Indices;
		Indices.Reserve((NumBins - 1) * 6);

		TArray<FVector2f> TopLine;
		TopLine.Reserve(NumBins);

		for (int32 Bin = 0; Bin < NumBins; ++Bin)
		{
			// Bin -> fraction of the 0-1023 axis; bin 255 sits on full scale.
			const float X = ChannelPlot.Left + ChannelPlot.GetSize().X * (static_cast<float>(Bin) / (NumBins - 1));
			const float Intensity = TrueScope::CountToIntensity(Result->Histogram[Channel][Bin], MaxCount);
			const float TopY = ChannelPlot.Bottom - ChannelPlot.GetSize().Y * Intensity;
			TopLine.Add(FVector2f(X, TopY));

			FSlateVertex Bottom;
			Bottom.Position = FVector2f(AllottedGeometry.LocalToAbsolute(FVector2D(X, ChannelPlot.Bottom)));
			Bottom.TexCoords[0] = Bottom.TexCoords[1] = Bottom.TexCoords[2] = Bottom.TexCoords[3] = 0.0f;
			Bottom.Color = FillColor;
			Vertices.Add(Bottom);

			FSlateVertex Top = Bottom;
			Top.Position = FVector2f(AllottedGeometry.LocalToAbsolute(FVector2D(X, TopY)));
			Vertices.Add(Top);

			if (Bin > 0)
			{
				const SlateIndex Base = static_cast<SlateIndex>((Bin - 1) * 2);
				Indices.Append({Base, Base + 1, Base + 2, Base + 1, Base + 3, Base + 2});
			}
		}

		FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId, WhiteBrush->GetRenderingResource(),
			Vertices, Indices, nullptr, 0, 0);

		// Crisp anti-aliased outline on top of the translucent fill.
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(),
			TopLine, ESlateDrawEffect::None, ChannelLineColors[Channel], /*bAntialias*/ true, 1.0f);
	}
	LayerId += 2;

	return LayerId;
}

#undef LOCTEXT_NAMESPACE
