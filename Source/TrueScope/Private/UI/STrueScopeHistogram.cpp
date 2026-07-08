// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "UI/STrueScopeHistogram.h"

#include "Analysis/TrueScopeAnalysis.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"

namespace
{
	// Fixed-size chrome (architecture guardrail: labels/axes never scale with
	// the panel).
	constexpr float PlotPadding = 6.0f;
	constexpr float LabelStripHeight = 14.0f;

	const FLinearColor BackgroundColor(0.008f, 0.008f, 0.008f);
	const FLinearColor GraticuleColor(0.5f, 0.5f, 0.5f, 0.22f);
	const FLinearColor LabelColor(0.6f, 0.6f, 0.6f, 0.9f);

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

int32 STrueScopeHistogram::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FSlateRect Content = GetScopeContentRect(AllottedGeometry);
	const FSlateBrush* WhiteBrush = FAppStyle::Get().GetBrush("WhiteBrush");

	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
		AllottedGeometry.ToPaintGeometry(FVector2f(Content.GetSize()), FSlateLayoutTransform(FVector2f(Content.GetTopLeft()))),
		WhiteBrush, ESlateDrawEffect::None, BackgroundColor);
	++LayerId;

	// Plot area: fixed-size padding and bottom label strip.
	const FSlateRect Plot(Content.Left + PlotPadding, Content.Top + PlotPadding,
		Content.Right - PlotPadding, Content.Bottom - PlotPadding - LabelStripHeight);
	if (Plot.GetSize().X < 16.0f || Plot.GetSize().Y < 16.0f)
	{
		return LayerId;
	}

	// Graticule + labels: verticals at 0/25/50/75/100 % of the value range.
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FSlateFontInfo LabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);
	for (int32 Percent = 0; Percent <= 100; Percent += 25)
	{
		const float X = Plot.Left + Plot.GetSize().X * (Percent / 100.0f);

		TArray<FVector2f> Line;
		Line.Add(FVector2f(X, Plot.Top));
		Line.Add(FVector2f(X, Plot.Bottom));
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(),
			Line, ESlateDrawEffect::None, GraticuleColor, /*bAntialias*/ true, 1.0f);

		const FString LabelString = FString::Printf(TEXT("%d"), Percent);
		const FVector2f LabelSize = FVector2f(FontMeasure->Measure(LabelString, LabelFont));
		const float LabelX = FMath::Clamp(X - LabelSize.X * 0.5f, Content.Left, Content.Right - LabelSize.X);
		FSlateDrawElement::MakeText(OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(LabelSize, FSlateLayoutTransform(FVector2f(LabelX, Plot.Bottom + 2.0f))),
			LabelString, LabelFont, ESlateDrawEffect::None, LabelColor);
	}
	++LayerId;

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

	constexpr int32 NumBins = FTrueScopeAnalysisResult::NumBins;
	for (int32 Channel = 0; Channel < FTrueScopeAnalysisResult::NumChannels; ++Channel)
	{
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
			const float X = Plot.Left + Plot.GetSize().X * (static_cast<float>(Bin) / (NumBins - 1));
			const float Intensity = TrueScope::CountToIntensity(Result->Histogram[Channel][Bin], MaxCount);
			const float TopY = Plot.Bottom - Plot.GetSize().Y * Intensity;
			TopLine.Add(FVector2f(X, TopY));

			FSlateVertex Bottom;
			Bottom.Position = FVector2f(AllottedGeometry.LocalToAbsolute(FVector2D(X, Plot.Bottom)));
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
