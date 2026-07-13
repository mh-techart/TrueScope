// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "UI/STrueScopeHistogram.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "UI/TrueScopeDraw.h"

#define LOCTEXT_NAMESPACE "STrueScopeHistogram"

namespace
{
	/** Fixed-size gap between the stacked channel sub-plots. */
	constexpr float ChannelGap = 3.0f;
}

void STrueScopeHistogram::Construct(const FArguments& InArgs,
	const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& InAnalyzer)
{
	for (FSlateBrush& Brush : StackedBrush)
	{
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.Tiling = ESlateBrushTileType::NoTile;
	}
	OverlayBrush.DrawAs = ESlateBrushDrawType::Image;
	OverlayBrush.Tiling = ESlateBrushTileType::NoTile;
	InitializeScopeWidget(InAnalyzer, /*bMaintainSquareAspect*/ false);
}

void STrueScopeHistogram::OnAnalysisResultChanged()
{
	// GPU path (phase-5.2): point the brushes at the producer's curve raster
	// targets — no texture build, no upload, no readback (the parade widget's
	// pattern). A CPU result needs nothing here: OnPaint draws the vector
	// curves directly from its bin arrays.
	const TSharedPtr<const FTrueScopeAnalysisResult, ESPMode::ThreadSafe>& Result = GetCachedResult();
	if (!Result.IsValid() || !Result->bHasGpuHistogram)
	{
		return;
	}

	for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
	{
		UTextureRenderTarget2D* Target = Result->HistogramStackedTarget[Channel].Get();
		if (Target && StackedBrush[Channel].GetResourceObject() != Target)
		{
			StackedBrush[Channel].SetResourceObject(Target);
			StackedBrush[Channel].ImageSize = FVector2f(Target->SizeX, Target->SizeY);
		}
	}
	UTextureRenderTarget2D* OverlayTarget = Result->HistogramOverlayTarget.Get();
	if (OverlayTarget && OverlayBrush.GetResourceObject() != OverlayTarget)
	{
		OverlayBrush.SetResourceObject(OverlayTarget);
		OverlayBrush.ImageSize = FVector2f(OverlayTarget->SizeX, OverlayTarget->SizeY);
	}
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

TArrayView<const uint32> STrueScopeHistogram::GetChannelBins(const FTrueScopeAnalysisResult& Result, int32 Channel)
{
	return Result.Histogram[Channel];
}

FLinearColor STrueScopeHistogram::GetChannelFillColor(int32 Channel)
{
	// Shared with the GPU raster pass (TrueScopeDraw.h) so both paths draw
	// pixel-identical colours.
	return TrueScope::Draw::HistogramFillColors[Channel];
}

FLinearColor STrueScopeHistogram::GetChannelLineColor(int32 Channel)
{
	return TrueScope::Draw::HistogramLineColors[Channel];
}

uint32 STrueScopeHistogram::GetSharedMaxCount(const FTrueScopeAnalysisResult& Result)
{
	uint32 MaxCount = 0;
	for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
	{
		for (uint32 Count : GetChannelBins(Result, Channel))
		{
			MaxCount = FMath::Max(MaxCount, Count);
		}
	}
	return MaxCount;
}

float STrueScopeHistogram::GetCurveHeight(uint32 Count, uint32 MaxCount)
{
	if (Count == 0 || MaxCount == 0)
	{
		return 0.0f;
	}
	return static_cast<float>(Count) / static_cast<float>(MaxCount);
}

bool STrueScopeHistogram::HasDrawableData() const
{
	const TSharedPtr<const FTrueScopeAnalysisResult, ESPMode::ThreadSafe>& Result = GetCachedResult();
	if (!Result.IsValid() || Result->SourceSize.X <= 0)
	{
		return false;
	}
	// CPU path: exactly the conditions the vector draw needs (bins present
	// and a nonzero shared scale).
	return Result->bHasGpuHistogram || GetSharedMaxCount(*Result) > 0;
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
		return DrawPausedHint(OutDrawElements, LayerId, AllottedGeometry, Content);
	}

	// One 0-1023 value axis spanning all channel sub-plots.
	LayerId = DrawValueAxisX(OutDrawElements, LayerId, AllottedGeometry, Plot, Content, GetHistogramGraticuleLevels());

	const TSharedPtr<const FTrueScopeAnalysisResult, ESPMode::ThreadSafe>& Result = GetCachedResult();
	if (!Result.IsValid() || Result->SourceSize.X <= 0)
	{
		return DrawScopeHints(OutDrawElements, LayerId, AllottedGeometry, Content);
	}

	// GPU path (phase-5.2): the curves arrive pre-rasterized (fill + outline,
	// exact colours and layer order, linear count -> height on the shared
	// scale); draw the mode's raster into the same rects the vector draw
	// uses. The transparent background keeps the gridlines visible.
	if (Result->bHasGpuHistogram)
	{
		auto DrawCurveBox = [&](const FSlateBrush& Brush, const FSlateRect& Rect)
		{
			if (Brush.GetResourceObject())
			{
				// NoGamma, like every GPU scope texture: without it Slate
				// display-gamma-corrects the sampled texel while the CPU
				// path's vertex colours are not corrected, rendering the GPU
				// fills visibly lighter (found in the phase-5.2 MCP A/B).
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
					AllottedGeometry.ToPaintGeometry(FVector2f(Rect.GetSize()),
						FSlateLayoutTransform(FVector2f(Rect.GetTopLeft()))),
					&Brush, ESlateDrawEffect::NoGamma, FLinearColor::White);
			}
		};
		if (Mode == ETrueScopeHistogramMode::RgbOverlay)
		{
			DrawCurveBox(OverlayBrush, Plot);
		}
		else
		{
			for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
			{
				DrawCurveBox(StackedBrush[Channel], GetChannelPlotRect(Plot, Mode, Channel));
			}
		}
		++LayerId;
		return DrawScopeHints(OutDrawElements, LayerId, AllottedGeometry, Content);
	}

	// One shared vertical scale across the channels, so their relative heights
	// stay comparable (see GetCurveHeight for why the scale is linear).
	const uint32 MaxCount = GetSharedMaxCount(*Result);
	if (MaxCount == 0)
	{
		return DrawScopeHints(OutDrawElements, LayerId, AllottedGeometry, Content);
	}

	const FSlateBrush* WhiteBrush = FAppStyle::Get().GetBrush("WhiteBrush");
	constexpr int32 NumBins = FTrueScopeAnalysisResult::NumBins;

	for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
	{
		const FSlateRect ChannelPlot = GetChannelPlotRect(Plot, Mode, Channel);
		const TArrayView<const uint32> Bins = GetChannelBins(*Result, Channel);
		const FColor FillColor = GetChannelFillColor(Channel).ToFColor(false);

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
			const float Height = GetCurveHeight(Bins[Bin], MaxCount);
			const float TopY = ChannelPlot.Bottom - ChannelPlot.GetSize().Y * Height;
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
			TopLine, ESlateDrawEffect::None, GetChannelLineColor(Channel), /*bAntialias*/ true, 1.0f);
	}
	LayerId += 2;

	return DrawScopeHints(OutDrawElements, LayerId, AllottedGeometry, Content);
}

#undef LOCTEXT_NAMESPACE
