// Copyright 2026 Michael Herber
// SPDX-License-Identifier: Apache-2.0

#include "Analysis/TrueScopeRasterSizes.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Phase-6.1 Task 1: the GPU rasters follow the panel pixel for pixel (one texel
 * = one device pixel), but the render targets are ALLOCATED in buckets — the
 * two rules together are what makes a splitter drag cheap without giving up the
 * 1:1 raster. These tests pin both halves:
 *
 * - a drag across a bucket's width must not reallocate anything, and
 * - the allocation must always contain the requested raster, so the pass can
 *   never write outside its target.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeRasterBucketTest,
	"TrueScope.UI.RasterBucket",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeRasterBucketTest::RunTest(const FString&)
{
	using namespace TrueScope;

	// A request is the plot rect in device pixels, rounded up and clamped.
	TestEqual(TEXT("Fractional device size rounds up"), ClampRasterSize(FVector2f(600.2f, 337.8f)),
		FIntPoint(601, 338));
	TestEqual(TEXT("Tiny panels clamp to the minimum side"), ClampRasterSize(FVector2f(3.0f, 0.0f)),
		FIntPoint(MinRasterSide, MinRasterSide));
	TestEqual(TEXT("Absurd requests clamp to the ceiling"), ClampRasterSize(FVector2f(99999.0f, 99999.0f)),
		FIntPoint(MaxRasterSide, MaxRasterSide));

	// The allocation contains the raster — for every size the widgets can ask
	// for, including both clamps. A raster larger than its target would write
	// outside the texture.
	for (int32 Side = MinRasterSide; Side <= MaxRasterSide; Side += 7)
	{
		const FIntPoint Requested(Side, Side);
		const FIntPoint Allocation = BucketRasterSize(Requested);
		if (Allocation.X < Requested.X || Allocation.Y < Requested.Y)
		{
			AddError(FString::Printf(TEXT("Allocation %dx%d is smaller than the requested raster %dx%d"),
				Allocation.X, Allocation.Y, Requested.X, Requested.Y));
			return false;
		}
	}

	// No churn: dragging a splitter across one bucket's worth of pixels must
	// leave the allocation untouched — this is the whole point of bucketing.
	const FIntPoint DragStart = BucketRasterSize(FIntPoint(MinRasterSide + 1, MinRasterSide + 1));
	for (int32 Pixel = MinRasterSide + 1; Pixel <= MinRasterSide + RasterBucketStep; ++Pixel)
	{
		TestEqual(TEXT("Allocation is constant within a bucket"),
			BucketRasterSize(FIntPoint(Pixel, Pixel)), DragStart);
	}
	TestNotEqual(TEXT("Crossing the bucket boundary does reallocate"),
		BucketRasterSize(FIntPoint(MinRasterSide + RasterBucketStep + 1, MinRasterSide)), DragStart);

	// A full drag of 200 px reallocates a handful of times, not 200 times.
	int32 Reallocations = 0;
	FIntPoint Previous = BucketRasterSize(FIntPoint(400, 300));
	for (int32 Pixel = 401; Pixel <= 600; ++Pixel)
	{
		const FIntPoint Allocation = BucketRasterSize(FIntPoint(Pixel, 300));
		if (Allocation != Previous)
		{
			++Reallocations;
			Previous = Allocation;
		}
	}
	TestEqual(TEXT("A 200 px drag crosses only the bucket boundaries"), Reallocations,
		200 / RasterBucketStep);

	// The registry hands the producer back exactly what a widget published.
	FTrueScopeRasterSizes Sizes;
	TestEqual(TEXT("Unrequested targets read back as zero"),
		Sizes.GetRequested(ERasterTarget::Vectorscope), FIntPoint::ZeroValue);
	Sizes.Request(ERasterTarget::Vectorscope, FIntPoint(512, 512));
	Sizes.Request(ERasterTarget::HistogramBand, FIntPoint(900, 141));
	TestEqual(TEXT("Vectorscope request round-trips"), Sizes.GetRequested(ERasterTarget::Vectorscope),
		FIntPoint(512, 512));
	TestEqual(TEXT("Histogram band request round-trips"), Sizes.GetRequested(ERasterTarget::HistogramBand),
		FIntPoint(900, 141));
	TestEqual(TEXT("Requests do not leak between targets"), Sizes.GetRequested(ERasterTarget::Waveform),
		FIntPoint::ZeroValue);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
