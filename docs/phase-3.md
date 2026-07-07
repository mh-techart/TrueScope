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

- [ ] Builds via the CLAUDE.md Build.sh command with no new warnings.
- [ ] Histogram and Parade render live in the TrueScope tab from the actual
      viewport capture, updating at the capture rate. The M2 raw preview is
      replaced.
- [ ] All four synthetic-image automation tests pass (`Automation RunTests
      TrueScope`).
- [ ] Pixel-exact / value spot-check: on a known test frame, the scopes'
      readings match expectation (use the Digital Color Meter workflow where a
      point comparison applies).
- [ ] Drawing is anti-aliased — no blocky/pixelated bars (visual check via MCP
      screenshot, reviewed by the human).
- [ ] Analysis runs without stalling the editor; no game/render-thread block
      (editor stays responsive during capture).
- [ ] Per-cycle cost (ms) measured and recorded.
- [ ] Task 0 readback approach recorded; no TrueScope warnings/errors in the
      Output Log.

## Explicitly out of scope (do not build yet)

Waveform, Vectorscope (M4); GPU compute path (M5); settings UI, selectable
measurement/refresh rate (M6); interpolated/decoupled draw rate (backlog);
double-buffered readback; final scale/graticule polish; SMPTE-bar tests;
per-scope display modes (mono/RGB-overlay). Ideas → `docs/backlog.md`.
