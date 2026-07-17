// Copyright 2026 TrueScope contributors
// SPDX-License-Identifier: Apache-2.0

/*
	M5 equivalence test: the GPU parade binning pass against the CPU path as
	ground truth, on the same synthetic images the CPU path is tested with.

	Counts must match exactly, except a compensating +/-1 at a bin boundary
	(GPU float vs. CPU float rounding a boundary pixel into the neighbouring
	bin): per bin |diff| <= 1, and a difference is only legal when the
	directly adjacent bin compensates it — the pair still sums correctly. A
	shifted distribution, a dropped channel, a non-adjacent or a
	non-compensating difference fails. Deliberately a strict count comparison,
	NOT a perceptual metric (bins are pixel counts, not colours).

	The GPU readback below is test-only: it runs in the test harness, never on
	the runtime path (the runtime GPU path is readback-free by design).
*/

#include "Analysis/TrueScopeAnalysis.h"
#include "Analysis/TrueScopeAnalysisResult.h"
#include "Analysis/TrueScopeGpuAnalysis.h"
#include "Math/RandomStream.h"
#include "UI/STrueScopeHistogram.h"
#include "Misc/AutomationTest.h"
#include "RHI.h"
#include "RHICommandList.h"
#include "RHIGPUReadback.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using FResult = FTrueScopeAnalysisResult;

	/**
	 * The spec's tolerance rule for one channel of one image, formalized per
	 * (column, bin) over the running prefix sum of (GPU - CPU):
	 *
	 *  - every per-bin difference is in {-1, 0, +1};
	 *  - the running prefix difference is in {-1, 0, +1} (a bigger prefix
	 *    means more than one count moved across the same boundary);
	 *  - a nonzero prefix never spans two consecutive bins (the compensation
	 *    must come from the DIRECTLY adjacent bin, so one moved count may not
	 *    travel further than one bin);
	 *  - the column total matches exactly (nothing dropped or invented).
	 */
	bool CompareParadeChannel(TArrayView<const uint32> Cpu, TArrayView<const uint32> Gpu, int32 Width,
		int32 Channel, FString& OutError)
	{
		check(Cpu.Num() == Width * FResult::NumBins);
		check(Gpu.Num() == Width * FResult::NumBins);

		for (int32 Column = 0; Column < Width; ++Column)
		{
			int64 Prefix = 0;
			bool bPreviousPrefixNonzero = false;
			for (int32 Bin = 0; Bin < FResult::NumBins; ++Bin)
			{
				const int32 Cell = Column * FResult::NumBins + Bin;
				const int64 Diff = static_cast<int64>(Gpu[Cell]) - static_cast<int64>(Cpu[Cell]);
				if (Diff < -1 || Diff > 1)
				{
					OutError = FString::Printf(
						TEXT("channel %d column %d bin %d: count diff %lld exceeds +/-1 (cpu %u, gpu %u)"),
						Channel, Column, Bin, Diff, Cpu[Cell], Gpu[Cell]);
					return false;
				}

				Prefix += Diff;
				if (Prefix < -1 || Prefix > 1)
				{
					OutError = FString::Printf(
						TEXT("channel %d column %d bin %d: more than one count moved across one boundary"),
						Channel, Column, Bin);
					return false;
				}
				if (Prefix != 0 && bPreviousPrefixNonzero)
				{
					OutError = FString::Printf(
						TEXT("channel %d column %d bin %d: difference not compensated by the adjacent bin"),
						Channel, Column, Bin);
					return false;
				}
				bPreviousPrefixNonzero = Prefix != 0;
			}
			if (Prefix != 0)
			{
				OutError = FString::Printf(
					TEXT("channel %d column %d: totals differ by %lld"), Channel, Column, Prefix);
				return false;
			}
		}
		return true;
	}

	/**
	 * The tolerance rule for the vectorscope's 2D (Cb, Cr) grid. The 1D
	 * prefix-sum formalization of the parade does not transfer to a plane, so
	 * the 2D rule is:
	 *
	 *  - every per-cell difference is in {-1, 0, +1};
	 *  - the grid total matches exactly (nothing dropped or invented);
	 *  - every nonzero difference has an opposite-sign difference among its 8
	 *    neighbours (a boundary pixel rounds CbBin and/or CrBin by one, so the
	 *    compensation must sit within Chebyshev distance 1 — a count moved
	 *    further is a mis-mapping, not rounding).
	 */
	bool CompareVectorscopeGrid(TArrayView<const uint32> Cpu, TArrayView<const uint32> Gpu, FString& OutError)
	{
		constexpr int32 N = FResult::NumVectorBins;
		check(Cpu.Num() == N * N);
		check(Gpu.Num() == N * N);

		int64 Total = 0;
		TArray<int8> Diff;
		Diff.SetNumUninitialized(N * N);
		for (int32 Cell = 0; Cell < N * N; ++Cell)
		{
			const int64 CellDiff = static_cast<int64>(Gpu[Cell]) - static_cast<int64>(Cpu[Cell]);
			if (CellDiff < -1 || CellDiff > 1)
			{
				OutError = FString::Printf(
					TEXT("cell (cb %d, cr %d): count diff %lld exceeds +/-1 (cpu %u, gpu %u)"),
					Cell % N, Cell / N, CellDiff, Cpu[Cell], Gpu[Cell]);
				return false;
			}
			Diff[Cell] = static_cast<int8>(CellDiff);
			Total += CellDiff;
		}
		if (Total != 0)
		{
			OutError = FString::Printf(TEXT("grid totals differ by %lld"), Total);
			return false;
		}

		for (int32 Cr = 0; Cr < N; ++Cr)
		{
			for (int32 Cb = 0; Cb < N; ++Cb)
			{
				const int8 CellDiff = Diff[Cr * N + Cb];
				if (CellDiff == 0)
				{
					continue;
				}
				bool bCompensated = false;
				for (int32 DCr = -1; DCr <= 1 && !bCompensated; ++DCr)
				{
					for (int32 DCb = -1; DCb <= 1 && !bCompensated; ++DCb)
					{
						const int32 NCr = Cr + DCr;
						const int32 NCb = Cb + DCb;
						if ((DCr != 0 || DCb != 0) && NCr >= 0 && NCr < N && NCb >= 0 && NCb < N
							&& Diff[NCr * N + NCb] == -CellDiff)
						{
							bCompensated = true;
						}
					}
				}
				if (!bCompensated)
				{
					OutError = FString::Printf(
						TEXT("cell (cb %d, cr %d): diff %d not compensated by any of the 8 adjacent cells"),
						Cb, Cr, CellDiff);
					return false;
				}
			}
		}
		return true;
	}

	/**
	 * Uploads Pixels into a capture-format texture, runs exactly the runtime
	 * binning pass (TrueScope::AddColumnBinningPass — parade R/G/B plus the
	 * phase-5.2 luma channel) and reads the bin buffer back. Blocking is fine
	 * here — test harness, not the runtime path.
	 */
	bool RunGpuColumnBinning(const TArray<FColor>& Pixels, FIntPoint Size, TArray<uint32>& OutBins)
	{
		const int32 NumCells = TrueScope::ColumnBinChannels * Size.X * FResult::NumBins;
		const uint32 NumBytes = NumCells * sizeof(uint32);

		bool bReadbackOk = false;
		ENQUEUE_RENDER_COMMAND(TrueScopeGpuEquivalenceRun)(
			[&Pixels, Size, NumCells, NumBytes, &OutBins, &bReadbackOk](FRHICommandListImmediate& RHICmdList)
			{
				// Same pixel format as the capture render target.
				const FRHITextureCreateDesc Desc =
					FRHITextureCreateDesc::Create2D(TEXT("TrueScope.EquivalenceInput"), Size.X, Size.Y, PF_B8G8R8A8)
						.SetFlags(ETextureCreateFlags::ShaderResource);
				FTextureRHIRef InputTexture = RHICmdList.CreateTexture(Desc);
				RHICmdList.UpdateTexture2D(InputTexture, 0,
					FUpdateTextureRegion2D(0, 0, 0, 0, Size.X, Size.Y),
					Size.X * sizeof(FColor), reinterpret_cast<const uint8*>(Pixels.GetData()));

				FRHIGPUBufferReadback Readback(TEXT("TrueScope.EquivalenceReadback"));
				{
					FRDGBuilder GraphBuilder(RHICmdList);
					FRDGTextureRef Input =
						RegisterExternalTexture(GraphBuilder, InputTexture, TEXT("TrueScope.EquivalenceInput"));
					FRDGBufferRef Bins = TrueScope::AddColumnBinningPass(GraphBuilder, Input, Size);
					AddEnqueueCopyPass(GraphBuilder, &Readback, Bins, NumBytes);
					GraphBuilder.Execute();
				}

				RHICmdList.SubmitAndBlockUntilGPUIdle();
				if (Readback.IsReady())
				{
					if (const uint32* Data = static_cast<const uint32*>(Readback.Lock(NumBytes)))
					{
						OutBins.SetNumUninitialized(NumCells);
						FMemory::Memcpy(OutBins.GetData(), Data, NumBytes);
						bReadbackOk = true;
					}
					Readback.Unlock();
				}
			});
		FlushRenderingCommands();
		return bReadbackOk;
	}

	/**
	 * Test-only round trip for the histogram (phase-5.2): uploads Pixels, runs
	 * exactly the runtime chain — column binning, then the histogram reduce —
	 * and reads back the 1D per-channel histogram plus the shared maximum.
	 */
	bool RunGpuHistogramReduce(const TArray<FColor>& Pixels, FIntPoint Size, TArray<uint32>& OutBins,
		uint32& OutSharedMax)
	{
		constexpr int32 NumCells = FResult::NumChannels * FResult::NumBins;
		constexpr uint32 NumBytes = NumCells * sizeof(uint32);

		bool bReadbackOk = false;
		ENQUEUE_RENDER_COMMAND(TrueScopeGpuHistogramEquivalenceRun)(
			[&Pixels, Size, &OutBins, &OutSharedMax, &bReadbackOk](FRHICommandListImmediate& RHICmdList)
			{
				const FRHITextureCreateDesc Desc =
					FRHITextureCreateDesc::Create2D(TEXT("TrueScope.EquivalenceInput"), Size.X, Size.Y, PF_B8G8R8A8)
						.SetFlags(ETextureCreateFlags::ShaderResource);
				FTextureRHIRef InputTexture = RHICmdList.CreateTexture(Desc);
				RHICmdList.UpdateTexture2D(InputTexture, 0,
					FUpdateTextureRegion2D(0, 0, 0, 0, Size.X, Size.Y),
					Size.X * sizeof(FColor), reinterpret_cast<const uint8*>(Pixels.GetData()));

				FRHIGPUBufferReadback BinsReadback(TEXT("TrueScope.EquivalenceReadback"));
				FRHIGPUBufferReadback MaxReadback(TEXT("TrueScope.EquivalenceMaxReadback"));
				{
					FRDGBuilder GraphBuilder(RHICmdList);
					FRDGTextureRef Input =
						RegisterExternalTexture(GraphBuilder, InputTexture, TEXT("TrueScope.EquivalenceInput"));
					FRDGBufferRef ColumnBins = TrueScope::AddColumnBinningPass(GraphBuilder, Input, Size);
					const TrueScope::FHistogramReduceOutputs Outputs =
						TrueScope::AddHistogramReducePass(GraphBuilder, ColumnBins, Size.X);
					AddEnqueueCopyPass(GraphBuilder, &BinsReadback, Outputs.Bins, NumBytes);
					AddEnqueueCopyPass(GraphBuilder, &MaxReadback, Outputs.SharedMax, sizeof(uint32));
					GraphBuilder.Execute();
				}

				RHICmdList.SubmitAndBlockUntilGPUIdle();
				if (BinsReadback.IsReady() && MaxReadback.IsReady())
				{
					const uint32* BinsData = static_cast<const uint32*>(BinsReadback.Lock(NumBytes));
					const uint32* MaxData = static_cast<const uint32*>(MaxReadback.Lock(sizeof(uint32)));
					if (BinsData && MaxData)
					{
						OutBins.SetNumUninitialized(NumCells);
						FMemory::Memcpy(OutBins.GetData(), BinsData, NumBytes);
						OutSharedMax = *MaxData;
						bReadbackOk = true;
					}
					BinsReadback.Unlock();
					MaxReadback.Unlock();
				}
			});
		FlushRenderingCommands();
		return bReadbackOk;
	}

	/**
	 * Same test-only round trip for the vectorscope: uploads Pixels, runs
	 * exactly the runtime scatter (TrueScope::AddVectorscopeBinningPass) and
	 * reads the (Cb, Cr) grid back.
	 */
	bool RunGpuVectorscopeBinning(const TArray<FColor>& Pixels, FIntPoint Size, TArray<uint32>& OutBins)
	{
		const int32 NumCells = FResult::NumVectorBins * FResult::NumVectorBins;
		const uint32 NumBytes = NumCells * sizeof(uint32);

		bool bReadbackOk = false;
		ENQUEUE_RENDER_COMMAND(TrueScopeGpuVectorscopeEquivalenceRun)(
			[&Pixels, Size, NumCells, NumBytes, &OutBins, &bReadbackOk](FRHICommandListImmediate& RHICmdList)
			{
				const FRHITextureCreateDesc Desc =
					FRHITextureCreateDesc::Create2D(TEXT("TrueScope.EquivalenceInput"), Size.X, Size.Y, PF_B8G8R8A8)
						.SetFlags(ETextureCreateFlags::ShaderResource);
				FTextureRHIRef InputTexture = RHICmdList.CreateTexture(Desc);
				RHICmdList.UpdateTexture2D(InputTexture, 0,
					FUpdateTextureRegion2D(0, 0, 0, 0, Size.X, Size.Y),
					Size.X * sizeof(FColor), reinterpret_cast<const uint8*>(Pixels.GetData()));

				FRHIGPUBufferReadback Readback(TEXT("TrueScope.EquivalenceReadback"));
				{
					FRDGBuilder GraphBuilder(RHICmdList);
					FRDGTextureRef Input =
						RegisterExternalTexture(GraphBuilder, InputTexture, TEXT("TrueScope.EquivalenceInput"));
					FRDGBufferRef Bins = TrueScope::AddVectorscopeBinningPass(GraphBuilder, Input, Size);
					AddEnqueueCopyPass(GraphBuilder, &Readback, Bins, NumBytes);
					GraphBuilder.Execute();
				}

				RHICmdList.SubmitAndBlockUntilGPUIdle();
				if (Readback.IsReady())
				{
					if (const uint32* Data = static_cast<const uint32*>(Readback.Lock(NumBytes)))
					{
						OutBins.SetNumUninitialized(NumCells);
						FMemory::Memcpy(OutBins.GetData(), Data, NumBytes);
						bReadbackOk = true;
					}
					Readback.Unlock();
				}
			});
		FlushRenderingCommands();
		return bReadbackOk;
	}

	// --- The synthetic image set the CPU path is tested against ------------

	struct FSyntheticImage
	{
		FString Name;
		FIntPoint Size = FIntPoint::ZeroValue;
		TArray<FColor> Pixels;
	};

	TArray<FSyntheticImage> MakeSyntheticImages()
	{
		TArray<FSyntheticImage> Images;

		// Solid red at the real capture resolution.
		{
			FSyntheticImage& Image = Images.Emplace_GetRef();
			Image.Name = TEXT("SolidRed960x540");
			Image.Size = FIntPoint(960, 540);
			Image.Pixels.Init(FColor(255, 0, 0, 255), Image.Size.X * Image.Size.Y);
		}

		// Horizontal black -> white gradient (diagonal parade).
		{
			FSyntheticImage& Image = Images.Emplace_GetRef();
			Image.Name = TEXT("HorizontalGradient256x64");
			Image.Size = FIntPoint(256, 64);
			Image.Pixels.SetNumUninitialized(Image.Size.X * Image.Size.Y);
			for (int32 Y = 0; Y < Image.Size.Y; ++Y)
			{
				for (int32 X = 0; X < Image.Size.X; ++X)
				{
					const uint8 V = static_cast<uint8>(X);
					Image.Pixels[Y * Image.Size.X + X] = FColor(V, V, V, 255);
				}
			}
		}

		// Vertical black -> white gradient (full-height columns).
		{
			FSyntheticImage& Image = Images.Emplace_GetRef();
			Image.Name = TEXT("VerticalGradient48x256");
			Image.Size = FIntPoint(48, 256);
			Image.Pixels.SetNumUninitialized(Image.Size.X * Image.Size.Y);
			for (int32 Y = 0; Y < Image.Size.Y; ++Y)
			{
				const uint8 V = static_cast<uint8>(Y);
				for (int32 X = 0; X < Image.Size.X; ++X)
				{
					Image.Pixels[Y * Image.Size.X + X] = FColor(V, V, V, 255);
				}
			}
		}

		// 75 % color bars (the SMPTE pattern of the CPU tests).
		{
			constexpr uint8 Bar75 = 191;
			const FColor BarColors[8] = {
				FColor(Bar75, Bar75, Bar75, 255), FColor(Bar75, Bar75, 0, 255),
				FColor(0, Bar75, Bar75, 255), FColor(0, Bar75, 0, 255),
				FColor(Bar75, 0, Bar75, 255), FColor(Bar75, 0, 0, 255),
				FColor(0, 0, Bar75, 255), FColor(0, 0, 0, 255)};

			FSyntheticImage& Image = Images.Emplace_GetRef();
			Image.Name = TEXT("ColorBars75_256x64");
			Image.Size = FIntPoint(256, 64);
			Image.Pixels.SetNumUninitialized(Image.Size.X * Image.Size.Y);
			const int32 BarWidth = Image.Size.X / 8;
			for (int32 Y = 0; Y < Image.Size.Y; ++Y)
			{
				for (int32 X = 0; X < Image.Size.X; ++X)
				{
					Image.Pixels[Y * Image.Size.X + X] = BarColors[FMath::Min(X / BarWidth, 7)];
				}
			}
		}

		// Flat mid-gray.
		{
			FSyntheticImage& Image = Images.Emplace_GetRef();
			Image.Name = TEXT("FlatMidGray64x64");
			Image.Size = FIntPoint(64, 64);
			Image.Pixels.Init(FColor(128, 128, 128, 255), Image.Size.X * Image.Size.Y);
		}

		// Solid black: the harshest bin-boundary frame — neutral (chroma 0, ON
		// the boundary between the two center vectorscope bins) and value 0
		// (the bottom edge of every value/luma axis). A float-binned path
		// flips this frame's entire population between adjacent bins
		// (phase-5.1's actual failure); integer-exact binning must not.
		{
			FSyntheticImage& Image = Images.Emplace_GetRef();
			Image.Name = TEXT("SolidBlack960x540");
			Image.Size = FIntPoint(960, 540);
			Image.Pixels.Init(FColor(0, 0, 0, 255), Image.Size.X * Image.Size.Y);
		}

		// Deterministic noise at capture resolution: every channel and bin
		// exercised at once — the densest atomic-contention case.
		{
			FSyntheticImage& Image = Images.Emplace_GetRef();
			Image.Name = TEXT("SeededNoise960x540");
			Image.Size = FIntPoint(960, 540);
			Image.Pixels.SetNumUninitialized(Image.Size.X * Image.Size.Y);
			FRandomStream Random(0x7503);
			for (FColor& Pixel : Image.Pixels)
			{
				Pixel = FColor(static_cast<uint8>(Random.RandRange(0, 255)),
					static_cast<uint8>(Random.RandRange(0, 255)),
					static_cast<uint8>(Random.RandRange(0, 255)), 255);
			}
		}

		return Images;
	}
}

// ---------------------------------------------------------------------------
// Tolerance-rule self-tests (headless): pin what the comparator accepts and,
// above all, what it must reject — the rule itself needs proven teeth.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeGpuEquivalenceToleranceRuleTest,
	"TrueScope.GPU.EquivalenceToleranceRule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeGpuEquivalenceToleranceRuleTest::RunTest(const FString&)
{
	constexpr int32 Width = 4;
	constexpr int32 NumCells = Width * FResult::NumBins;
	FString Error;

	TArray<uint32> Cpu;
	Cpu.SetNumZeroed(NumCells);
	// Column 1: some occupied bins to move counts between.
	Cpu[1 * FResult::NumBins + 100] = 10;
	Cpu[1 * FResult::NumBins + 101] = 5;
	Cpu[1 * FResult::NumBins + 200] = 3;

	// Identical arrays pass.
	TArray<uint32> Gpu = Cpu;
	TestTrue(TEXT("Identical counts pass"), CompareParadeChannel(Cpu, Gpu, Width, 0, Error));

	// One count moved to the directly adjacent bin (the legitimate boundary
	// rounding case) passes.
	Gpu = Cpu;
	Gpu[1 * FResult::NumBins + 100] -= 1;
	Gpu[1 * FResult::NumBins + 101] += 1;
	TestTrue(TEXT("Compensating +/-1 at a bin boundary passes"), CompareParadeChannel(Cpu, Gpu, Width, 0, Error));

	// A non-compensating extra count fails (something invented).
	Gpu = Cpu;
	Gpu[1 * FResult::NumBins + 101] += 1;
	TestFalse(TEXT("Non-compensating +1 fails"), CompareParadeChannel(Cpu, Gpu, Width, 0, Error));

	// A dropped count fails (something lost).
	Gpu = Cpu;
	Gpu[1 * FResult::NumBins + 100] -= 1;
	TestFalse(TEXT("Dropped count fails"), CompareParadeChannel(Cpu, Gpu, Width, 0, Error));

	// A count moved two bins away fails: only the DIRECTLY adjacent bin may
	// compensate.
	Gpu = Cpu;
	Gpu[1 * FResult::NumBins + 100] -= 1;
	Gpu[1 * FResult::NumBins + 102] += 1;
	TestFalse(TEXT("Non-adjacent compensation fails"), CompareParadeChannel(Cpu, Gpu, Width, 0, Error));

	// Two counts across the same boundary fail (|diff| 2 per bin).
	Gpu = Cpu;
	Gpu[1 * FResult::NumBins + 100] -= 2;
	Gpu[1 * FResult::NumBins + 101] += 2;
	TestFalse(TEXT("Two counts across one boundary fail"), CompareParadeChannel(Cpu, Gpu, Width, 0, Error));

	// A whole shifted distribution fails even though every per-bin diff is
	// small: consecutive boundaries all move together.
	Gpu.SetNumZeroed(NumCells);
	Gpu[1 * FResult::NumBins + 99] = 10;
	Gpu[1 * FResult::NumBins + 100] = 5;
	Gpu[1 * FResult::NumBins + 199] = 3;
	TestFalse(TEXT("Shifted distribution fails"), CompareParadeChannel(Cpu, Gpu, Width, 0, Error));

	// Content in the wrong column fails (data-to-element assignment).
	Gpu.SetNumZeroed(NumCells);
	Gpu[2 * FResult::NumBins + 100] = 10;
	Gpu[2 * FResult::NumBins + 101] = 5;
	Gpu[2 * FResult::NumBins + 200] = 3;
	TestFalse(TEXT("Counts in the wrong column fail"), CompareParadeChannel(Cpu, Gpu, Width, 0, Error));

	return true;
}

// ---------------------------------------------------------------------------
// 2D tolerance-rule self-tests (headless), same purpose as the 1D rule's: the
// comparator itself needs proven teeth before it may judge the GPU scatter.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeGpuVectorscopeToleranceRuleTest,
	"TrueScope.GPU.VectorscopeToleranceRule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeGpuVectorscopeToleranceRuleTest::RunTest(const FString&)
{
	constexpr int32 N = FResult::NumVectorBins;
	FString Error;

	TArray<uint32> Cpu;
	Cpu.SetNumZeroed(N * N);
	// A few occupied cells, asymmetric in (Cb, Cr) so a transpose is visible.
	Cpu[100 * N + 40] = 10; // (cb 40, cr 100)
	Cpu[100 * N + 41] = 5;
	Cpu[200 * N + 90] = 3;

	// Identical grids pass.
	TArray<uint32> Gpu = Cpu;
	TestTrue(TEXT("Identical grids pass"), CompareVectorscopeGrid(Cpu, Gpu, Error));

	// One count moved to a directly adjacent cell (Cb, Cr, and diagonal — the
	// legitimate boundary-rounding cases) passes.
	Gpu = Cpu;
	Gpu[100 * N + 40] -= 1;
	Gpu[100 * N + 41] += 1; // Cb neighbour
	TestTrue(TEXT("Compensating +/-1 at a Cb boundary passes"), CompareVectorscopeGrid(Cpu, Gpu, Error));
	Gpu = Cpu;
	Gpu[100 * N + 40] -= 1;
	Gpu[101 * N + 40] += 1; // Cr neighbour
	TestTrue(TEXT("Compensating +/-1 at a Cr boundary passes"), CompareVectorscopeGrid(Cpu, Gpu, Error));
	Gpu = Cpu;
	Gpu[100 * N + 40] -= 1;
	Gpu[101 * N + 41] += 1; // both boundaries at once: diagonal neighbour
	TestTrue(TEXT("Compensating +/-1 at a diagonal boundary passes"), CompareVectorscopeGrid(Cpu, Gpu, Error));

	// A non-compensating extra count fails (something invented).
	Gpu = Cpu;
	Gpu[100 * N + 41] += 1;
	TestFalse(TEXT("Non-compensating +1 fails"), CompareVectorscopeGrid(Cpu, Gpu, Error));

	// A dropped count fails (something lost).
	Gpu = Cpu;
	Gpu[100 * N + 40] -= 1;
	TestFalse(TEXT("Dropped count fails"), CompareVectorscopeGrid(Cpu, Gpu, Error));

	// A count moved two cells away fails: only an adjacent cell may compensate.
	Gpu = Cpu;
	Gpu[100 * N + 40] -= 1;
	Gpu[100 * N + 42] += 1;
	TestFalse(TEXT("Non-adjacent compensation fails"), CompareVectorscopeGrid(Cpu, Gpu, Error));

	// Two counts across the same boundary fail (|diff| 2 per cell).
	Gpu = Cpu;
	Gpu[100 * N + 40] -= 2;
	Gpu[100 * N + 41] += 2;
	TestFalse(TEXT("Two counts across one boundary fail"), CompareVectorscopeGrid(Cpu, Gpu, Error));

	// A whole distribution shifted by one Cb bin fails (dense cells produce
	// per-cell diffs far beyond +/-1).
	Gpu.SetNumZeroed(N * N);
	Gpu[100 * N + 41] = 10;
	Gpu[100 * N + 42] = 5;
	Gpu[200 * N + 91] = 3;
	TestFalse(TEXT("Shifted distribution fails"), CompareVectorscopeGrid(Cpu, Gpu, Error));

	// Swapped axes (the scatter's classic failure) fail: the transpose puts
	// counts in entirely different cells.
	Gpu.SetNumZeroed(N * N);
	Gpu[40 * N + 100] = 10;
	Gpu[41 * N + 100] = 5;
	Gpu[90 * N + 200] = 3;
	TestFalse(TEXT("Swapped Cb/Cr axes fail"), CompareVectorscopeGrid(Cpu, Gpu, Error));

	return true;
}

// ---------------------------------------------------------------------------
// The equivalence test itself (needs a real RHI: compute + readback).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeGpuAnalysisBinEquivalenceTest,
	"TrueScope.GPU.ParadeBinEquivalence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeGpuAnalysisBinEquivalenceTest::RunTest(const FString&)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("Skipped: GPU equivalence needs a real RHI (run without -nullrhi)."));
		return true;
	}

	for (const FSyntheticImage& Image : MakeSyntheticImages())
	{
		// CPU ground truth.
		FResult CpuResult;
		TrueScope::AnalyzePixels(Image.Pixels, Image.Size.X, Image.Size.Y, CpuResult);

		// The runtime GPU binning pass, read back (test-only round trip).
		TArray<uint32> GpuBins;
		if (!RunGpuColumnBinning(Image.Pixels, Image.Size, GpuBins))
		{
			AddError(FString::Printf(TEXT("%s: GPU binning readback failed"), *Image.Name));
			continue;
		}

		const int32 ChannelStride = Image.Size.X * FResult::NumBins;
		for (int32 Channel = 0; Channel < FResult::NumChannels; ++Channel)
		{
			FString Error;
			const bool bEqual = CompareParadeChannel(CpuResult.Parade[Channel],
				TArrayView<const uint32>(GpuBins.GetData() + Channel * ChannelStride, ChannelStride),
				Image.Size.X, Channel, Error);
			TestTrue(*FString::Printf(TEXT("%s: GPU bins match CPU ground truth (channel %d)"),
				*Image.Name, Channel), bEqual);
			if (!bEqual)
			{
				AddError(FString::Printf(TEXT("%s: %s"), *Image.Name, *Error));
			}
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
// Waveform equivalence (phase-5.2): the GPU luma channel of the same column
// binning pass against the CPU luma waveform as ground truth. The binning is
// integer-exact on both paths (PixelToLumaBin and its shader mirror), so the
// expectation is bit-identical counts; the comparator's compensating-±1 rule
// stays as a safety net only. The gradients and the solid-black frame are the
// key images: every neutral pixel's luma sits exactly on its code value's
// bin, where float binning flips whole populations (the phase-5.1 failure
// class this phase must not reintroduce).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeGpuWaveformBinEquivalenceTest,
	"TrueScope.GPU.WaveformBinEquivalence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeGpuWaveformBinEquivalenceTest::RunTest(const FString&)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("Skipped: GPU equivalence needs a real RHI (run without -nullrhi)."));
		return true;
	}

	for (const FSyntheticImage& Image : MakeSyntheticImages())
	{
		// CPU ground truth.
		FResult CpuResult;
		TrueScope::AnalyzePixels(Image.Pixels, Image.Size.X, Image.Size.Y, CpuResult);

		// The runtime GPU binning pass, read back (test-only round trip).
		TArray<uint32> GpuBins;
		if (!RunGpuColumnBinning(Image.Pixels, Image.Size, GpuBins))
		{
			AddError(FString::Printf(TEXT("%s: GPU binning readback failed"), *Image.Name));
			continue;
		}

		const int32 ChannelStride = Image.Size.X * FResult::NumBins;
		FString Error;
		const bool bEqual = CompareParadeChannel(CpuResult.LumaParade,
			TArrayView<const uint32>(GpuBins.GetData() + TrueScope::LumaBinChannel * ChannelStride, ChannelStride),
			Image.Size.X, TrueScope::LumaBinChannel, Error);
		TestTrue(*FString::Printf(TEXT("%s: GPU luma bins match the CPU luma waveform"), *Image.Name), bEqual);
		if (!bEqual)
		{
			AddError(FString::Printf(TEXT("%s: %s"), *Image.Name, *Error));
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
// Histogram equivalence (phase-5.2): the GPU reduce chain (column binning ->
// histogram reduce) against the CPU histogram as ground truth, bin counts and
// the shared strongest-channel maximum — the one scale all three curves share.
// The histogram is a deterministic column sum, so the expectation is exact.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeGpuHistogramBinEquivalenceTest,
	"TrueScope.GPU.HistogramBinEquivalence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeGpuHistogramBinEquivalenceTest::RunTest(const FString&)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("Skipped: GPU equivalence needs a real RHI (run without -nullrhi)."));
		return true;
	}

	for (const FSyntheticImage& Image : MakeSyntheticImages())
	{
		// CPU ground truth.
		FResult CpuResult;
		TrueScope::AnalyzePixels(Image.Pixels, Image.Size.X, Image.Size.Y, CpuResult);

		// The runtime GPU binning + reduce, read back (test-only round trip).
		TArray<uint32> GpuBins;
		uint32 GpuSharedMax = 0;
		if (!RunGpuHistogramReduce(Image.Pixels, Image.Size, GpuBins, GpuSharedMax))
		{
			AddError(FString::Printf(TEXT("%s: GPU histogram readback failed"), *Image.Name));
			continue;
		}

		// The 1D histogram is one 256-bin column per channel, so the parade
		// comparator applies with Width = 1.
		for (int32 Channel = 0; Channel < FResult::NumChannels; ++Channel)
		{
			FString Error;
			const bool bEqual = CompareParadeChannel(CpuResult.Histogram[Channel],
				TArrayView<const uint32>(GpuBins.GetData() + Channel * FResult::NumBins, FResult::NumBins),
				/*Width*/ 1, Channel, Error);
			TestTrue(*FString::Printf(TEXT("%s: GPU histogram matches CPU ground truth (channel %d)"),
				*Image.Name, Channel), bEqual);
			if (!bEqual)
			{
				AddError(FString::Printf(TEXT("%s: %s"), *Image.Name, *Error));
			}
		}

		// The shared curve scale must be the same number the CPU draw derives.
		TestEqual(*FString::Printf(TEXT("%s: GPU shared maximum matches the CPU shared scale"), *Image.Name),
			GpuSharedMax, STrueScopeHistogram::GetSharedMaxCount(CpuResult));
	}

	return true;
}

// ---------------------------------------------------------------------------
// Histogram LUMA equivalence (phase-6.2): the reduce's fourth channel — the
// YRGB mode's luma curve — against the CPU widget's luma histogram, which is
// itself the column sum of the waveform's integer-exact luma bins. Pins both
// halves of the phase-6.2 provenance requirement at once: the GPU luma
// histogram equals the CPU one, and both derive from the SAME luma bins the
// waveform plots (no second luma computation on either path). Deliberately a
// separate test so the phase-5.2 bin-equivalence tests stay untouched.
// ---------------------------------------------------------------------------

namespace
{
	/**
	 * Test-only round trip for the luma reduce (phase-6.2): the same runtime
	 * chain as RunGpuHistogramReduce, reading back ALL reduce channels (the
	 * three colour channels plus luma) and the luma maximum.
	 */
	bool RunGpuHistogramLumaReduce(const TArray<FColor>& Pixels, FIntPoint Size, TArray<uint32>& OutBins,
		uint32& OutLumaMax)
	{
		constexpr int32 NumCells = TrueScope::ColumnBinChannels * FResult::NumBins;
		constexpr uint32 NumBytes = NumCells * sizeof(uint32);

		bool bReadbackOk = false;
		ENQUEUE_RENDER_COMMAND(TrueScopeGpuHistogramLumaEquivalenceRun)(
			[&Pixels, Size, &OutBins, &OutLumaMax, &bReadbackOk](FRHICommandListImmediate& RHICmdList)
			{
				const FRHITextureCreateDesc Desc =
					FRHITextureCreateDesc::Create2D(TEXT("TrueScope.EquivalenceInput"), Size.X, Size.Y, PF_B8G8R8A8)
						.SetFlags(ETextureCreateFlags::ShaderResource);
				FTextureRHIRef InputTexture = RHICmdList.CreateTexture(Desc);
				RHICmdList.UpdateTexture2D(InputTexture, 0,
					FUpdateTextureRegion2D(0, 0, 0, 0, Size.X, Size.Y),
					Size.X * sizeof(FColor), reinterpret_cast<const uint8*>(Pixels.GetData()));

				FRHIGPUBufferReadback BinsReadback(TEXT("TrueScope.EquivalenceReadback"));
				FRHIGPUBufferReadback MaxReadback(TEXT("TrueScope.EquivalenceMaxReadback"));
				{
					FRDGBuilder GraphBuilder(RHICmdList);
					FRDGTextureRef Input =
						RegisterExternalTexture(GraphBuilder, InputTexture, TEXT("TrueScope.EquivalenceInput"));
					FRDGBufferRef ColumnBins = TrueScope::AddColumnBinningPass(GraphBuilder, Input, Size);
					const TrueScope::FHistogramReduceOutputs Outputs =
						TrueScope::AddHistogramReducePass(GraphBuilder, ColumnBins, Size.X);
					AddEnqueueCopyPass(GraphBuilder, &BinsReadback, Outputs.Bins, NumBytes);
					AddEnqueueCopyPass(GraphBuilder, &MaxReadback, Outputs.LumaMax, sizeof(uint32));
					GraphBuilder.Execute();
				}

				RHICmdList.SubmitAndBlockUntilGPUIdle();
				if (BinsReadback.IsReady() && MaxReadback.IsReady())
				{
					const uint32* BinsData = static_cast<const uint32*>(BinsReadback.Lock(NumBytes));
					const uint32* MaxData = static_cast<const uint32*>(MaxReadback.Lock(sizeof(uint32)));
					if (BinsData && MaxData)
					{
						OutBins.SetNumUninitialized(NumCells);
						FMemory::Memcpy(OutBins.GetData(), BinsData, NumBytes);
						OutLumaMax = *MaxData;
						bReadbackOk = true;
					}
					BinsReadback.Unlock();
					MaxReadback.Unlock();
				}
			});
		FlushRenderingCommands();
		return bReadbackOk;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeGpuHistogramLumaBinEquivalenceTest,
	"TrueScope.GPU.HistogramLumaBinEquivalence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeGpuHistogramLumaBinEquivalenceTest::RunTest(const FString&)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("Skipped: GPU equivalence needs a real RHI (run without -nullrhi)."));
		return true;
	}

	for (const FSyntheticImage& Image : MakeSyntheticImages())
	{
		// CPU ground truth: the widget's own luma histogram derivation — the
		// column sum of the waveform's integer-exact luma bins.
		FResult CpuResult;
		TrueScope::AnalyzePixels(Image.Pixels, Image.Size.X, Image.Size.Y, CpuResult);
		const TArray<uint32> CpuLumaBins = STrueScopeHistogram::BuildLumaHistogramBins(CpuResult);
		if (CpuLumaBins.Num() != FResult::NumBins)
		{
			AddError(FString::Printf(TEXT("%s: CPU luma histogram bins missing"), *Image.Name));
			continue;
		}

		TArray<uint32> GpuBins;
		uint32 GpuLumaMax = 0;
		if (!RunGpuHistogramLumaReduce(Image.Pixels, Image.Size, GpuBins, GpuLumaMax))
		{
			AddError(FString::Printf(TEXT("%s: GPU luma histogram readback failed"), *Image.Name));
			continue;
		}

		// The luma binning is integer-exact on both paths (phase-5.2), and the
		// reduce is a deterministic sum — so the expectation is EXACT equality,
		// stricter than the parade's +/-1 boundary rule.
		const TArrayView<const uint32> GpuLumaBins(
			GpuBins.GetData() + TrueScope::LumaBinChannel * FResult::NumBins, FResult::NumBins);
		uint32 CpuLumaMax = 0;
		bool bBinsEqual = true;
		for (int32 Bin = 0; Bin < FResult::NumBins; ++Bin)
		{
			CpuLumaMax = FMath::Max(CpuLumaMax, CpuLumaBins[Bin]);
			if (GpuLumaBins[Bin] != CpuLumaBins[Bin])
			{
				AddError(FString::Printf(TEXT("%s: luma bin %d differs (cpu %u, gpu %u)"),
					*Image.Name, Bin, CpuLumaBins[Bin], GpuLumaBins[Bin]));
				bBinsEqual = false;
			}
		}
		TestTrue(*FString::Printf(TEXT("%s: GPU luma histogram matches the CPU luma histogram exactly"),
			*Image.Name), bBinsEqual);

		// The YRGB scale ingredient: LumaMax is the luma histogram's peak, and
		// max(SharedMax, LumaMax) equals the CPU widget's YRGB shared maximum.
		TestEqual(*FString::Printf(TEXT("%s: GPU luma maximum matches the CPU luma peak"), *Image.Name),
			GpuLumaMax, CpuLumaMax);
		TestEqual(*FString::Printf(TEXT("%s: YRGB shared maximum agrees between the paths"), *Image.Name),
			FMath::Max(GpuLumaMax, STrueScopeHistogram::GetSharedMaxCount(CpuResult)),
			STrueScopeHistogram::GetYrgbSharedMaxCount(CpuResult, CpuLumaBins));
	}

	return true;
}

// ---------------------------------------------------------------------------
// Vectorscope equivalence (phase-5.1): the GPU (Cb, Cr) scatter against the
// CPU vectorscope as ground truth. Colorbars are the key image here — a solid
// frame cannot reveal an axis swap, the bars' six distinct chroma points can.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrueScopeGpuVectorscopeBinEquivalenceTest,
	"TrueScope.GPU.VectorscopeBinEquivalence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrueScopeGpuVectorscopeBinEquivalenceTest::RunTest(const FString&)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("Skipped: GPU equivalence needs a real RHI (run without -nullrhi)."));
		return true;
	}

	for (const FSyntheticImage& Image : MakeSyntheticImages())
	{
		// CPU ground truth.
		FResult CpuResult;
		TrueScope::AnalyzePixels(Image.Pixels, Image.Size.X, Image.Size.Y, CpuResult);

		// The runtime GPU scatter, read back (test-only round trip).
		TArray<uint32> GpuBins;
		if (!RunGpuVectorscopeBinning(Image.Pixels, Image.Size, GpuBins))
		{
			AddError(FString::Printf(TEXT("%s: GPU vectorscope readback failed"), *Image.Name));
			continue;
		}

		FString Error;
		const bool bEqual = CompareVectorscopeGrid(CpuResult.Vectorscope, GpuBins, Error);
		TestTrue(*FString::Printf(TEXT("%s: GPU (Cb, Cr) grid matches CPU ground truth"), *Image.Name), bEqual);
		if (!bEqual)
		{
			AddError(FString::Printf(TEXT("%s: %s"), *Image.Name, *Error));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
