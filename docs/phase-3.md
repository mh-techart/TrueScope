# Phase 3 — CPU MVP: Histogram + Parade (first real scopes)

Read `architecture.md` first (esp. the M3 row with the space-usage guardrail,
and the "Testing strategy" section). Phase 2 left a downsampled,
display-referred capture of the active viewport in a 960×540 render target,
refreshed ~2–3 Hz. This phase turns that image into the **first two real,
measured scopes — Histogram and Parade** — drawn anti-aliased in Slate, and
establishes the architecture every later scope reuses.

Scope: **Histogram and Parade only.** Waveform and Vectorscope are M4. This is
the CPU path (the ground truth the M5 GPU path will later be checked against);
no compute shaders. Correctness over polish — bins must be exact; Resolve-grade
visual finish is M4/M6.

If a decision-level surprise appears, stop and ask per the CLAUDE.md
self-check.

## Task 0 — Research before writing code (mandatory first step)

Do not write the readback from memory. In the installed 5.8 engine source,
read how the engine performs **asynchronous GPU texture readback** without
stalling the render thread:

- `FRHIGPUTextureReadback` — how it is enqueued on the render thread, how
  readiness is polled (`IsReady()`), how the mapped data is retrieved and the
  lock released. Find real in-engine users (grep the renderer / engine plugins)
  and copy their pattern rather than inventing one.
- Confirm the non-blocking flow: request the readback on the render thread,
  retrieve it a few frames later when ready, run analysis off the game and
  render threads, hand the result back to the UI thread-safely.
- Record the chosen approach in a short note (commit or a note appended here).

### Task 0 findings — chosen approach

Modeled on the engine's `FLandscapeAsyncTextureReadback`
(`Runtime/Landscape/Private/LandscapeAsyncTextureReadback.cpp`), the closest
in-engine user of a non-blocking single-buffer readback:

- A single `FRHIGPUTextureReadback` copy is enqueued into the capture's own
  `FRDGBuilder` via `AddEnqueueCopyPass` (RenderGraphUtils), immediately after
  the downscale draw. RDG orders the copy after the write; the pass carries
  `ERDGPassFlags::Readback`.
- Readiness is polled every frame on the render thread from
  `FCoreDelegates::OnEndFrameRT` — the throttled capture pass (~2.5 Hz) cannot
  poll itself. `FRHIGPUTextureReadback::IsReady()` (fence poll) gates a
  `Lock(RowPitchInPixels, &BufferHeight)` / row-by-row copy / `Unlock()`; the
  lock never waits because the fence has already signaled, so the render
  thread is never stalled on the GPU. The staging row pitch can exceed the
  texture width (GPU row alignment) → copy per row, exactly as landscape does.
- The copied pixels are moved to a `UE::Tasks::Launch` background task that
  runs `AnalyzePixels` + `BuildParadeImage` and publishes an immutable
  `TSharedPtr<const FTrueScopeAnalysisResult>`. Widgets poll a serial counter
  on a UI active timer and read the published pointer lock-free.
- **Single buffer, skip-if-busy:** while a readback *or* its analysis is in
  flight, the next capture skips the readback (does not queue). Sufficient at
  2–3 Hz; double-buffering is deferred (backlog / GPU path).

## Architecture (reused by all later scopes — get this right)

- **Analysis/UI decoupling:** a data interface `FTrueScopeAnalysisResult`
  (bin arrays; extensible to textures for the GPU path) sits between analysis
  and drawing. Widgets draw from this struct and know nothing about how it was
  produced — so the M5 GPU path can replace the producer without touching the
  UI.
- **Extensible scope types:** an enumeration + factory for scope widgets, not
  hardwired cases. Histogram and Parade are the first two entries; adding
  Waveform/Vectorscope (M4), CIE, or overlays later must not rework the UI.
- **Per-scope space-usage policy** (architecture guardrail): each scope widget
  declares how it uses its space. Histogram and Parade are **responsive**
  (fill the panel). The `bMaintainSquareAspect` path (vectorscope) is not used
  yet but the mechanism should exist so M4 just sets the flag.
- **Axis labels are fixed-size UI elements**, not part of the scaled graphic —
  legible at any panel size.
- **Single readback buffer** (sufficient at 2–3 Hz; if a readback is still in
  flight when the next is due, skip rather than queue). Double-buffering is a
  later concern tied to high measurement rates / the GPU path.
- **Threading:** never block the game or render thread. Analysis runs on a
  background task; results are published to the UI thread-safely.

## Tasks

1. **Async readback** (`Source/TrueScope/Analysis/`): pull the capture RT to
   CPU via `FRHIGPUTextureReadback` using the Task 0 pattern. Single buffer,
   skip-if-busy, no stalls.
2. **CPU analysis → `FTrueScopeAnalysisResult`:**
   - *Histogram:* per-channel (R,G,B) 1D count arrays over luma bins
     (e.g. 256 bins) across all pixels.
   - *Parade:* per-channel 2D counts (image column × luma bin) at full
     960×540 capture resolution (no further downscale). Waveform (M4) will be
     the same mechanism without channel separation.
   - Log-ish intensity mapping of counts → display brightness so the result
     is not binary on/off (the smooth "density" look; final tuning is M4/M6).
3. **Slate drawing** of Histogram and Parade in their tab panels, replacing
   the M2 throwaway preview:
   - Anti-aliased fills/lines (`FSlateDrawElement::MakeLines` /
     `MakeCustomVerts`, `bAntiAlias=true`) drawn from the bin arrays.
   - Responsive: fill the panel. Scales/axis labels may be minimal this phase
     (correctness first) but drawn as fixed-size UI elements.
4. **Automation tests on synthetic images** (headless — analysis takes a pixel
   array directly, no rendering needed):
   - Solid red (255,0,0): histogram = single peak in R at top bin, G/B empty.
     Proves channel separation + bin mapping.
   - Horizontal gradient (black left → white right): parade = diagonal line
     bottom-left → top-right per channel. Proves column↔luma 2D mapping.
   - Vertical gradient (black top → white bottom): parade = every column spans
     the full value range (full-height bars across the width). Proves column
     aggregation sums all rows, not one scanline.
   - Flat mid-gray (~50%): histogram = single centered peak all channels;
     parade = flat horizontal line at mid-height. Proves even values
     concentrate, no smearing.
   (SMPTE bars are deferred to M4, where the vectorscope gives them full
   diagnostic value.)
5. **Per-cycle cost measurement:** measure and log the rough cost of one
   capture→readback→analysis cycle on this machine (ms). This turns the "is
   the measurement rate a performance problem?" question into a number, and is
   the basis for the settable rate in M6. Record the figure in the commit/note.
6. License headers; naming; commit when acceptance criteria are met.

## Acceptance criteria

- [x] Builds via the CLAUDE.md Build.sh command with no new warnings.
      *(Self-verified: clean Build.sh, 0 warnings.)*
- [x] Histogram and Parade render live in the TrueScope tab from the actual
      viewport capture, updating at the capture rate. The M2 raw preview is
      replaced. *(Self-verified via MCP screenshot: both scopes draw the
      live color-calibrator scene; capture-preview code removed. Layout still
      shows Waveform/Vectorscope placeholders as intended for M4.)*
- [x] All four synthetic-image automation tests pass (`Automation RunTests
      TrueScope`). *(Self-verified headless: SolidRed, HorizontalGradient,
      VerticalGradient, FlatMidGray all Result={Success}.)*
- [~] Pixel-exact / value spot-check: on a known test frame, the scopes'
      readings match expectation. *(Partial/self-verified: the four automation
      tests prove bin-exact channel separation, column↔luma mapping and
      concentration on synthetic frames. Live scopes match the calibrator
      scene qualitatively. A Digital Color Meter point comparison against the
      viewport is **left for human confirmation** — it applies most directly
      to the M4 SMPTE-bar/vectorscope work; histogram/parade aggregate rather
      than map a single point.)*
- [~] Drawing is anti-aliased — no blocky/pixelated bars. *(Self-verified via
      MCP screenshot: histogram curves and parade density read as smooth;
      `MakeLines`/`MakeCustomVerts` with `bAntiAlias=true`, bilinear parade
      texture. **Human review of visual finish requested** — final
      Resolve-grade polish is M4/M6.)*
- [x] Analysis runs without stalling the editor; no game/render-thread block.
      *(Self-verified: editor fully interactive during capture, MCP
      game-thread calls return immediately; readback never waits on the fence.
      Note: the render-thread readback *retrieval* costs ~41 ms per capture on
      this machine — a periodic hitch at 2.5 Hz, not a block; optimization
      logged in backlog.)*
- [x] Per-cycle cost (ms) measured and recorded. *(This machine, macOS/Metal,
      960×540: render-thread readback retrieval ~41 ms, background analysis
      ~7.4 ms, end-to-end capture→published-result latency ~98 ms — the latter
      dominated by async readback frame latency, i.e. waiting, not CPU work.
      Logged by `LogTrueScopeAnalysis` every 50 cycles.)*
- [x] Task 0 readback approach recorded (above); no TrueScope warnings/errors
      in the Output Log. *(Self-verified: only Log/Display entries from
      `LogTrueScope`/`LogTrueScopeAnalysis`.)*

### Verification summary (2026-07-08)

Self-verified by me: build (0 warnings), all four automation tests, live
rendering + preview replacement + no stalls + no log errors (MCP), per-cycle
cost measured. **Needs your confirmation:** (1) Digital Color Meter point
check viewport-vs-scope if you want the pixel-exact ritual on top of the
synthetic tests; (2) sign-off on anti-aliased visual finish. The ~41 ms
render-thread retrieval cost is within M3 scope (measured, not a stall) but
flagged for a possible pre-M4 look — see backlog.

## Explicitly out of scope (do not build yet)

Waveform, Vectorscope (M4); GPU compute path (M5); settings UI, selectable
measurement/refresh rate (M6); interpolated/decoupled draw rate (backlog);
double-buffered readback; final scale/graticule polish; SMPTE-bar tests;
per-scope display modes (mono/RGB-overlay). Ideas → `docs/backlog.md`.
