// Copyright 2026 Michael Herber
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
	for (FSlateBrush& Brush : YrgbBrush)
	{
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.Tiling = ESlateBrushTileType::NoTile;
	}
	OverlayBrush.DrawAs = ESlateBrushDrawType::Image;
	OverlayBrush.Tiling = ESlateBrushTileType::NoTile;
	InitializeScopeWidget(InAnalyzer, ETrueScopeType::Histogram, /*bMaintainSquareAspect*/ false,
		InArgs._PanelIndex);
}

void STrueScopeHistogram::OnAnalysisResultChanged()
{
	// GPU path (phase-5.2): point the brushes at the producer's curve raster
	// targets — no texture build, no upload, no readback (the parade widget's
	// pattern). All three sets are rasterized at their own rect's device
	// resolution (phase-6.1: an RGB band, a YRGB band and the whole plot), so
	// each is drawn 1:1 and the outline is exactly the CPU draw's 1-px Slate
	// line. A CPU result instead rebuilds the YRGB luma bins here: OnPaint
	// draws the vector curves from the bin arrays.
	const TSharedPtr<const FTrueScopeAnalysisResult, ESPMode::ThreadSafe>& Result = GetCachedResult();
	if (!Result.IsValid())
	{
		return;
	}
	if (!Result->bHasGpuHistogram)
	{
		LumaHistogramBins = BuildLumaHistogramBins(*Result);
		return;
	}
	LumaHistogramBins.Reset();

	for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
	{
		TrueScope::Draw::BindRasterBrush(StackedBrush[Channel], Result->HistogramStackedTarget[Channel].Get(),
			Result->HistogramStackedRasterSize);
	}
	for (int32 Slot = 0; Slot < FTrueScopeAnalysisResult::NumYrgbCurves; ++Slot)
	{
		TrueScope::Draw::BindRasterBrush(YrgbBrush[Slot], Result->HistogramYrgbTarget[Slot].Get(),
			Result->HistogramYrgbRasterSize);
	}
	TrueScope::Draw::BindRasterBrush(OverlayBrush, Result->HistogramOverlayTarget.Get(),
		Result->HistogramOverlayRasterSize);
}

int32 STrueScopeHistogram::GetNumCurveSlots(ETrueScopeHistogramMode InMode)
{
	return InMode == ETrueScopeHistogramMode::SeparateChannelsYrgb
		? FTrueScopeAnalysisResult::NumYrgbCurves
		: FTrueScopeAnalysisResult::NumChannels;
}

int32 STrueScopeHistogram::SlotToChannel(ETrueScopeHistogramMode InMode, int32 Slot)
{
	if (InMode != ETrueScopeHistogramMode::SeparateChannelsYrgb)
	{
		return Slot;
	}
	// YRGB display order: luma on top (Resolve's layout), then R, G, B.
	return Slot == 0 ? INDEX_NONE : Slot - 1;
}

FSlateRect STrueScopeHistogram::GetChannelPlotRect(const FSlateRect& Plot, ETrueScopeHistogramMode InMode, int32 Slot)
{
	if (InMode == ETrueScopeHistogramMode::RgbOverlay)
	{
		return Plot;
	}

	// Clamped so a plot too small for the gaps degenerates to zero-height bands
	// instead of inverted rects. OnPaint never draws that small.
	const int32 NumSlots = GetNumCurveSlots(InMode);
	const float SectionHeight = FMath::Max(0.0f, (Plot.GetSize().Y - ChannelGap * (NumSlots - 1)) / NumSlots);
	const float Top = Plot.Top + Slot * (SectionHeight + ChannelGap);
	return FSlateRect(Plot.Left, Top, Plot.Right, Top + SectionHeight);
}

TArrayView<const uint32> STrueScopeHistogram::GetChannelBins(const FTrueScopeAnalysisResult& Result, int32 Channel)
{
	return Result.Histogram[Channel];
}

TArray<uint32> STrueScopeHistogram::BuildLumaHistogramBins(const FTrueScopeAnalysisResult& Result)
{
	// Column sums of the waveform's integer-exact luma column bins — the exact
	// relation the R/G/B histograms have to the parade (and what the GPU
	// reduce pass computes for its luma band). Summing is not binning: the
	// bins themselves are phase-5.2's, untouched.
	constexpr int32 NumBins = FTrueScopeAnalysisResult::NumBins;
	TArray<uint32> LumaBins;
	if (Result.SourceSize.X <= 0 || Result.LumaParade.Num() != Result.SourceSize.X * NumBins)
	{
		return LumaBins;
	}

	LumaBins.SetNumZeroed(NumBins);
	for (int32 Column = 0; Column < Result.SourceSize.X; ++Column)
	{
		const uint32* ColumnBins = Result.LumaParade.GetData() + Column * NumBins;
		for (int32 Bin = 0; Bin < NumBins; ++Bin)
		{
			LumaBins[Bin] += ColumnBins[Bin];
		}
	}
	return LumaBins;
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

float STrueScopeHistogram::GetClipThresholdCount(const FTrueScopeAnalysisResult& Result, float HistogramGain)
{
	// The fraction is OF THE ANALYSED FRAME's pixel count, never the viewport
	// or the scope texture — that is what keeps the histogram's appearance
	// independent of resolution and of the panel's size.
	const int64 TotalFramePixels =
		static_cast<int64>(Result.SourceSize.X) * static_cast<int64>(Result.SourceSize.Y);
	return TrueScope::HistogramClipThresholdCount(TotalFramePixels, HistogramGain);
}

float STrueScopeHistogram::GetCurveHeight(uint32 Count, float ClipThresholdCount)
{
	return TrueScope::CountToCurveHeight(Count, ClipThresholdCount);
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


const FTrueScopeGainOption* STrueScopeHistogram::GetGainOption() const
{
	// Function-local: LOCTEXT must not run before localization is initialized.
	// The slider moves the fixed clip threshold, so it reads as a zoom on the
	// vertical scale: centre is the calibrated 1.58 % of the frame's pixels
	// filling the band, left shows more headroom (12.6 %), right magnifies
	// sparse bins (0.20 %). See TrueScopeDisplayGains.h for the derivation.
	static const FTrueScopeGainOption Option{
		TrueScope::HistogramGainKey,
		// Named in full even inside the histogram's own menu: the phase-6.3
		// term for this control is "Histogram Gain", and the docs, the settings
		// key and the row the user sees should all say the same thing.
		LOCTEXT("HistogramGain", "Histogram Gain"),
		LOCTEXT("HistogramGainTooltip",
			"Vertical scale of the curves. Centre is the calibrated default (a bin holding 1.58% of the frame's pixels fills the band); left compresses, right magnifies. Double-click to reset."),
	};
	return &Option;
}

TArrayView<const FTrueScopeDisplayMode> STrueScopeHistogram::GetDisplayModes() const
{
	// Function-local: LOCTEXT must not run before localization is initialized.
	// RGB before YRGB: RGB is the default (see the mode enum for why not YRGB).
	static const TArray<FTrueScopeDisplayMode> Modes = {
		FTrueScopeDisplayMode{static_cast<int32>(ETrueScopeHistogramMode::SeparateChannels),
			LOCTEXT("SeparateChannels", "Separate Channels RGB")},
		FTrueScopeDisplayMode{static_cast<int32>(ETrueScopeHistogramMode::SeparateChannelsYrgb),
			LOCTEXT("SeparateChannelsYrgb", "Separate Channels YRGB")},
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

	// Too small to read: the shared hint mechanism says so (phase-6.1 Task 5).
	if (IsTooSmallToRead(Content.GetSize()))
	{
		return DrawScopeHints(OutDrawElements, LayerId, AllottedGeometry, Content);
	}

	// Plot area: fixed-size padding and bottom label strip.
	const FSlateRect Plot(Content.Left + PlotPadding, Content.Top + PlotPadding,
		Content.Right - PlotPadding, Content.Bottom - PlotPadding - ValueLabelStripHeight);

	// One 0-1023 value axis spanning all channel sub-plots.
	LayerId = DrawValueAxisX(OutDrawElements, LayerId, AllottedGeometry, Plot, Content, GetHistogramGraticuleLevels());

	// All curve rasters are requested every paint, not just the current mode's:
	// the producer rasterizes every set each capture, so switching mode swaps
	// to a texture that is already correct for its rect instead of waiting for
	// the next capture (phase-6.1 Task 1; sizes in device pixels).
	RequestRasterSize(TrueScope::ERasterTarget::HistogramBand, AllottedGeometry,
		GetChannelPlotRect(Plot, ETrueScopeHistogramMode::SeparateChannels, 0).GetSize());
	RequestRasterSize(TrueScope::ERasterTarget::HistogramBandYrgb, AllottedGeometry,
		GetChannelPlotRect(Plot, ETrueScopeHistogramMode::SeparateChannelsYrgb, 0).GetSize());
	RequestRasterSize(TrueScope::ERasterTarget::HistogramOverlay, AllottedGeometry, Plot.GetSize());

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
		else if (Mode == ETrueScopeHistogramMode::SeparateChannelsYrgb)
		{
			for (int32 Slot = 0; Slot < FTrueScopeAnalysisResult::NumYrgbCurves; ++Slot)
			{
				DrawCurveBox(YrgbBrush[Slot], GetChannelPlotRect(Plot, Mode, Slot));
			}
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

	// One shared vertical scale across all displayed curves, so their relative
	// heights stay comparable — and since M6.3 a FIXED one, so the same height
	// always means the same pixel count across shots (see GetClipThresholdCount).
	// It is shared by construction now: the threshold depends only on the frame
	// size and the gain, so no display mode can end up on a scale of its own.
	const float ClipThresholdCount = GetClipThresholdCount(*Result, GetGainMultiplier());
	if (ClipThresholdCount <= 0.0f)
	{
		return DrawScopeHints(OutDrawElements, LayerId, AllottedGeometry, Content);
	}

	const FSlateBrush* WhiteBrush = FAppStyle::Get().GetBrush("WhiteBrush");
	constexpr int32 NumBins = FTrueScopeAnalysisResult::NumBins;

	for (int32 Slot = 0; Slot < GetNumCurveSlots(Mode); ++Slot)
	{
		const int32 Channel = SlotToChannel(Mode, Slot);
		const bool bLumaSlot = Channel == INDEX_NONE;
		if (bLumaSlot && LumaHistogramBins.Num() != NumBins)
		{
			continue; // luma bins not derivable from this result; draw the rest
		}

		const FSlateRect ChannelPlot = GetChannelPlotRect(Plot, Mode, Slot);
		const TArrayView<const uint32> Bins = bLumaSlot
			? TArrayView<const uint32>(LumaHistogramBins)
			: GetChannelBins(*Result, Channel);
		const FColor FillColor =
			(bLumaSlot ? HistogramLumaFillColor : GetChannelFillColor(Channel)).ToFColor(false);

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
			const float Height = GetCurveHeight(Bins[Bin], ClipThresholdCount);
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
			TopLine, ESlateDrawEffect::None,
			bLumaSlot ? HistogramLumaLineColor : GetChannelLineColor(Channel), /*bAntialias*/ true,
			TrueScope::Draw::HistogramLineThickness);
	}
	LayerId += 2;

	return DrawScopeHints(OutDrawElements, LayerId, AllottedGeometry, Content);
}

#undef LOCTEXT_NAMESPACE
