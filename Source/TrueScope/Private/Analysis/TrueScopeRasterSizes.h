// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"

#include <atomic>

namespace TrueScope
{
	/**
	 * The scope rasters the GPU path produces. Each is sized to the rect the
	 * owning widget draws it into, in DEVICE pixels — the anti-pixelation rule
	 * ("scope textures at display resolution or higher", architecture.md): one
	 * raster texel is one device pixel, so Slate's bilinear upscale becomes an
	 * identity and the GPU stroke has the same width as the CPU/Slate line.
	 *
	 * Parade and histogram-stacked have three targets each (one per channel),
	 * but the three are congruent, so one size covers the set.
	 */
	enum class ERasterTarget : uint8
	{
		/** One channel section of the parade. */
		ParadeSection = 0,

		/** The waveform's plot area (both trace targets, RGB and Luma). */
		Waveform,

		/** The vectorscope's square plot (aspect-locked, so width == height). */
		Vectorscope,

		/** One channel band of the histogram's Separate Channels mode. */
		HistogramBand,

		/** The histogram's whole plot (RGB Overlay mode). */
		HistogramOverlay,

		Num
	};

	/**
	 * Allocation granularity of the raster targets. The requested size follows
	 * the panel pixel for pixel, but the render target is only reallocated when
	 * the request crosses a bucket — dragging a splitter would otherwise
	 * reallocate every target on every mouse move. The raster is drawn into the
	 * exact requested region of the bucketed target, and the widget samples only
	 * that region (FSlateBrush UV region), so bucketing costs a little memory,
	 * never accuracy.
	 */
	inline constexpr int32 RasterBucketStep = 64;

	/** A scope smaller than this is not drawn at all (Task 5's hint territory). */
	inline constexpr int32 MinRasterSide = 64;

	/** Ceiling for a raster axis: ~8K panels, and a guard against absurd requests. */
	inline constexpr int32 MaxRasterSide = 8192;

	/** Device-pixel size a widget asks for, clamped to the drawable range. */
	inline FIntPoint ClampRasterSize(FVector2f DeviceSize)
	{
		return FIntPoint(
			FMath::Clamp(FMath::CeilToInt(DeviceSize.X), MinRasterSide, MaxRasterSide),
			FMath::Clamp(FMath::CeilToInt(DeviceSize.Y), MinRasterSide, MaxRasterSide));
	}

	/** Allocated size for a requested raster: the request bucketed up. */
	inline FIntPoint BucketRasterSize(FIntPoint RequestedSize)
	{
		auto BucketAxis = [](int32 Value)
		{
			const int32 Clamped = FMath::Clamp(Value, MinRasterSide, MaxRasterSide);
			const int32 Bucketed = FMath::DivideAndRoundUp(Clamped, RasterBucketStep) * RasterBucketStep;
			return FMath::Min(Bucketed, MaxRasterSide);
		};
		return FIntPoint(BucketAxis(RequestedSize.X), BucketAxis(RequestedSize.Y));
	}
}

/**
 * The scope widgets' current plot rects in device pixels, handed to the GPU
 * analysis producer so it can rasterize at display resolution (phase-6.1
 * Task 1). Each widget publishes the size of the rect it draws its raster into
 * on every paint; the producer reads the requests once per capture, resizes its
 * render targets when a request crosses a bucket, and rasterizes into the exact
 * requested region.
 *
 * Written and read on the game thread (Slate paints there, and the producer's
 * resize/cache step runs in BeginRenderViewFamily); the values are atomics so a
 * future off-thread reader cannot tear a size in half.
 */
class FTrueScopeRasterSizes
{
public:
	/** Publish the device-pixel rect a scope draws its raster into. */
	void Request(TrueScope::ERasterTarget Target, FIntPoint DeviceSize)
	{
		Packed[static_cast<int32>(Target)].store(Pack(DeviceSize), std::memory_order_relaxed);
	}

	/** The last requested size, or a zero point while no widget has painted yet. */
	FIntPoint GetRequested(TrueScope::ERasterTarget Target) const
	{
		return Unpack(Packed[static_cast<int32>(Target)].load(std::memory_order_relaxed));
	}

private:
	static uint64 Pack(FIntPoint Size)
	{
		return (static_cast<uint64>(static_cast<uint32>(Size.X)) << 32)
			| static_cast<uint64>(static_cast<uint32>(Size.Y));
	}

	static FIntPoint Unpack(uint64 Value)
	{
		return FIntPoint(static_cast<int32>(Value >> 32), static_cast<int32>(Value & 0xFFFFFFFFull));
	}

	std::atomic<uint64> Packed[static_cast<int32>(TrueScope::ERasterTarget::Num)] = {};
};
