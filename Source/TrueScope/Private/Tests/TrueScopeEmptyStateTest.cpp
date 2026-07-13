// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#include "Analysis/TrueScopeAnalysis.h"
#include "Analysis/TrueScopeAnalysisResult.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/AutomationTest.h"
#include "Rendering/DrawElements.h"
#include "UI/STrueScopeHistogram.h"
#include "UI/STrueScopeParade.h"
#include "UI/STrueScopeVectorscope.h"
#include "UI/STrueScopeWaveform.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWindow.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
	Phase-5.2 unified empty-state: when a scope has no data to draw, all four
	scopes must say so identically (the DrawCenteredHint mechanism), and the
	decision must be per-scope data-aware — no scope may silently hold a stale
	trace while another blanks. These assertions drive the widgets'
	HasDrawableData contract and the DrawScopeHints paint branch directly.
*/

namespace
{
	using FResult = FTrueScopeAnalysisResult;

	struct FScopeUnderTest
	{
		const TCHAR* Name;
		TSharedRef<STrueScopeWidget> Widget;
	};

	TArray<FScopeUnderTest> MakeAllScopes()
	{
		return {
			{TEXT("Waveform"), SNew(STrueScopeWaveform, nullptr)},
			{TEXT("Parade"), SNew(STrueScopeParade, nullptr)},
			{TEXT("Vectorscope"), SNew(STrueScopeVectorscope, nullptr)},
			{TEXT("Histogram"), SNew(STrueScopeHistogram, nullptr)},
		};
	}

	/** A complete CPU-path result, as the analyzer publishes it. */
	TSharedRef<FResult, ESPMode::ThreadSafe> MakeCpuResult()
	{
		constexpr int32 Width = 16;
		constexpr int32 Height = 8;
		TArray<FColor> Pixels;
		Pixels.Init(FColor(128, 64, 192, 255), Width * Height);

		TSharedRef<FResult, ESPMode::ThreadSafe> Result = MakeShared<FResult, ESPMode::ThreadSafe>();
		TrueScope::AnalyzePixels(Pixels, Width, Height, *Result);
		TrueScope::BuildParadeImage(*Result);
		TrueScope::BuildLumaWaveformImage(*Result);
		TrueScope::BuildVectorscopeImage(*Result);
		return Result;
	}

	/** A GPU-path result, as the GPU producer publishes it (targets not resolvable here). */
	TSharedRef<FResult, ESPMode::ThreadSafe> MakeGpuResult()
	{
		TSharedRef<FResult, ESPMode::ThreadSafe> Result = MakeShared<FResult, ESPMode::ThreadSafe>();
		Result->SourceSize = FIntPoint(16, 8);
		Result->ParadeImageSize = FIntPoint(16, FResult::NumBins);
		Result->bHasGpuParade = true;
		Result->bHasGpuVectorscope = true;
		Result->bHasGpuWaveform = true;
		Result->bHasGpuHistogram = true;
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeEmptyStateTest,
	"TrueScope.UI.EmptyState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeEmptyStateTest::RunTest(const FString&)
{
	// --- No result at all: every scope reports "nothing to draw", uniformly.
	for (const FScopeUnderTest& Scope : MakeAllScopes())
	{
		TestFalse(*FString::Printf(TEXT("%s: no result means no drawable data"), Scope.Name),
			Scope.Widget->HasDrawableData());
	}

	// --- A published but empty result must not count as data either.
	{
		TSharedRef<FResult, ESPMode::ThreadSafe> Empty = MakeShared<FResult, ESPMode::ThreadSafe>();
		for (const FScopeUnderTest& Scope : MakeAllScopes())
		{
			Scope.Widget->SetCachedResultForTesting(Empty);
			TestFalse(*FString::Printf(TEXT("%s: an empty result is no drawable data"), Scope.Name),
				Scope.Widget->HasDrawableData());
		}
	}

	// --- Both producers' complete results count as data on all four scopes.
	{
		const TSharedRef<FResult, ESPMode::ThreadSafe> Cpu = MakeCpuResult();
		const TSharedRef<FResult, ESPMode::ThreadSafe> Gpu = MakeGpuResult();
		for (const FScopeUnderTest& Scope : MakeAllScopes())
		{
			Scope.Widget->SetCachedResultForTesting(Cpu);
			TestTrue(*FString::Printf(TEXT("%s: a CPU result is drawable"), Scope.Name),
				Scope.Widget->HasDrawableData());
			Scope.Widget->SetCachedResultForTesting(Gpu);
			TestTrue(*FString::Printf(TEXT("%s: a GPU result is drawable"), Scope.Name),
				Scope.Widget->HasDrawableData());
		}
	}

	// --- The decision is per scope: a result carrying only the vectorscope's
	// data must blank the other three (they may not hold a stale trace while
	// a neighbour draws).
	{
		TSharedRef<FResult, ESPMode::ThreadSafe> VectorOnly = MakeShared<FResult, ESPMode::ThreadSafe>();
		VectorOnly->SourceSize = FIntPoint(16, 8);
		VectorOnly->bHasGpuVectorscope = true;
		TArray<FScopeUnderTest> Scopes = MakeAllScopes();
		for (const FScopeUnderTest& Scope : Scopes)
		{
			Scope.Widget->SetCachedResultForTesting(VectorOnly);
		}
		TestFalse(TEXT("Waveform blanks on a vectorscope-only result"), Scopes[0].Widget->HasDrawableData());
		TestFalse(TEXT("Parade blanks on a vectorscope-only result"), Scopes[1].Widget->HasDrawableData());
		TestTrue(TEXT("Vectorscope draws its own data"), Scopes[2].Widget->HasDrawableData());
		TestFalse(TEXT("Histogram blanks on a vectorscope-only result"), Scopes[3].Widget->HasDrawableData());
	}

	// --- The paint branch itself: an empty live scope draws the hint, a scope
	// with data draws nothing extra. Drawing needs the font-measure service,
	// so the element assertions run only under an initialized Slate
	// application (as in the paused-state test); the state assertions above
	// hold either way.
	if (FSlateApplication::IsInitialized())
	{
		const FGeometry Geometry = FGeometry::MakeRoot(FVector2f(400.0f, 300.0f), FSlateLayoutTransform());
		const FSlateRect Content(0.0f, 0.0f, 400.0f, 300.0f);
		constexpr int32 BaseLayer = 7;
		const TSharedPtr<SWindow> NoWindow;

		for (const FScopeUnderTest& Scope : MakeAllScopes())
		{
			{
				FSlateWindowElementList Elements(NoWindow);
				TestTrue(*FString::Printf(TEXT("%s: empty live scope draws the shared hint"), Scope.Name),
					Scope.Widget->DrawScopeHints(Elements, BaseLayer, Geometry, Content) > BaseLayer);
			}
			{
				Scope.Widget->SetCachedResultForTesting(MakeGpuResult());
				FSlateWindowElementList Elements(NoWindow);
				TestEqual(*FString::Printf(TEXT("%s: scope with data draws no hint"), Scope.Name),
					Scope.Widget->DrawScopeHints(Elements, BaseLayer, Geometry, Content), BaseLayer);
			}
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
