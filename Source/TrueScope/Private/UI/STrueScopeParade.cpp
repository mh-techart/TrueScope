// Copyright 2026 Michael Herber
// SPDX-License-Identifier: Apache-2.0

#include "UI/STrueScopeParade.h"

#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Rendering/DrawElements.h"
#include "UI/TrueScopeDraw.h"

#define LOCTEXT_NAMESPACE "STrueScopeParade"

namespace
{
	/** Fixed-size gap between the three channel sections. */
	constexpr float SectionGap = 4.0f;

	/**
	 * Colour written into each channel's density texel — the single-channel
	 * intensity goes into exactly the channel's own component, zero elsewhere,
	 * so a block physically cannot contain another channel's trace.
	 */
	FColor MakeChannelTexel(int32 Channel, uint8 Intensity)
	{
		switch (Channel)
		{
		case FTrueScopeAnalysisResult::ChannelR: return FColor(Intensity, 0, 0, 255);
		case FTrueScopeAnalysisResult::ChannelG: return FColor(0, Intensity, 0, 255);
		default:                                 return FColor(0, 0, Intensity, 255);
		}
	}
}

void STrueScopeParade::Construct(const FArguments& InArgs,
	const TSharedPtr<FTrueScopeAnalyzer, ESPMode::ThreadSafe>& InAnalyzer)
{
	for (FSlateBrush& Brush : ChannelBrush)
	{
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.Tiling = ESlateBrushTileType::NoTile;
	}
	InitializeScopeWidget(InAnalyzer, ETrueScopeType::Parade, /*bMaintainSquareAspect*/ false,
		InArgs._PanelIndex);
}


const FTrueScopeGainOption* STrueScopeParade::GetGainOption() const
{
	// Function-local: LOCTEXT must not run before localization is initialized.
	static const FTrueScopeGainOption Option{
		TrueScope::TraceBrightnessGainKey,
		LOCTEXT("TraceBrightness", "Trace Brightness"),
		LOCTEXT("TraceBrightnessTooltip",
			"How bright a given pixel count draws. Centre is this scope's calibrated default; the ends are 1/8x and 8x. Double-click to reset."),
	};
	return &Option;
}

void STrueScopeParade::OnAnalysisResultChanged()
{
	const TSharedPtr<const FTrueScopeAnalysisResult, ESPMode::ThreadSafe>& Result = GetCachedResult();
	if (!Result.IsValid() || Result->ParadeImageSize.X <= 0)
	{
		return;
	}
	const FIntPoint Size = Result->ParadeImageSize;
	const int32 NumTexels = Size.X * Size.Y;

	// GPU path (M5): the result carries the producer's density render targets;
	// point the brushes at them — no texture build, no upload, no readback. The
	// raster fills the section rect this widget asked for in device pixels
	// (phase-6.1), so the brush samples exactly that region.
	if (Result->bHasGpuParade)
	{
		for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
		{
			UTextureRenderTarget2D* Target = Result->ParadeChannelTarget[Channel].Get();
			TrueScope::Draw::BindRasterBrush(ChannelBrush[Channel], Target, Result->ParadeRasterSize);
			if (Target)
			{
				ChannelTexture[Channel].Reset(); // CPU-path texture no longer drawn
			}
		}
		return;
	}

	for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
	{
		const TArray<uint8>& Intensity = Result->ParadeChannelIntensity[Channel];
		if (Intensity.Num() != NumTexels)
		{
			continue;
		}

		if (!ChannelTexture[Channel].IsValid()
			|| ChannelTexture[Channel]->GetSizeX() != Size.X || ChannelTexture[Channel]->GetSizeY() != Size.Y)
		{
			// NAME_None: let each of the three channel textures get a unique
			// auto-generated name. A shared explicit name makes the 2nd/3rd
			// CreateTransient collide with the still-referenced 1st in the
			// transient package and assert in FreeUObjectIndex.
			UTexture2D* Texture = UTexture2D::CreateTransient(Size.X, Size.Y, PF_B8G8R8A8);
			// Raw intensity values: no sRGB decode on sampling (drawn with
			// NoGamma, so the computed display mapping arrives 1:1 on screen).
			Texture->SRGB = false;
			Texture->Filter = TF_Bilinear;
			Texture->UpdateResource();
			ChannelTexture[Channel].Reset(Texture);
			TrueScope::Draw::BindTextureBrush(ChannelBrush[Channel], Texture, Size);
		}

		// Colour the single-channel intensity into a private BGRA buffer, freed
		// by the async-upload cleanup once the render thread has consumed it.
		FColor* Texels = static_cast<FColor*>(FMemory::Malloc(NumTexels * sizeof(FColor)));
		for (int32 Index = 0; Index < NumTexels; ++Index)
		{
			Texels[Index] = MakeChannelTexel(Channel, Intensity[Index]);
		}

		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Size.X, Size.Y);
		ChannelTexture[Channel]->UpdateTextureRegions(0, 1, Region, Size.X * sizeof(FColor), sizeof(FColor),
			reinterpret_cast<uint8*>(Texels),
			[](uint8* SrcData, const FUpdateTextureRegion2D* InRegion)
			{
				FMemory::Free(SrcData);
				delete InRegion;
			});
	}
}

bool STrueScopeParade::HasDrawableData() const
{
	const TSharedPtr<const FTrueScopeAnalysisResult, ESPMode::ThreadSafe>& Result = GetCachedResult();
	if (!Result.IsValid() || Result->ParadeImageSize.X <= 0)
	{
		return false;
	}
	if (Result->bHasGpuParade)
	{
		return true;
	}
	// CPU path: exactly the completeness condition the texture upload checks.
	return Result->ParadeChannelIntensity[FTrueScopeAnalysisResult::ChannelR].Num()
		== Result->ParadeImageSize.X * Result->ParadeImageSize.Y;
}

int32 STrueScopeParade::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
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

	// Plot area: fixed-size left label strip and padding.
	const FSlateRect Plot(Content.Left + ValueLabelStripWidth, Content.Top + PlotPadding,
		Content.Right - PlotPadding, Content.Bottom - PlotPadding);

	// Density sections, drawn under the graticule. Each section samples only
	// its own channel's texture (colour already baked in), so it is drawn with
	// a plain white tint — no cross-channel masking that could bleed.
	{
		const float SectionWidth =
			(Plot.GetSize().X - SectionGap * (FTrueScopeAnalysisResult::NumChannels - 1))
			/ FTrueScopeAnalysisResult::NumChannels;

		// The GPU producer rasterizes one section at exactly this size in device
		// pixels (the three sections are congruent), so its density texture is
		// drawn 1:1 rather than upscaled (phase-6.1 Task 1).
		RequestRasterSize(TrueScope::ERasterTarget::ParadeSection, AllottedGeometry,
			FVector2f(SectionWidth, Plot.GetSize().Y));

		for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
		{
			// The brush holds whichever producer's texture is current: the
			// CPU path's transient texture or the GPU path's render target.
			if (!ChannelBrush[Channel].GetResourceObject())
			{
				continue;
			}
			const float SectionLeft = Plot.Left + Channel * (SectionWidth + SectionGap);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
				AllottedGeometry.ToPaintGeometry(FVector2f(SectionWidth, Plot.GetSize().Y),
					FSlateLayoutTransform(FVector2f(SectionLeft, Plot.Top))),
				&ChannelBrush[Channel], ESlateDrawEffect::NoGamma, FLinearColor::White);
		}
		++LayerId;
	}

	// Graticule + fixed-size 10-bit scale labels, every 128 code values.
	LayerId = DrawValueAxisY(OutDrawElements, LayerId, AllottedGeometry, Plot, Content, GetWaveformGraticuleLevels());

	return DrawScopeHints(OutDrawElements, LayerId, AllottedGeometry, Content);
}

#undef LOCTEXT_NAMESPACE
