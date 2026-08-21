// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "UI/TrueScopeDraw.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"

namespace TrueScope::Draw
{

namespace
{
	/** Every 128 code values; the last line sits on full scale, not on 1024. */
	constexpr int32 WaveformLevels[] = {0, 128, 256, 384, 512, 640, 768, 896, ScaleMax};

	/**
	 * The histogram labels the SAME levels as the waveform and parade (M6.4
	 * Task 7). It used to label only the quarters, which left the user reading
	 * one scale on three scopes and a coarser one on the fourth — the values
	 * are the same 0-1023 code values, so the graticule is too.
	 */
	constexpr int32 HistogramLevels[] = {0, 128, 256, 384, 512, 640, 768, 896, ScaleMax};

	/** Fraction of the value axis a 10-bit level sits at. */
	float LevelFraction(int32 Level)
	{
		return Level / static_cast<float>(ScaleMax);
	}

	/**
	 * Minor scale ticks (phase-6.2 Task 3): short fixed-size marks beside the
	 * label strip — length sized to the gap between labels and plot, so they
	 * can never run into the label text.
	 */
	constexpr float MinorTickLength = 4.0f;
	const FLinearColor MinorTickColor(0.5f, 0.5f, 0.5f, 0.5f);

	/** All minor tick axis values of a labelled-level list. */
	TArray<float> BuildMinorTickValues(TArrayView<const int32> Levels)
	{
		TArray<float> Values;
		Values.Reserve(FMath::Max(0, Levels.Num() - 1) * MinorTicksPerDivision);
		for (int32 Index = 0; Index + 1 < Levels.Num(); ++Index)
		{
			GetMinorTickValues(Levels[Index], Levels[Index + 1], Values);
		}
		return Values;
	}
}

void BindRasterBrush(FSlateBrush& Brush, UTextureRenderTarget2D* Target, FIntPoint RasterSize)
{
	if (!Target || RasterSize.X <= 0 || RasterSize.Y <= 0)
	{
		return;
	}

	if (Brush.GetResourceObject() != Target)
	{
		Brush.SetResourceObject(Target);
	}
	Brush.ImageSize = FVector2f(RasterSize.X, RasterSize.Y);

	// The rasterized region within the bucket-allocated target. Re-set on every
	// result: the region tracks the panel, the allocation only tracks buckets.
	const float TargetWidth = static_cast<float>(FMath::Max<int32>(Target->SizeX, 1));
	const float TargetHeight = static_cast<float>(FMath::Max<int32>(Target->SizeY, 1));
	Brush.SetUVRegion(FBox2f(FVector2f::ZeroVector,
		FVector2f(FMath::Min(RasterSize.X / TargetWidth, 1.0f), FMath::Min(RasterSize.Y / TargetHeight, 1.0f))));
}

void BindTextureBrush(FSlateBrush& Brush, UTexture* Texture, FIntPoint Size)
{
	Brush.SetResourceObject(Texture);
	Brush.ImageSize = FVector2f(Size.X, Size.Y);
	// The CPU path draws whole textures; clear any region a GPU result left.
	Brush.SetUVRegion(FBox2f(ForceInit));
}

FSlateFontInfo GetLabelFont()
{
	return FCoreStyle::GetDefaultFontStyle("Regular", 8);
}

FSlateFontInfo GetHintFont()
{
	return FCoreStyle::GetDefaultFontStyle("Regular", 10);
}

TArrayView<const int32> GetWaveformGraticuleLevels()
{
	return WaveformLevels;
}

TArrayView<const int32> GetHistogramGraticuleLevels()
{
	return HistogramLevels;
}

int32 DrawBackground(FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FGeometry& AllottedGeometry, const FSlateRect& Rect)
{
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
		AllottedGeometry.ToPaintGeometry(FVector2f(Rect.GetSize()), FSlateLayoutTransform(FVector2f(Rect.GetTopLeft()))),
		FAppStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, BackgroundColor);
	return LayerId + 1;
}

int32 DrawValueAxisY(FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FGeometry& AllottedGeometry, const FSlateRect& Plot, const FSlateRect& Content,
	TArrayView<const int32> Levels)
{
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FSlateFontInfo LabelFont = GetLabelFont();

	for (int32 Level : Levels)
	{
		const float Y = Plot.Bottom - Plot.GetSize().Y * LevelFraction(Level);

		TArray<FVector2f> Line;
		Line.Add(FVector2f(Plot.Left, Y));
		Line.Add(FVector2f(Plot.Right, Y));
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(),
			Line, ESlateDrawEffect::None, GraticuleColor, /*bAntialias*/ true, 1.0f);

		const FString LabelString = FString::Printf(TEXT("%d"), Level);
		const FVector2f LabelSize = FVector2f(FontMeasure->Measure(LabelString, LabelFont));
		const float LabelY = FMath::Clamp(Y - LabelSize.Y * 0.5f, Content.Top, Content.Bottom - LabelSize.Y);
		FSlateDrawElement::MakeText(OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(LabelSize,
				FSlateLayoutTransform(FVector2f(Plot.Left - 4.0f - LabelSize.X, LabelY))),
			LabelString, LabelFont, ESlateDrawEffect::None, LabelColor);
	}

	// Minor ticks between the labelled majors (phase-6.2 Task 3): short marks
	// in the gap between label strip and plot — at fixed size, like the labels,
	// and at value positions the majors leave free, so nothing can collide.
	for (float Value : BuildMinorTickValues(Levels))
	{
		const float Y = Plot.Bottom - Plot.GetSize().Y * (Value / ScaleMax);
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(),
			{FVector2f(Plot.Left - MinorTickLength, Y), FVector2f(Plot.Left, Y)},
			ESlateDrawEffect::None, MinorTickColor, /*bAntialias*/ true, 1.0f);
	}

	return LayerId + 1;
}

int32 DrawValueAxisX(FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FGeometry& AllottedGeometry, const FSlateRect& Plot, const FSlateRect& Content,
	TArrayView<const int32> Levels)
{
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FSlateFontInfo LabelFont = GetLabelFont();

	for (int32 Level : Levels)
	{
		const float X = Plot.Left + Plot.GetSize().X * LevelFraction(Level);

		TArray<FVector2f> Line;
		Line.Add(FVector2f(X, Plot.Top));
		Line.Add(FVector2f(X, Plot.Bottom));
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(),
			Line, ESlateDrawEffect::None, GraticuleColor, /*bAntialias*/ true, 1.0f);

		const FString LabelString = FString::Printf(TEXT("%d"), Level);
		const FVector2f LabelSize = FVector2f(FontMeasure->Measure(LabelString, LabelFont));
		const float LabelX = FMath::Clamp(X - LabelSize.X * 0.5f, Content.Left, Content.Right - LabelSize.X);
		FSlateDrawElement::MakeText(OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(LabelSize, FSlateLayoutTransform(FVector2f(LabelX, Plot.Bottom + 2.0f))),
			LabelString, LabelFont, ESlateDrawEffect::None, LabelColor);
	}

	// Minor ticks between the labelled majors (phase-6.2 Task 3), pointing
	// into the bottom label strip — the histogram scale stays at the bottom
	// edge (spec), and the ticks sit between the labels' value positions.
	for (float Value : BuildMinorTickValues(Levels))
	{
		const float X = Plot.Left + Plot.GetSize().X * (Value / ScaleMax);
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(),
			{FVector2f(X, Plot.Bottom), FVector2f(X, Plot.Bottom + MinorTickLength)},
			ESlateDrawEffect::None, MinorTickColor, /*bAntialias*/ true, 1.0f);
	}

	return LayerId + 1;
}

int32 DrawCenteredHint(FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FGeometry& AllottedGeometry, const FSlateRect& Rect, const FText& Hint)
{
	TArray<FString> Lines;
	Hint.ToString().ParseIntoArray(Lines, TEXT("\n"), /*InCullEmpty*/ true);
	if (Lines.IsEmpty())
	{
		return LayerId;
	}

	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FSlateFontInfo HintFont = GetHintFont();
	const float LineHeight = FontMeasure->GetMaxCharacterHeight(HintFont);

	TArray<float, TInlineAllocator<2>> LineWidths;
	float TextWidth = 0.0f;
	for (const FString& Line : Lines)
	{
		const float Width = static_cast<float>(FontMeasure->Measure(Line, HintFont).X);
		LineWidths.Add(Width);
		TextWidth = FMath::Max(TextWidth, Width);
	}
	const float TextHeight = LineHeight * Lines.Num();

	// A hint that does not fit is worse than none: it would overdraw the scope it
	// is trying to annotate. Stay silent instead (the panel-size hint is backlog).
	const float PlateWidth = TextWidth + 2.0f * HintPlatePaddingX;
	const float PlateHeight = TextHeight + 2.0f * HintPlatePaddingY;
	if (PlateWidth > Rect.GetSize().X || PlateHeight > Rect.GetSize().Y)
	{
		return LayerId;
	}

	const FVector2f Center((Rect.Left + Rect.Right) * 0.5f, (Rect.Top + Rect.Bottom) * 0.5f);
	const FVector2f PlateTopLeft(Center.X - PlateWidth * 0.5f, Center.Y - PlateHeight * 0.5f);

	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
		AllottedGeometry.ToPaintGeometry(FVector2f(PlateWidth, PlateHeight), FSlateLayoutTransform(PlateTopLeft)),
		FAppStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, HintPlateColor);
	++LayerId;

	float LineY = Center.Y - TextHeight * 0.5f;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const FVector2f LineTopLeft(Center.X - LineWidths[Index] * 0.5f, LineY);
		FSlateDrawElement::MakeText(OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(FVector2f(LineWidths[Index], LineHeight),
				FSlateLayoutTransform(LineTopLeft)),
			Lines[Index], HintFont, ESlateDrawEffect::None, HintTextColor);
		LineY += LineHeight;
	}

	return LayerId + 1;
}

} // namespace TrueScope::Draw
