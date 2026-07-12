# Phase 5 — GPU compute analysis path (no runtime readback)

Read `architecture.md` first: the "Analysis, stage 2" and "Analysis path
selection" rows, the "Two distinct count mappings" row, the "Cross-platform
parity outranks optimization" settled decision, and "Measured: controlled
readback benchmark". Also read `phase-3.md` "Correction" (why no readback) and
`backlog.md` → "Capture runs at full cost while the tab is closed".

Phase 3/4 delivered the four scopes on the **CPU path** (async readback →
`AnalyzePixels` → `FTrueScopeAnalysisResult` → Slate). This phase adds a **GPU
compute path** that produces the same result **without ever reading pixel data
back to the CPU**, validated against the CPU path as ground truth.

**Scope of THIS phase: one scope only — the Parade** (its 2D column×luma
mechanism is the hardest; proving it de-risks the rest). Waveform, Histogram,
Vectorscope on the GPU path are explicitly deferred to a follow-up phase (M5.x),
written after this one is signed off. The CPU path stays the default until the
GPU path is validated on all three platforms (M7).

This is a heavy render-pipeline phase. Expect to consult the engine source
repeatedly; expect iteration. If a decision-level surprise appears, **stop and
ask** per the `dev-rules.md` Rule 3 self-check — do not silently re-architect.

## Why no readback (the load-bearing constraint)

The controlled benchmark (2026-07-11) measured the readback `Lock` at ≈2× host
frame-time, size-independent, because Metal's `RHIMapStagingSurface` drains the
in-flight GPU queue. A GPU path that computed bins on the GPU and then read the
*bins* back would pay the same block — it would save nothing. Therefore the GPU
path must keep its result **GPU-resident end to end**: compute writes bins into
a GPU buffer → a second pass turns bins into the intensity/vertex data the scope
draws → Slate draws that GPU resource directly. **No `FRHIGPUTextureReadback` on
the runtime path.** (The equivalence *test* below may read back — it runs in the
test harness, not at runtime.)

## Cross-platform constraint (parity rule)

- **`uint` atomics only** (`InterlockedAdd` on `RWBuffer<uint>` / `RWTexture`).
  No float atomics, no wave/subgroup intrinsics — availability varies by RHI and
  feature level; uint atomics run everywhere. This is a hard requirement, not a
  preference (parity rule).
- Nothing platform-specific in the shader or its host code paths. If a portable
  construct is slower than a platform-specific one, the portable one wins.

## Task 0 — Research before writing code (mandatory)

Do not write the compute path from memory. In the installed 5.8 source:

- **Find in-engine GPU histogram / binning compute users** (grep the renderer
  and engine plugins for `InterlockedAdd`, RDG compute passes writing to
  `RWBuffer<uint>`, e.g. auto-exposure/eye-adaptation histogram, GPU particle
  binning). Copy a real pattern rather than inventing one.
- **Plugin shader setup on this engine:** how a plugin registers a virtual
  shader directory (`AddShaderSourceDirectoryMapping`) at the right loading
  phase (`PostConfigInit`, already set on the module in M1), how `.usf` lives
  under `Shaders/`, and how an RDG compute pass is declared
  (`FGlobalShader` / `SHADER_PARAMETER_STRUCT` / `FComputeShaderUtils::AddPass`).
- **Where in the capture chain the compute runs:** the M2 SceneViewExtension
  already has the downsampled scene color on the GPU as an RDG resource. Confirm
  the compute pass can consume that resource in the same graph, so the image is
  never copied to the CPU. Record the chosen insertion point.
- **How Slate draws a GPU texture** without a CPU round-trip (the parade already
  draws from a texture on the CPU path — determine whether that texture can be
  the compute output written on the GPU, so drawing needs no readback).
- Record findings in a short note (commit or appended here) **before** writing
  the pass.

### Task 0 findings (recorded 2026-07-12, verified in the installed 5.8 source)

All spec assumptions hold; no decision-level surprise. Implementation follows
these verified engine patterns:

- **In-engine GPU binning precedent:** the eye-adaptation histogram
  (`FHistogramAtomicCS`, `Renderer/Private/PostProcess/PostProcessHistogram.cpp`,
  `Shaders/Private/Histogram.usf` `MainAtomicCS`) — `InterlockedAdd` scatter,
  `AddClearUAVPass` to zero the bins, `FComputeShaderUtils::AddPass` dispatch.
  `RWBuffer<uint>` UAVs are engine-wide practice
  (`SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, …)`, e.g.
  `DistanceFieldAmbientOcclusion.h`). The engine's wave-intrinsic fast path in
  `Histogram.usf` is deliberately **not** copied (parity rule); at our scale
  (960×540 at ~2.5 Hz) the simplest fully portable form — one thread per pixel,
  three global `InterlockedAdd`s — is sufficient.
- **Plugin shader setup:** OCIO pattern (`OpenColorIOModule.cpp`):
  `AddShaderSourceDirectoryMapping(TEXT("/Plugin/TrueScope"), <plugin>/Shaders)`
  directly in `StartupModule()`; our module already loads at `PostConfigInit`.
  Shader classes via `DECLARE_GLOBAL_SHADER` / `IMPLEMENT_GLOBAL_SHADER` with
  the virtual `.usf` path. Needs the `Projects` module for `IPluginManager`.
- **Insertion point:** the same `FRDGBuilder` inside
  `AfterTonemapPass_RenderThread`, immediately after the downscale draw,
  consuming the registered external capture RT texture — the image never
  leaves the GPU, and the existing throttle/viewport selection
  (`IsActiveThisFrame_Internal`) applies to the GPU path unchanged.
- **Slate draws the compute output with no round-trip:**
  `UTextureRenderTarget2D` with `bCanCreateUAV = true` (adds
  `ETextureCreateFlags::UAV`, `TextureRenderTarget2D.cpp`; exactly the Niagara
  render-target data-interface pattern,
  `NiagaraDataInterfaceRenderTarget2D.cpp`) → registered in RDG via
  `RegisterExternalTexture` → compute writes its UAV → the widget's
  `FSlateBrush::SetResourceObject(RT)` draws it (Slate wraps any `UTexture`
  via `FSlateUTextureResource`, `SlateRHIResourceManager.cpp`). Format note:
  `RTF_RGBA8` maps to `PF_B8G8R8A8`, whose typed-UAV store is not guaranteed
  on all RHIs — the parade output RTs therefore use
  `InitCustomFormat(…, PF_R8G8B8A8, /*bForceLinearGamma*/ true)` (RGBA8 unorm
  UAV store is universally supported; linear gamma matches the CPU path's
  `SRGB = false` transient textures drawn with `NoGamma`).
- **Bin parity:** the capture is 8-bit unorm; `Texture2D.Load` returns exactly
  `byte / 255`, and `uint(v * 255 + 0.5)` recovers the byte, so GPU bins are
  expected to match the CPU's byte-indexed bins **exactly**; the spec's ±1
  boundary tolerance stays as a safety net only.
- **Test-only readback:** `FRHIGPUBufferReadback` +
  `AddEnqueueCopyPass(GraphBuilder, Readback, Buffer, NumBytes)`
  (`RenderGraphUtils.h`); synthetic input uploaded with
  `RHICmdList.UpdateTexture2D` into a `PF_B8G8R8A8` texture.
- **Tab-gating signal (Task 5):** Slate executes widget active timers inside
  `SWidget::Paint` (`SWidget.cpp`), pending timers keep Slate awake
  (`FSlateApplication::UpdateAnyActiveTimersArePending`), and a dock stack
  paints only its foreground tab's content — so "the TrueScope panel painted
  within the last second" is a truthful visible-and-drawn signal: a
  hidden-behind-tabs or closed panel stops painting, and re-foregrounding
  paints immediately, resuming capture within one throttle interval.
- **Consequence of no-readback while GPU path is active (intermediate, until
  M5.x):** with the readback off, the CPU analysis never runs, so the other
  three scopes receive no fresh data (waveform/vectorscope hold their last
  trace, the histogram shows only its axis; verified against the widgets'
  existing empty-result guards — no crash path). Acceptable for an opt-in
  diagnostic path; M5.x removes the gap by moving all scopes to the GPU path.

## Tasks

1. **Compute binning pass (Parade):** an RDG compute pass consuming the capture
   RDG texture, writing per-column × luma-bin counts per channel into a
   `RWBuffer<uint>` via `InterlockedAdd`. Same bin layout the CPU parade
   produces, so results are comparable.
2. **Bins → drawable, on the GPU:** a second pass maps counts to the intensity
   texture the parade draws, applying the **count → texel brightness = log**
   density mapping (NOT curve-height/linear — see the "Two distinct count
   mappings" row; the M4 histogram bug came from confusing these). Output is a
   GPU texture/vertex resource Slate can draw directly.
3. **Producer swap behind the existing interface:** the GPU path publishes into
   the same `FTrueScopeAnalysisResult` abstraction (extended to carry a
   GPU-resident result) so the parade widget draws without knowing which
   producer ran. The UI must not branch on path — this is the whole point of the
   M3 analysis/UI decoupling.
4. **Path selection:** wire `GPU (performance)` / `CPU (compatibility)` for the
   parade, plus the diagnostic CVar (`TrueScope.UseCPUAnalysis` or similar,
   verify naming). Default this phase: **CPU stays default** (GPU proven per-
   scope but not yet all-scopes / all-platforms). The GPU path is selectable and
   exercised, not yet the shipped default (that flips when M5.x completes the
   scopes and M7 verifies platforms).
5. **Tab-closed capture gating (folded in per the benchmark finding):** gate
   capture on tab visibility so a closed/hidden TrueScope tab stops paying the
   capture cost. "Docked but hidden behind another tab" counts as closed. This
   touches the capture chain, which this phase reworks anyway. Keep it a
   separate, clearly-scoped change with its own test.
6. License headers; naming; commit per the git rules.

## Equivalence test (GPU vs. CPU ground truth)

The core acceptance mechanism. On the **same synthetic images** the CPU path is
tested against (solid red, gradients, SMPTE bars, flat mid-gray), run **both**
paths and compare **bin arrays**:

- **Counts must match exactly, except ±1 count at bin edges.** Both paths bin the
  same pixels; the only legitimate divergence is a pixel sitting exactly on a bin
  boundary that GPU float vs. CPU float rounds into the neighbouring bin. Assert
  per-bin counts equal, tolerating at most ±1 count per bin, and only where an
  adjacent bin compensates (a true off-by-one at a boundary moves one count from
  bin N to bin N±1; the pair still sums correctly). A whole-scope mismatch, a
  shifted distribution, a dropped channel, or a non-compensating difference must
  fail. **This is a strict count comparison, deliberately NOT a perceptual metric
  like ΔE** — bins are pixel *counts*, not colours, so colour-difference measures
  do not apply here. (Perceptual image comparison of the *drawn* scopes is a
  separate, deferred idea — see `backlog.md`, "Perceptual scope comparison
  (ΔE2000)".)
- This test **may read the GPU bins back** — it runs headless in the test
  harness, not on the runtime path. (Runtime stays readback-free; the test is
  allowed the round-trip to compare.)
- **Prove teeth:** deliberately perturb the GPU binning (off-by-one bin, wrong
  channel) and confirm the test fails, then revert.
- Note: GPU-dependent tests need a real RHI — drop `-nullrhi` for these (see host
  `CLAUDE.md`). The CPU-path tests stay headless with `-nullrhi`.

## Acceptance criteria

Verification record 2026-07-12 (self-verified except where marked human):

- [x] Builds via the Build.sh command (host `CLAUDE.md`) with no new warnings.
- [x] The Parade renders live from the **GPU path** (selectable via the
      path option), visually indistinguishable from the CPU parade — MCP
      screenshot, side by side, human review. *(Self-verified live on Metal:
      parade updates from the GPU targets while the readback is off, follows a
      camera move while the CPU-fed scopes hold; screenshot comparison of the
      parade region CPU vs. GPU sits exactly on the frame-to-frame noise
      baseline — mean abs diff 1.15/255 vs. 1.12/255 for two CPU frames 2 s
      apart. Path switching exercised live via the `TrueScope.AnalysisPath`
      CVar; the right-click UI menu is implemented but could not be exercised
      by UI automation in this session — Slate popup menus did not open via
      MCP clicks at all, including the M4 histogram menu — so the menu is part
      of the human sign-off below.)*
- [x] **No runtime readback on the GPU path:** verified in code and by the
      absence of any `FRHIGPUTextureReadback` on the GPU runtime path; the
      ≈2×-frame-time `Lock` block does not occur when the GPU path is active
      (spot-check: with the GPU path active the per-cycle readback log stops
      entirely — zero analysis cycles while the parade keeps updating).
- [x] **Equivalence test passes** — GPU and CPU parade bins match within the
      defined tolerance on all synthetic images (plus seeded noise at capture
      resolution); teeth proven by two deliberate shader perturbations —
      off-by-one bin shift and G/B channel swap — each failing the test with a
      precise per-bin diagnosis, then reverted to green.
- [x] `uint`-atomics only; no wave intrinsics; nothing platform-specific — code
      review confirms portability (parity rule).
- [x] CPU path unchanged and still default; both paths selectable; the CPU-path
      automation suite still green (`-nullrhi`), count checked: 22/22 (19
      pre-M5 + 3 new: TabVisibilityGate, EquivalenceToleranceRule,
      ParadeBinEquivalence — the last self-skips under NullRHI and runs green
      with the real RHI).
- [x] **Tab-closed gating:** with the tab closed/hidden, capture stops (no
      analysis cycles in the log; the render-thread block disappears) — verified
      live for both the hidden-behind-another-tab case (cycles stop within one
      second, resume immediately on re-foregrounding) and the fully-closed case
      (cycles stop; reopening is covered by the first-paint rule and the
      regression test `TrueScope.Capture.TabVisibilityGate`).
- [x] **Human visual sign-off:** the human compares GPU vs. CPU parade on a real
      viewport and confirms they are "the same enough" for artists (the second,
      human half of the two-part acceptance; the equivalence test is the first).
      *Confirmed by the human 2026-07-12 (including the parade's right-click
      Analysis Path menu, which UI automation could not exercise). M5 accepted.*
- [x] No new TrueScope warnings/errors in the Output Log. *(The two
      startup-time `LogAutomationTest: Error: Condition failed` lines predate
      M5 — present in every editor log since at least 2026-07-11 — and are not
      TrueScope-attributed.)*

## Explicitly out of scope (do not build)

Waveform / Histogram / Vectorscope on the GPU path (deferred to M5.x, written
after this phase); making the GPU path the shipped default (flips at M7 after
platform verification); the M6 status line, settings UI, selectable measurement
rate; density-curve finish-tuning (M6); double-buffering; the adaptive
measurement rate (backlog). Ideas → `docs/backlog.md`.

## Notes for the human (review these before Claude Code starts)

- **Two-part acceptance:** automated bin-equivalence (objective, catches a
  single wrong bin the eye misses) **plus** your visual sign-off (the artist-
  facing "same enough"). Neither alone is sufficient.
- **One scope only (Parade).** The other three GPU scopes are a follow-up phase.
  This keeps M5 a provable de-risking step rather than a big-bang rewrite.
- **Default stays CPU** through M5 and M5.x; the flip to GPU-default is an M7
  decision gated on all-platform verification. So this phase ships the GPU path
  as an *option*, exercised and validated, not yet the out-of-the-box default.
- **Tab-closed gating rides along** because the capture chain is open on the
  bench here — but it is scoped and tested separately so it can't muddy the
  GPU-path work.
- **Bin tolerance (decided):** strict — exact counts, tolerating only a
  compensating ±1 at bin boundaries (float rounding). Not ΔE: bins are counts,
  not colours. The perceptual side (does the *drawn* parade look the same to an
  artist) is covered by the human visual sign-off now, and by the deferred
  ΔE2000 image-comparison backlog item later — so a future automated perceptual
  check exists without bloating this GPU phase.
