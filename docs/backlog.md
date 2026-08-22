# TrueScope — Backlog

Ideas and deferred features. Nothing here may be built while a phase is in
progress (CLAUDE.md rule 4) — new ideas get appended here instead. Items
marked **[planned]** are committed future milestones; the rest are options.

## [planned — M8] Viewport overlays: Zebra, clipping warning, False Color

Exposure aids drawn *into* the viewport image (complement to the scopes
window, which analyzes *next to* the image). Implemented as a **writing pass
on the same SceneViewExtension** built in M2, using the plugin shader
infrastructure from M5. No PostProcessVolume, no material asset, zero level
footprint — solves the known drawbacks of the volume-based prototype
(per-level setup, level file changes, active in game builds, affects teams
without the plugin).

- **Zebra:** procedural diagonal stripes (shader-side sine/modulo over screen
  coordinates, optional animation) over pixels above a threshold. Presets:
  100 % / 100 %+ for highlight clipping; 70–80 % band mode for skin tones.
- **Clipping warning:** Photoshop-style solid red for clipped highlights,
  blue for crushed shadows; separate high/low thresholds.
- **False Color:** whole image mapped to an IRE-style color ramp with a small
  legend overlay.
- Thresholds are **display-referred %** — by construction consistent with the
  waveform (waveform shows 80 % ⇔ zebra at 80 % marks exactly those pixels).
- Per-viewport enable/disable via `IsActiveThisFrame`/viewport filtering;
  toggles and threshold sliders live in the TrueScope tab.
- Depends on: M2 (view extension), M5 (plugin shaders). Estimated effort
  after M5: a few days for all three modes including UI.
- Log-profile zebra levels (e.g. S-Log targets) belong to the
  scene-referred/OCIO item below, not to the first overlay release.

## [resolved in M5] Capture runs at full cost while the tab is closed

**Resolved 2026-07-12 (M5 Task 5):** capture is now gated on the TrueScope
panel actually painting (`FTrueScopeCaptureGate`, reported from
`STrueScopeTab::OnPaint` — named `STrueScopePanel` until M6.4 renamed the tab
root and gave the name to the per-panel widget — checked in
`IsActiveThisFrame_Internal`). Paint is
the truthful "visible and drawn" signal: a closed tab, a tab hidden behind
another docked tab and a minimized floating window all stop painting, so
capture (and with it readback/analysis on either path) stops entirely;
re-showing the tab resumes within one throttle interval. Regression test:
`TrueScope.Capture.TabVisibilityGate`. Original analysis kept below for the
record.

### Original record (2026-07-11)

**Found 2026-07-11** while establishing the idle baseline for the controlled
benchmark. `FTrueScopeViewExtension::IsActiveThisFrame_Internal`
(`Capture/TrueScopeViewExtension.cpp`) gates capture on **two things only**: the
focused level-editor viewport, and the ~0.4 s capture throttle. There is **no
tab-state check and no kill-switch CVar**, and the view extension is registered
unconditionally at `OnPostEngineInit` (`TrueScopeModule.cpp`) — independent of
whether the TrueScope tab exists. So with the tab closed, TrueScope keeps
capturing, keeps reading back, and keeps analyzing at full price: a **1–2×
frame-time render-thread block every capture interval, for a result nobody can
see**. (This is also why the benchmark's run-1 baseline had to be taken with
`-DisablePlugins=TrueScope`: the tab-closed baseline the methodology asks for is
not reachable from the UI.)

This is a **real product gap**, not a benchmark inconvenience — a user who closes
the tab reasonably expects the cost to go away, and an installed-but-unused
plugin silently taxing every editor session is exactly the reputation an editor
plugin cannot afford.

- *Fix candidate:* gate capture on tab visibility — the view extension asks
  whether a live TrueScope panel is actually presenting results (widget/tab
  existence, not just window focus), and returns false otherwise. Cheap in
  principle; the subtlety is defining "visible" correctly (tab docked but hidden
  behind another tab still counts as closed; a floating tab does not).
- *Placement:* **decide in M5**, which reworks the capture/readback chain anyway
  (GPU path, no runtime readback) — building the gate now would only be redone
  there. Flag it explicitly in **phase-5 planning** so it is decided, not
  forgotten: even with the GPU path there is no reason to run analysis for a
  closed tab.

## [resolved in phase-5.2] While the GPU path is selected, the other three scopes show no fresh data

**Resolved 2026-07-13 (phase-5.2):** all four scopes now run on the GPU path,
so no scope is left on a stopped producer; additionally the unified
empty-state (`DrawScopeHints` / `HasDrawableData` on the base scope widget,
reusing `DrawCenteredHint`) makes any scope without data for the current
result say so identically instead of holding a stale trace. Regression test:
`TrueScope.UI.EmptyState`. Original record kept below.

With the M5 GPU path active (opt-in; CPU stays the default), the readback is
off by design, so the CPU analysis never runs: the waveform and vectorscope
hold their last CPU-produced trace, the histogram shows only its axis. No
crash path (verified against the widgets' empty-result guards), but a user who
flips the parade to GPU gets silently stale neighbours — acceptable only while
the path is a validation option. M5.x (all scopes on the GPU path) removes the
gap; if M5.x ships display modes per scope instead of all four at once, add a
per-scope "no data on this path" hint via the existing DrawCenteredHint
mechanism rather than leaving stale traces.

## Deferred features

- **HDR / wide-gamut colorbar verification (Rec.2020 / BT.2111)** — DaVinci can
  generate EBU, SMPTE, and BT.2111 HLG/PQ (Narrow/Full) colorbars. When TrueScope
  gains Rec.2020 / HDR support, verify the vectorscope/parade/waveform against
  these generated bars, deriving the Rec.2020 matrix and target radii the same way
  Rec.709 was done (documented in "Color standard reference"). Not now — current
  scopes measure Rec.709; HDR bars would only confuse verification until the
  color-space handling exists.
- **Vectorscope 2× zoom orientation aids** — at 2× zoom TrueScope currently drops
  the target markers and outer ring (technically correct — they no longer apply at
  the zoomed scale), which loses visual orientation. Resolve instead keeps the
  markers and ring fixed and lets the plotted values grow past them, expecting the
  user to understand the markers don't apply while zoomed. Design a better
  orientation treatment for the zoomed state (e.g. keep a faint ring/markers for
  reference, or a scaled graticule) rather than removing them entirely. Appearance
  only. Reference: the human's 2× zoom comparison screenshots (Resolve vs.
  TrueScope).
- **Cine Camera source via SceneCaptureComponent2D** — measure a selected
  camera *without* piloting it. Original MVP path, superseded by the
  viewport-only decision. Opt-in convenience only; must never become a
  requirement (would reintroduce scene-side state). Caveats to solve:
  mirroring camera/post settings, auto-exposure parity with a real viewport.
- **Pane split-ratio persistence** — the TrueScope panel's internal `SSplitter`
  ratios reset to equal on tab close/reopen; the docking position persists but
  the pane sizes do not (code default 0.5, confirmed during the phase-5.1 MCP
  sessions). Persist the user's pane sizes through the same per-user settings
  mechanism the other options use (phase-6.1 Task 3). Low priority (human's
  call, work order 13).
- **Unsuitable view-mode signalling (open design question, work order 13)** —
  even in a perspective/cine view, the viewport's **view mode** may carry no
  meaningful colour signal. Confirmed fine and worth keeping measuring: Lit,
  Detail Lighting, Lighting Only, Reflections. Open for a later phase: which
  view modes yield no usable colour image, and should those get a status-line
  note like the ortho case? Deliberately **not** a blanket "warn on non-Lit" —
  Lighting Only etc. are wanted. **The Fix 1 investigation already answered
  half of this** (see `architecture.md`, "Capture exists only while…"): modes
  that `ApplyViewMode` maps to `PostProcessing=0` — Wireframe, Unlit, most
  debug visualizations — produce **no capture at all** (the scopes silently
  hold their last frame), so the engine's own `ApplyViewMode` table in
  `ShowFlags.cpp` is the authoritative list to design against. Remaining
  design work: per-mode decision (note vs. measure) for modes that *do*
  capture but are questionable as a signal, and a status-line state for the
  no-capture modes.
- **[resolved in work order 13] Name the active OCIO view transform in the
  status line** — **Resolved 2026-07-17 via a human-decided hard OCIO plugin
  dependency** (`FOpenColorIODisplayManager`, live per-viewport config; the
  full decision history — including why the reflection soft path was built
  first and then rejected (teardown-only persistence made it lag live
  toggles), the exact API wall, and the OCIO plugin's per-layout-pane preset
  behaviour — is recorded in `architecture.md`, "Status line: OCIO
  view-transform name via a hard OpenColorIO plugin dependency"). What
  remains for the **color-space-aware work below**: making the transform
  *load-bearing* (scale and graticule calibration) — the dependency now
  already exists, so that work can query OCIO directly. V1's percentages and
  vectorscope targets stay calibrated for Rec.709/sRGB only, so under a P3/PQ
  view transform the name in the status line is a truthful label, not a
  recalibration.
- **Scene-referred / HDR / OCIO measurement modes** — analyze pre-tonemap or
  OCIO-managed signals; log-scale waveform; values above display white;
  log-profile (S-Log etc.) zebra targets; color-space-aware calibration
  (vectorscope graticule and % interpretation for non-Rec.709 view
  transforms such as P3 or PQ). V1 is deliberately display-referred
  sRGB/Rec.709 only (see architecture.md).
- **Toolbar button** to open the TrueScope tab (V1: Window menu only).
- **"Reset to Defaults" for the settings popup** — M6.4 gave every panel menu a
  reset. The global options (analysis path, capture rate) have none. Low value
  while there are only two of them; revisit if the popup grows.
- **Plugin icon + tab icon (assets available 2026-07-20; tab/window part resolved
  in M6.4, 2026-08-21)** — the human provided a **logo (PNG)** and an **icon
  (SVG)**. `Resources/Icon128.png` (Plugin Browser icon, which cleared the
  missing-Slate-resource log line) was already in place; M6.4 Task 1 added the
  plugin's Slate style set and registered
  `Resources/Icons/TrueScope_Icon_mono.svg` as the 16×16 brush
  `TrueScope.TabIcon`, shown in the Window-menu entry and on the tab itself.
  **Still open:** the main-toolbar button (separate item above), which would reuse
  the same brush.
- **Higher refresh rates / per-frame mode** — trivial once the GPU path (M5)
  exists; expose as a setting with a resource-use warning.
- **Smooth scope drawing via interpolation (decoupled draw rate)** — the
  measurement rate (capture + readback + analysis, the expensive part) and the
  draw rate (rasterizing the scope from existing bin data, nearly free) are
  independent. Measure at a low rate (~2–3 Hz) but *draw* at display rate
  (~60 fps), interpolating each scope smoothly from the previous result to the
  current one. Gives visually fluid scopes without raising measurement load —
  the right fix for "3 Hz looks choppy", instead of measuring more often.
  Deliberately deferred from M3: M3 must prove bin-exact correctness on a
  *static* result (an interpolating scope shows in-between values that are
  harder to verify). Depends on M3/M4 (a correct static scope first). Once
  the per-cycle cost measured in M3 is known, this plus a settable
  measurement rate (M6) covers both performance and smoothness.
- **Adaptive measurement rate (CPU compatibility path)** — since the readback
  `Lock` costs **1–2× host frame-time** (measured ≈2× median, 2026-07-11), its
  editor impact is self-scaling if the measurement rate adapts to frame-time:
  throttle so the readback never consumes more than a set fraction of editor time
  (e.g. "at most two lost frames per second"). On a slow machine this yields
  ~1 Hz automatically; on a workstation ~2.5 Hz or more — one rule, correct
  everywhere. Only relevant to the shipped CPU path (the GPU path has no
  readback). Ties into the settable rate (M6) and answers the M7 "is the CPU
  fallback tolerable?" question. Budget in frame-times, not milliseconds, and
  budget **two** per capture, not one.
- **Buffer readback instead of texture readback (unverified)** — *hypothesis, not
  a plan.* The Metal GPU-drain lives in `RHIMapStagingSurface` (texture path).
  Whether `FRHIGPUBufferReadback` + `RHILockStagingBuffer` avoids the
  `SubmitAndBlockUntilGPUIdle` on Metal is **unknown** and must be read in 5.8
  source before any work — do not assume. Only relevant to the CPU compatibility
  path (the GPU path reads nothing back). Low priority; the GPU-resident M5 design
  makes it moot for normal use.
- **Cheaper readback retrieval (render-thread cost)** — M3 measured the
  render-thread step that maps the staging texture and copies it to CPU
  (`Lock` + row-by-row copy + `Unlock` in `FTrueScopeAnalyzer::
  PollReadback_RenderThread`). The readback step costs **1–2× host frame-time
  per capture** — measured ≈2× median in the controlled benchmark (2026-07-11;
  earlier millisecond figures were taken under uncontrolled conditions and have
  been discarded; see `architecture.md`, "Measured: controlled readback
  benchmark"). The cost is the
  Metal GPU-drain inside `Lock`, not the copy — see `phase-3.md`,
  "Correction"). It runs on throttled capture frames (~2.5 Hz), so the editor
  stays interactive — the same benchmark measured **no felt FPS impact** — but it
  is a periodic render-thread block of one to two frame-times. Options to
  explore: `Lock` on the render thread but hand the mapped pointer + pitch to
  the background task for the slow copy (then `Unlock`); investigate whether
  the Metal `Lock` itself (not the memcpy) dominates and whether a
  persistently-mapped buffer readback avoids it; or reduce the capture
  resolution. Tie-in with the settable measurement rate (M6) and the GPU path
  (M5, which removes the CPU copy entirely for the GPU analysis result).
  **Status after the M5 path decision:** with the GPU path as the internal
  default from M5 on, the readback block no longer affects normal use, so this is
  **not a release blocker**. It does still affect the shipped CPU compatibility
  path. **M7 acceptance question:** is the CPU path, as shipped, tolerable for a
  user who has to fall back to it? If not, do the optimization there, or lower
  the capture resolution for the CPU path only. Independently, the
  instrumentation experiment that preceded `phase-5.md` answered a question that
  **decided M5's architecture**: the cost **is** the GPU-drain in the map/lock
  call (size-independent), not the memcpy — confirmed in engine source — so the
  GPU path must contain **no readback at all** (results stay GPU-resident),
  because even a few kilobytes would cost the same.
  **Update (readback experiment):** both the "lower capture resolution" and the
  "Lock-on-render-thread, copy-in-background" options **do not help** — the cost is
  the GPU-drain inside `Lock` (1–2× frame-time, size-independent), not the memcpy
  (negligible). Retain them here only as recorded dead ends. The live viability
  question is now the *adaptive measurement rate* item and the *buffer-readback*
  hypothesis above.
- **Readback state-machine recovery net (robustness; tie to M5)** — the
  single-buffer skip-if-busy readback has no timeout: a readback whose fence
  never signals would leave `bReadbackInFlight` stuck and freeze capture. The
  PIE investigation proved this does **not** happen for PIE (nothing hangs; the
  freeze is the selection logic holding the last frame, not a stuck readback),
  so it is not a current bug — but a genuine viewport/world teardown could in
  principle trip it. Add a recovery path (abandon-and-reset after N capture
  intervals, N in intervals so it scales with the settable rate) **when M5
  reworks the readback machine anyway**, rather than as speculative work now.
  Include a regression test that drives an in-flight readback whose fence never
  signals and asserts the reset restores capture.
- **[resolved in phase-6.1] Render scope textures at display resolution** —
  **Resolved 2026-07-14 (phase-6.1 Task 1).** Every scope widget now publishes
  the rect it draws its raster into, in device pixels
  (`FTrueScopeRasterSizes`, filled from `OnPaint`), and the GPU producer sizes
  its render targets to those rects: one raster texel is one device pixel, so
  Slate's upscale becomes an identity. Targets are *allocated* in 64-px buckets
  (a splitter drag must not reallocate them every mouse move) and the pass
  rasterizes the exact requested region inside the bucket, which the widget then
  samples through the brush's UV region. Bins and counts are untouched — the
  shaders resample the bin grid with the same clamp-bilinear filter Slate's
  sampler used to apply. Regression test: `TrueScope.UI.RasterBucket`.

  *Re-measured after the fix* (same method as below, retina, red curve,
  perpendicular stroke width in device px): GPU **1.43 px** on flat stretches and
  **1.02 px** on steep ones, against the CPU/Slate line's 1.28 / 0.97 px — the
  slope-dependence is gone (the GPU stroke now thins on steep stretches exactly
  as the Slate line does) and the width matches within ~0.15 px. Why it works:
  Slate expresses line thickness in *screen-space* pixels (`FLineBuilder` divides
  it by the element scale), so a device-resolution raster reproduces the CPU
  line's width from the same constant, by construction.

  *Original record (measured 2026-07-14, before the fix):* the raster was
  magnified 3.67× horizontally but only 1.64× vertically, and the shader measures
  the stroke distance isotropically in *texel* space — so the GPU stroke was not
  just wider than the Slate vector line but **slope-dependent**: 1.98 px on flat
  stretches and 4.00 px on steep peaks, against the CPU line's uniform
  1.13/1.32 px. No thickness constant could fix that (a texture with fewer texels
  than the screen has pixels cannot carry a crisp 1-px line, and no constant
  removes the anisotropy) — hence the resolution fix. The stroke's *coverage
  profile* was a separate, genuine bug, fixed earlier (commit "Match Slate's
  antialiased line coverage in the histogram GPU raster").
- **[resolved into M6.3] Density-to-brightness curve ("scopes drowning in dense
  areas")** — the M3 observation. **Cause found by measurement (2026-07-20),
  opposite to the original guess:** TrueScope's transfer is **log**
  (`ln(1+16·C)/ln(1+16·C_max)`), which *over-lifts* sparse cells (one pixel in a
  540-row column → 31 % brightness) into an undifferentiated bright mass; Resolve's
  transfer is **linear** (measured). The fix is to go **linear × per-scope gain,
  hard clip** — not a stronger curve. Compositing was ruled out (D2: TrueScope
  already desaturates on overlap like Resolve). Decided and specified for **M6.3**
  — see `architecture.md`, "Density transfer & Trace Brightness".
- **Export the capture RT for true 1:1 Resolve comparison (M6 density-calibration aid)** —
  TrueScope analyzes a fixed **960×540** bilinear downscale of the viewport
  ViewRect (verified 2026-07-20; see architecture.md scope cross-check). A
  `HighResShot 960x540` gets Resolve close — matching pixel count, hence
  matching density normalization — but it is a *native* 960×540 render, not the
  *downscaled* image TrueScope actually measured, so it is not literally
  pixel-identical. For the finest density-curve calibration, add a
  dev/diagnostic command that dumps
  `FTrueScopeViewExtension::GetCaptureRenderTarget()` to a PNG: that is the exact
  set of pixels the scopes see, giving true one-PNG-pixel = one-measured-pixel.
  Natural companion to the "frozen reference image" cross-check automation below.
- **Downscale-ratio reproducibility caveat (M6 density calibration)** — the
  capture target is always 960×540, but the *source* `SceneColor.ViewRect`
  varies with viewport/window size, so the bilinear downscale *ratio* varies
  with it. Consequence: the exact 960×540 pixel values (and therefore the fine
  density readings) differ slightly for the *same scene* at different viewport
  sizes — a small but real source of run-to-run variance. When calibrating the
  density-to-brightness curve, **pin the ViewRect/window size** (read it off the
  `Capture pass ran (WxH)` log line, architecture.md) so the reference is
  reproducible; or drive calibration off the dumped capture RT (item above),
  which removes the viewport-size dependency entirely.
- **[resolved into M6.3] Histogram scale gain** — **re-measured 2026-08-17**
  (superseding the 2026-07-20 reading, which had recorded an unexplained
  `≈1.6 % of frame pixels` and an "Exact" detent that does not exist in Resolve):
  Resolve's histogram uses a **fixed** shared scale — not normalised to the frame's
  own peak — that is **linear** in count and **resolution-independent**, clipping a
  bin once it holds more than ≈**1.58 % of all the frame's pixels**. Its gain slider
  is linear in that threshold (3.16 % at the left end → ≈0 at the right end), which
  is why its right end is unusable. The left-end figure is *derived*, not measured:
  what was measured is that Resolve's left end sits at exactly twice its default, so
  the percentage moves whenever the default does (hence 3.34 % before the 2026-08-19
  refinement of the default, 3.16 % after). **Decided for M6.3:** same fixed-scale principle,
  default threshold 1.58 % of all frame pixels, but a **logarithmic** gain slider
  (±3 doublings, centre = default) instead of Resolve's linear-in-threshold one. The
  "Exact" detent is dropped — see the new backlog item "Fit-the-peak histogram view".
  See `architecture.md`, "Histogram vertical scaling" and "Gain slider law".
- **Fit-the-peak histogram view (self-adjusting, never clips)** — the behaviour
  TrueScope shipped in M4–M6.2: the shared scale is derived from the frame's own
  tallest bin, so the highest peak always just touches the band top and nothing is
  ever clipped. M6.3 replaced it with a fixed scale (see `architecture.md`,
  "Histogram vertical scaling") because a scale that moves with the content makes
  two shots incomparable. It remains a legitimate view — it is the only one that
  guarantees nothing is hidden by clipping — but it is **not** what the gain slider
  provides: the slider is a fixed multiplier, so a setting that fits one frame will
  not fit the next, whereas this view re-fits itself per frame. **Priority: low.**
  M6.3's slider reaches a clip threshold of 12.6 % of frame pixels at the left end,
  which already covers all but extreme content, so the practical need is small. If
  it is ever built, it must be a clearly separate mode, never a position on the gain
  slider — the two use different normalisation bases and merging them is the same
  class of confusion that caused the M4 histogram bug.
- **Histogram bin count if capture moves to 10-bit** — TrueScope currently captures
  8-bit (`RTF_RGBA8`) and uses 256 bins, one per code value, which is why the
  clip threshold measured against Resolve transfers unchanged (see `architecture.md`,
  "Histogram vertical scaling"). If capture ever becomes 10-bit — HDR viewport output
  (HDR10 / ST.2084 / HLG) or a 10-bit SDI/NDI path via the AJA or Blackmagic Media
  I/O plugins, both common in virtual production — the same image spreads across four
  times as many distinct values, each holding roughly a quarter of the pixels. At 1024
  bins the same shot would then draw about four times lower than today. **Open and
  unmeasured:** whether Resolve compensates for this — every measurement so far used
  8-bit material only. Re-measure with genuine 10-bit material before switching, and
  treat bin count, capture format, working colour space and scale reference (code
  values vs nits/PQ/HLG) as one connected decision rather than four separate ones.
  Related: "Scale behavior across bit depth / HDR / color spaces".
- **Fill the empty `.uplugin` metadata before release** — `CreatedBy`,
  `CreatedByURL`, `DocsURL`, `MarketplaceURL` and `SupportURL` are empty strings left
  by the plugin wizard, so the Plugin Browser shows a blank author and the links go
  nowhere. For an open-source plugin `CreatedBy` is how a user finds the project.
  Also confirm whether `IsBetaVersion: true` is still wanted at release. Blocking for
  publication, harmless until then.
- **`VersionName` has no stated rule** — it moved 0.1.0 → 0.1.5 deliberately, but
  nothing records what the number tracks (milestone? release?). Decide and document it
  before the first public release, or it will drift.
- **Waveform density basis: fraction of column height vs absolute pixel count**
  — Resolve's density transfer was measured at 1080 rows only (2026-08-19: full white
  at ≈34 pixels per column ≈ 3.2 % of column height). Whether Resolve keys on the
  *fraction of column height* or on an *absolute pixel count* is unproven; the two
  differ by exactly 2× at TrueScope's 540-row capture, i.e. gain 31.25 vs 15.625.
  TrueScope assumes the fraction, consistent with the measured histogram behaviour and
  with the fact that an absolute count would make UHD material blow out relative to HD.
  **Reclassified as a kind 2 value** (internal default; see `dev-rules.md`): rather
  than another Resolve round, expose the density gain in the Tweakables menu and let
  the human settle it by eye in one sitting. The Resolve reading remains possible if
  ever wanted, and is cheap: **`Tools/make_calibration_plates.py`** (stdlib only,
  `python3 make_calibration_plates.py <out-dir>`) regenerates every plate, and
  **`M63_count_wedge_1920x540.png` is the single reading that settles this** — the same
  absolute per-column counts as the 1080 wedge, so only the column height differs.
  Clip near count 17 => fraction of column height (what the 31.25 default assumes);
  clip near count 34 => absolute pixel count (the default would have to be 15.625).
  Read it in a 1920x540 timeline — one timeline per image size, or the upscale
  destroys the known counts. The already-built plates currently sit in the host
  project at `Saved/TrueScope_M63_Task5/`, which is **unversioned and may vanish**;
  the generator in the plugin is the durable copy.
- **Density slider range: symmetric ±3 doublings vs asymmetric** — M6.3 ships the
  uniform logarithmic law with ±3 doublings and 1.0× at centre for all gain sliders
  (`architecture.md`, "Gain slider law"). Open question raised during M6.3: with a
  correctly calibrated default, is the bottom of the range (white only at ~25 % of a
  column) useful or wasted travel? It is plausibly useful — it isolates large uniform
  areas — but this was never checked on screen, because the observation that prompted
  it ("too dim to read below ~55 % of travel") was made while the defaults were still
  at 1.0, i.e. about 31× too dim, so it says nothing about the range. **Do not decide
  from that observation.** An asymmetric span (e.g. one doubling down, three up) would
  collide with "1.0× sits exactly at centre", which is a settled decision and is pinned
  by a test; the options are to bend the law, to move the tick/snap/reset off the
  geometric centre, or to leave it alone. Settle it from the Tweakables menu once that
  exists. **Priority: low** — nothing is broken; the default is correct and the range
  is usable.
- **UI consolidation + "Tweakables" and "Settings" menus (next phase after M6.3)** —
  the tab toolbar has accumulated controls across M6.1–M6.3 (display modes, analysis
  path, measurement rate, two families of gain slider) without anyone reviewing the
  whole. Human assessment: cluttered and redundant. The same phase introduces two menu
  entries that change how appearance values get decided:
  - **Tweakables** — developer-only, not present in shipped builds. Live controls for
    internal values, applying immediately without a restart, so the human can find a
    value in one sitting instead of over chat rounds. First candidates: histogram clip
    threshold, per-scope density gain defaults, gain-slider range endpoints, snapping
    on/off. Include an action that copies the current values as text, so a found value
    is handed over without transcription.
  - **Settings** — end-user facing. First candidates: per-scope Trace Brightness,
    graticule brightness. The human establishes minimum, maximum and default from the
    Tweakables menu, and those become the shipped bounds.
  See `dev-rules.md`, "Classify a value before spending effort on it".
- **CPU vs GPU: small per-cell count differences on a real capture** — the
  integer-exact bin-equivalence tests (`TrueScope.GPU.*`) feed synthetic pixel arrays
  and stay exact, but a real frame compared through the shipping path is not
  bit-identical. Measured 2026-08-19 on the frozen ColorCalibrator still (Realtime
  off, so both paths analysed the *same* capture — the only condition under which
  this comparison is valid): 17.29 % of the panel's pixels differ, mean |Δ| 0.9 of
  255, 64 % of the differing pixels off by 1–3 code values, 0.067 % off by ≥32 with
  a maximum of 46 (≈3 counts at the new gain). Direction is not symmetric: 393
  differing waveform cells were CPU-brighter against 167 GPU-brighter. Sub-pixel
  misalignment was tested and **ruled out** — diffing at all nine dx,dy ∈ {−1,0,1}
  offsets gave 17.29 % / mean 0.901 at (0,0) versus ≥31 % / mean ≥9 at every shift,
  so the images are aligned and the difference is real. The differences pre-date
  M6.3; the new density gain (one count = 14.76 code values, up from 0.47) is what
  made them visible. Root cause unknown — candidates: downscale/sampling differences
  feeding the two paths, or float-vs-integer accumulation order. Find a synthetic
  case that reproduces it and pin it, rather than measuring the panel again.
- **Scale behavior across bit depth / HDR / color spaces** — V1 uses a
  normalized 0–1023 scale for the SDR display-referred case (settled in
  architecture.md). Open for later: what the scale should show for HDR
  material (Nits 0–1000/4000/10000, ST2084/PQ, HLG, or a normalized 10-bit
  code value) and how it interacts with non-Rec.709 Working Color Spaces;
  scale and vectorscope graticule become color-space-aware there. Part of the
  scene-referred/HDR/OCIO item above.
- **Cross-check against Final Cut Pro scopes before public release** — the
  scope cross-check (architecture.md) currently uses DaVinci Resolve; also
  verify against FCP's scopes before a release (FCP not installed on the dev
  machine yet).
- **Cross-check automation (option, undecided)** — the Resolve cross-check is
  currently fully manual. Three levels of possible automation, increasing cost:
  (1) **frozen reference image** — store one deterministic PNG of the reference
  scene (TAA off) in the repo and run future scope checks against it, decoupling
  scope correctness from the renderer (no jitter, no drifting sky, no Lumen
  convergence). Highest value, lowest cost; natural companion to the synthetic
  test images. (2) **numeric ground-truth test** — capture Resolve's peak-bin
  values for that fixed image once (as in M4: 706/674/664 …), assert TrueScope's
  analysis against them; Resolve itself is not scriptable, so ground-truth
  collection stays a one-time manual act. (3) **golden-image pixel diff of the
  drawn scopes** — thorough but fragile (every intentional visual change breaks
  it; AA/platform false positives); not recommended. Suggested if pursued:
  level 1 after M4 or within M6, level 2 alongside M6 calibration, skip level 3.
- **Fab listing (free)** — companion distribution channel to GitHub for
  Blueprint-only users; verify current Fab submission terms at M7 time.
- **Engine version ports (±1 minor)** — after M6; per-version binaries in
  GitHub releases.
- **Scope display modes** — small per-scope display variants, seen in FCP:
  **Monochrome** (white instead of colored traces), Show Guides toggle,
  trace-brightness control **(now specified: a per-scope gain slider, M6.3)**,
  and per-scope modes for the parade and the
  vectorscope. These are display options on a scope, separate from the window
  layout (below). M4 built the mechanism (`FTrueScopeDisplayMode` + the
  right-click menu on `STrueScopeWidget`) and shipped its first two users — the
  histogram's Separate Channels / RGB Overlay pair and the waveform's RGB /
  Luma pair. Further modes only need the three overrides.
- **[resolved in phase-6.1] Minimum panel size / graceful degradation** —
  **Resolved 2026-07-14 (phase-6.1 Task 5).** The cause was what the item
  suspected: each scope had its own hand-rolled "plot too small" guard that drew
  the background and bailed out, so a squeezed scope silently vanished. The rule
  now lives on the base scope widget (`TrueScope::Draw::IsTooSmallToRead`, routed
  through `DrawScopeHints`), so every current and future scope inherits it: below
  the readable size it shows a centred *"Panel too small"*, and below the size
  where even that hint's plate fits, `DrawCenteredHint` draws nothing rather than
  clipped text — the same mechanism the paused/empty hints use. The vectorscope
  reaches the threshold first, as predicted, because its content rect is the
  largest centred square. Regression test: `TrueScope.UI.PanelSizeHint`.
  (The detailed vectorscope graticule spec, previously a placeholder here, is now
  its own section below: "Vectorscope graticule extensions".)
- **Richer PIE handling (deferred; behaviour otherwise resolved).** The former
  "PIE breaks capture until restart" concern was a measurement artifact
  (separate-window PIE captures input; nothing hangs) — see `architecture.md`,
  "Behaviour during PIE". Current behaviour is correct and intended: a PIE
  viewport is never adopted as a source; the scopes hold the last frame only
  when the measured viewport itself enters PIE, and there is deliberately no
  auto-fallback to another viewport. Phase 4.1 adds the visible paused
  indicator. Still deferred, and only if ever wanted: actually *measuring* the
  PIE image itself (would require treating a PIE viewport as a valid source — a
  scene-side concern, low demand since Simulate covers animated content).
- **README: document Standalone Game is not measured** — Standalone Game runs
  as a separate process with no editor module, so TrueScope does not load
  there; state this as a known limitation in the plugin README (M7 release
  docs). See `architecture.md`, "Behaviour during PIE".
- **[partly resolved in phase-6.1] Orthographic viewports are never measured —
  tell the user.** **The UX half is resolved (2026-07-14, phase-6.1 Task 4):** the
  status line now names the state — *"No signal — orthographic view (Top); switch
  the viewport to Perspective"* — instead of leaving blank scopes unexplained
  (`TrueScope::GatherMeasurementStatus`; test `TrueScope.UI.StatusLine`). **The
  source investigation the "(b) actually fix it" branch asked for is now done**
  (work order 13, 2026-07-17, recorded in `architecture.md` "Capture exists only
  while…"): ortho views run with `PostProcessing=0`, and the renderer's minimal
  path **never registers scene-view-extension pass callbacks at all** — so no
  pass choice can capture ortho; measuring it would need a different mechanism
  entirely (e.g. a non-SVE hook), which is a real architecture decision, not a
  fix. Left deliberately open at low priority. Original record follows.

  Found during M4.1: with the level viewport in an ortho view (Top/Front/Left…),
  no capture ever happens — the log shows no "First viewport capture" at all and
  the scopes stay blank, until the viewport is switched back to Perspective, at
  which point capture starts immediately. (Cause verified 2026-07-17, above.)

  **This is not a bug, it is the same class as the M4.1 paused indicator:** the
  source delivers nothing and the user is not told why. The scopes are simply never
  fed, so M4.1's paused hint correctly stays silent (no play session is running).
  What is missing is the *explanation*, and explaining what is (or is not) being
  measured is precisely the job of the **M6 measurement status line** (see
  `architecture.md`, M6: "measurement status line showing what is being measured").
  So consolidate it there rather than building a piecemeal one-off hint now — while
  reusing the base-widget hint mechanism that phase 4.1 introduced
  (`DrawCenteredHint` / `DrawPausedHint`, which the minimum-panel-size hint will
  also reuse). At M6, decide additionally whether ortho is (a) documented as a
  limitation ("scopes measure perspective viewports") or (b) actually fixed by
  hooking a pass that runs in ortho too — the status line is worth having either
  way.
- **Viewport testpattern / colorbar generator** — TrueScope can display
  standard test patterns (SMPTE/ARIB colorbars, ramps, gradients) *in the
  viewport* as a known reference signal. Two independent uses:
  1. **Scope self-validation with known ground truth.** A test pattern has
     mathematically known target positions, so it validates the scopes without
     needing numbers out of Resolve (Resolve exposes no readable numeric
     values — cross-checks against it are by-eye against the graticule, not
     figures). Colorbars *must* land their density in the six vectorscope
     targets and draw straight lines between the R/G/B/Cy/Mg/Yl marks; ramps
     *must* draw the known diagonal on parade/waveform. This is the same
     principle as the existing headless SMPTE-bars test, but live in the
     editor for human cross-check — the ground truth comes from the pattern,
     not from Resolve, which then only has to match the *look*. Reference for
     the expected vectorscope geometry (colorbars → green lines into each
     target): videoclarity.com 4kscope vectorscope sample image.
  2. **Downstream hardware calibration (Virtual Production).** A workstation
     feeding footage over professional video channels (SDI etc.) to external
     devices needs a known signal to calibrate that chain. A built-in
     colorbar generator serves that directly — a real production feature for
     the artist/VP audience, not just a debug aid.
  - Display-only, on the same SceneViewExtension the overlays (M8) use, or as
     a simple emissive fullscreen draw — no level footprint, editor-only, in
     keeping with the zero-footprint constraint. Scope/priority TBD; likely
     companions the M8 overlay work since both are viewport-writing features.
  - Relationship to WYSIWYG: the generator writes into the viewport, so the
     scopes measure it like any other image — no special path needed. Confirm
     it sits before the capture hook so the scopes see it.
- **Perceptual scope comparison (ΔE2000)** — an automated, perception-based
  way to confirm two *rendered* scopes look the same, complementing the strict
  bin-count equivalence test (which compares counts, not appearance). Render
  two scope images (e.g. GPU vs. CPU parade, or TrueScope vs. a stored
  reference), convert both to CIELAB, compute **ΔE2000 per pixel**, and assert
  a percentile bound rather than a mean (a mean hides local outliers). Target
  **ΔE2000 < 1** (below the trained eye's threshold) for, say, ≥99 % of pixels;
  **< 2** as the practical ceiling. Rationale: ΔE is the standard perceptual
  colour-difference measure; sub-1 is "barely perceptible even to a trained
  eye", >2 starts to read as a difference. Caveat to honour: Lab/ΔE is only
  approximately perceptually uniform (blue region is weakest), so use ΔE2000
  (not ΔE76) and a percentile, not a single average. Uses: the GPU-vs-CPU check
  for the M5.x scopes, and a future automated leg of the DaVinci Resolve
  cross-check (pairs naturally with the "frozen reference image" idea and the
  testpattern generator). Small standalone infra (Lab conversion + ΔE2000 +
  image-diff harness); deliberately kept out of M5 to not bloat the GPU phase.
  Not a substitute for the strict bin test — the two answer different questions
  (counts vs. appearance).

## Resolve-parity candidates (from the DaVinci Resolve scope manual, 2026-07-20)

Options Resolve offers that TrueScope could add later; display-only unless noted.
None is M6.3. Most fit the M4 per-scope display-mode mechanism.

- **Waveform Extents** — outline of the true min/max excursions, readable even
  with a low-pass filter on.
- **Waveform Low Pass Filter** — denoise the trace for readability (Resolve pairs
  it with Extents).
- **Reference Levels** — user Low/High horizontal lines on the waveform (e.g.
  broadcast-safe 64/940).
- **Waveform/Parade scale styles** — %, IRE/mV, 12-bit alternatives to the 0–1023
  axis (the axis-label infra already anticipates %/IRE).
- **Graticule brightness control** — a separate graticule-intensity slider
  alongside the Trace-Brightness one.
- **Vectorscope graticule styles** — Resolve's **Hue Vectors** (reference lines
  target→centre, fading in saturated regions) and **Simplified** styles, in
  addition to the hue ring already planned in "Vectorscope graticule extensions".
- **Chroma (CbCr) waveform mode** — Resolve's waveform has a CbCr mode; TrueScope
  has Y and RGB. A per-scope display-mode candidate.
- **Display Qualifier Focus** — hovering the viewer eyedropper highlights the
  sampled pixels' location in the scope. A later interactivity feature.

(CIE Chromaticity as a 5th scope type is already noted under the extensible
scope-type architecture.)

## Vectorscope graticule extensions (M6 polish — display only)

All of this is **drawing only** — no analysis change, no new colour maths beyond
the existing `RgbToYCbCr`. All of it sits behind a "Show Guides" toggle and is a
candidate for the M4 per-scope display-mode mechanism. Target milestone: M6
polish.

**Angular reference frame (settle once, reuse everywhere).** All angles are in
the **BT.709 Cb/Cr plane**: Cb on x (positive right), Cr on y (positive up),
angle counter-clockwise from +Cb. This is the frame the M4 vectorscope already
plots in, and the frame DaVinci Resolve uses — Resolve applies **no** NTSC/YIQ
rotation. **Measured from a Resolve screenshot** (least-squares circle fit; the
white skin line gives 123.08°, σ=0.09° over 1546 px). The six target boxes in
that screenshot measure 48.6 / 102.4 / 175.1 / 230.3 / 283.3 / 354.3° against
computed 49.7 / 102.9 / 174.8 / 229.7 / 282.9 / 354.8° (offsets are the letter
labels pulling the centroid). Compute all angles; use this table only as a
self-check:

| Target | Angle (Cb/Cr, BT.709) |
|---|---|
| Magenta | 49.7° |
| Red | 102.9° |
| *(skin tone line)* | *123.0°* |
| Yellow | 174.8° |
| Green | 229.7° |
| Cyan | 282.9° |
| Blue | 354.8° |

- **Skin tone line (flesh tone line).** Centre-to-rim line at **123.0°**, between
  red and yellow, closer to red; drawn neutral white, full radius. A plausibility
  guide: human skin of every ethnicity clusters at this hue angle (melanin/
  haemoglobin physics), which matters here because MetaHumans put photoreal skin
  into UE viewports. **This angle is a convention, not a standard** — Blackmagic
  publishes no value; it is the NTSC I-axis direction expressed in Cb/Cr, and it
  is what Resolve/FCP/Premiere/Media Composer converge on. Three independent
  confirmations: the NTSC I-axis derivation, a representative skin colour (sRGB
  0.80/0.60/0.50 → 124.9°), and the direct 123.08° measurement above. Record this
  next to the constant so nobody "corrects" it.
- **Hue ring on the rim.** Replace the plain outer circle with a coloured ring
  showing the fully-saturated hue at each angle (fast angle→hue reading).
  - Inner edge sits **exactly on today's circle radius**; the ring grows
    **outward only** and must never enter the plotting area (that circle is the
    100 % saturation reference — the reading must not change).
  - Width ≈ half the height of a 75 % target box; scale as a fraction of radius
    with a pixel min/max; final value by eye.
  - **Construction:** *not* an HSV/RGB wheel. Sweep fully-saturated RGB hues,
    convert each to Cb/Cr, place its colour at `atan2(Cr, Cb)`. Non-uniform
    spacing then falls out by itself (red→yellow 72°, yellow→green 55°), it
    matches Resolve/FCP, and it is automatically in register with the six
    targets — it cannot be mis-rotated because it comes from the same formula.
    Do not hand-place hues.
  - A fully-saturated ring is visually loud on the dark editor theme; keep it
    narrow and somewhat muted, behind "Show Guides". Resolve has **no** hue ring
    — this is a TrueScope addition, not a reproduction.
- **5° tick marks.** Every 5° (72 ticks), alternating long/short so they are
  countable. **Measured from Resolve:** ticks point **inward** from the circle —
  long ≈2.7 % of radius, short ≈1.5 %, alternating. This settles the layout: ticks
  inward, hue ring outward, no conflict. (Resolve's ticks do enter the plotting
  area slightly and that is fine; the hue ring must not.) **Ticks will not align
  with the targets** (102.9°, 49.7° are not multiples of 5) — correct, looks like
  a bug, add a comment, do not rotate to "fix" it.
- **100 % targets** in addition to the 75 % boxes, and a **2× zoom** for
  low-saturation material. **Caution (measured):** the six targets do **not**
  share one radius — magenta/green at 0.745 R, red/cyan ≈0.64 R, yellow/blue
  ≈0.63 R. The outer circle is a fixed chroma radius, not a per-hue gamut
  boundary. Derive each target's radius from its chroma magnitude, as M4 already
  does for the 75 % boxes; do not place the 100 % targets on a common radius.

All candidates for the M4 per-scope display-mode mechanism plus a "Show Guides"
toggle.

## [planned] Panel layout system and per-panel scope-type switcher

The two belong together and were deliberately split out of M6.4, which built the
container (the panel header) but not the feature.

- **Per-panel scope-type switcher.** The panel header's static type name becomes a
  dropdown; each panel picks its scope type independently. Showing the same type
  in two panels is **explicitly allowed** — a user may want two waveforms at
  different Trace Brightness settings. The prerequisite is already done: settings
  ownership is per panel (M6.4, see architecture.md).
- **Layout selection.** A layout button in the main bar opens a 4×4 icon grid of
  14 layouts: single; 1+1 and 1-left/1-right; 2×2; 2+1 and 1+2 in both
  orientations; 3+1 and 1+3 in both orientations; 1×3; 1×4. Two of the sixteen
  cells stay empty, and the last row's two icons are left-aligned, not centred.
- **Assets are ready and checked (2026-08-21).** Fourteen SVGs exist outside the
  repository (`Layout_one`, `Layout_1_top-1_bottom`, `Layout_1_left-1_right`,
  `Layout_2x2`, `Layout_2_top-1_bottom`, `Layout_1_top-2_bottom`,
  `Layout_2_left-1_right`, `Layout_1_left-2_right`, `Layout_3_top-1_bottom`,
  `Layout_1_top-3_bottom`, `Layout_3_left-1_right`, `Layout_1_left-3_right`,
  `Layout_1x3`, `Layout_1x4`). Reviewed in the planning layer: paths only — no
  text, filters, masks, gradients, CSS classes or embedded images; 16×16 viewBox;
  white fill (so a Slate tint multiplies correctly); every cut-out uses opposite
  path winding, so the holes render correctly under either fill rule and
  `fill-rule` needs no editing; each filename matches the geometry its transform
  actually produces (verified by recomputing the rotation matrices). The one open
  point was that they declare `width="100%" height="100%"` rather than numeric
  values — **answered in M6.4 Task 0 and no longer open: they need no editing.**
  Measured against the engine's own vendored nanosvg: a percentage length resolves
  against a zero reference length → 0, and `nsvg__scaleToViewbox` then substitutes
  the viewBox, so `width="100%" height="100%"` *with* a viewBox rasterises
  identically to numeric `width="16" height="16"` (16.000 × 16.000, same path
  bounds). Without a viewBox it collapses to the path bounding box instead — so
  the only requirement is that each file keeps its 16×16 viewBox. Copy them to
  `Resources/Icons/Layouts/` when this phase starts, not before, so unused assets
  are not shipped.
- **Deliberately after the first release.** The MVP does not need it, and the real
  work is not the icons but panel count, arrangement, persistence and the
  scope-type assignment. The per-scope space-usage policy that makes free
  arrangement safe (responsive scopes fill their panel, the vectorscope stays
  circular) is already settled in architecture.md.
- **Removes the temporary duplicate:** when the switcher lands, the scope-canvas
  right-click menu goes away; the panel menu becomes the single entry point.

## Window layout options (post-M6 feature)

*Scope now lives in the section above (added M6.4, 2026-08-21); this older item is
kept for its Final Cut Pro / Resolve reference detail.*

Let the user choose how many scopes are shown and how they are arranged in the
TrueScope tab — modeled on Final Cut Pro's scopes viewer (the richer, cleaner
UX reference) and DaVinci Resolve's Scopes window. The *architecture guardrail*
that makes this cheap to add later — per-scope space-usage policy (responsive
vs. aspect-locked vectorscope) and extensible scope-type enumeration — is
established in M3 (see architecture.md); this backlog item is the visible UI
built on top of it.

- **Layouts:** Single, Dual, Quad (current 2×2), vertical Row (stacked — good
  when docked to a left/right edge), horizontal Row. FCP offers ~12
  arrangements via an icon grid; start with the common ones and keep the set
  extensible. Icon buttons in the tab's title area (FCP style) are preferred
  over a text dropdown; final choice at build time.
- **Free arrangement, sensible default:** the user freely sizes and arranges
  panels; Quad (2×2) is the default so first open looks intentional.
- **Per-panel scope selection:** each panel has a small dropdown (like UE
  viewport's camera selector) to switch its scope type independently of the
  layout — e.g. Dual view with Parade left, switch right panel from Waveform
  to Histogram. Uses the extensible scope-type enumeration from M3.
- **Space-usage is a scope-type property, not a layout setting** (settled in
  architecture.md): responsive scopes (Waveform/Parade/Histogram) fill their
  panel; the Vectorscope always stays circular with margins. This is why free
  user arrangement does not conflict with correct scopes.
- Reference screenshots: DaVinci Resolve (Single/Dual/Quad/4×1) and Final Cut
  Pro (12-layout icon grid, Display submenu with Vertical Layout / Show Guides
  / Monochrome / brightness).

## [resolved in phase-5.2] GPU-path housekeeping (from phase-5.1)

All three items were delivered by phase-5.2 (2026-07-13):

- **Rename `FTrueScopeGpuParade` → `FTrueScopeGpuAnalysis`** — done, its own
  mechanical commit (`git mv` + identifier rename), suite green after.
- **"Analysis Path" menu placement** — the section now lives on the base
  scope widget, so every scope's right-click menu offers it ("Analysis Path
  (all scopes)"); the selection is global by decision (architecture.md,
  "Analysis path selection").
- **Luma waveform binned integer-exactly** — `PixelToLumaBin` (CPU) and its
  shader mirror in `TrueScopeParade.usf`, done before the GPU port, exactly
  as this item demanded; `TrueScope.GPU.WaveformBinEquivalence` pins it.


## [M7 input] CPU vs. GPU path cost on the M1 Mac Mini — measured, not settled

From the M6.4.1 condition-3 benchmark (2026-08-22, `/Game/Maps/StartMap`,
Scalability Epic, `r.ScreenPercentage` 100, `t.MaxFPS 0`, editor VSync off, High
DPI off). Full conditions and all four runs are recorded in `phase-6.4.md`.
**One machine, one run per condition — these are hypotheses, not results.** Do
not chase them here; M7 must take its own measurement before the default path
flips.

- **CPU idle came out faster than GPU idle** — 32.73 FPS (30.55 ms) against
  28.86 FPS (34.65 ms), at a matched capture cadence of ~5.1 Hz on both paths.
  *Hypothesis / unverified assumption.* If it holds it bears directly on the M7
  decision to flip the compiled default to GPU.
- **The CPU path hitched ~36× more often** — 108 hitches in 90 s (71.33/min)
  against 3 (1.97/min) on GPU; 107 of the CPU hitches fell in the 0.06–0.10 s
  bucket and 101 were render-thread-bound. *Hypothesis / unverified assumption:*
  the CPU path's readback `Lock` (M5: ≈2× frame time, `dev-rules.md`). The rate
  does not match cleanly — 1.19 hitches/s against 5.07 captures/s — so the cause
  is **suspected, not established**.
- **Taken together, CPU wins on average frame rate and loses on smoothness.**
  Neither figure alone decides the default, and averaging them would hide exactly
  the trade-off that matters. M7 needs a measurement designed for *this* question
  — both quantities, repeated runs, and a hitch analysis that actually identifies
  what stalls — rather than this one, which was designed to answer whether a
  slider drag is affordable.
- **During a drag the ordering reverses:** GPU re-derivation ran at 25.23 ms per
  frame against the CPU re-map's 18.52 ms under identical drag conditions, the
  difference sitting almost entirely on the game thread (17.87 vs 5.39 ms).
  Recorded here because it is the same M7 question seen from the other side. It
  did not affect the M6.4.1 verdict, where both paths passed comfortably.
