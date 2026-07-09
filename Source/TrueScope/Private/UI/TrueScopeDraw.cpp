// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "UI/TrueScopeDraw.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"

namespace TrueScope::Draw
{

namespace
{
	/** Every 128 code values; the last line sits on full scale, not on 1024. */
	constexpr int32 WaveformLevels[] = {0, 128, 256, 384, 512, 640, 768, 896, ScaleMax};

	/** Quarters of the same axis — enough for the histogram's value labels. */
	constexpr int32 HistogramLevels[] = {0, 256, 512, 768, ScaleMax};

	/** Fraction of the value axis a 10-bit level sits at. */
	float LevelFraction(int32 Level)
	{
		return Level / static_cast<float>(ScaleMax);
	}
}

FSlateFontInfo GetLabelFont()
{
	return FCoreStyle::GetDefaultFontStyle("Regular", 8);
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

	return LayerId + 1;
}

} // namespace TrueScope::Draw
