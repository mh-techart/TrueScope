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
`STrueScopePanel::OnPaint`, checked in `IsActiveThisFrame_Internal`). Paint is
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

## While the GPU path is selected, the other three scopes show no fresh data (M5 interim — resolved by M5.x)

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

- **Cine Camera source via SceneCaptureComponent2D** — measure a selected
  camera *without* piloting it. Original MVP path, superseded by the
  viewport-only decision. Opt-in convenience only; must never become a
  requirement (would reintroduce scene-side state). Caveats to solve:
  mirroring camera/post settings, auto-exposure parity with a real viewport.
- **Scene-referred / HDR / OCIO measurement modes** — analyze pre-tonemap or
  OCIO-managed signals; log-scale waveform; values above display white;
  log-profile (S-Log etc.) zebra targets; color-space-aware calibration
  (vectorscope graticule and % interpretation for non-Rec.709 view
  transforms such as P3 or PQ). V1 is deliberately display-referred
  sRGB/Rec.709 only (see architecture.md).
- **Toolbar button** to open the TrueScope tab (V1: Window menu only).
- **Plugin icon (`Resources/Icon128.png`)** — the editor's Plugin Browser
  logs a missing-Slate-resource line for it; cosmetic only, but needed for a
  clean log and for Fab/marketplace presentation. Tab icon in the Window
  menu could reuse it (phase 1 deliberately ships without icons).
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
- **Density-to-brightness curve tuning ("scopes drowning in dense areas")** —
  observed in the M3 review: TrueScope's parade density is much denser than
  Resolve's, so structure drowns in solid fills. The likely cause is **not**
  capture resolution (lowering it would cost precision in other scopes) but
  the **intensity mapping**: our count→brightness curve is probably too
  linear, so dense regions saturate into an undifferentiated mass. Resolve
  keeps structure visible with a pronounced log/sqrt curve that lifts low
  densities and saturates high ones early; per-sample opacity is another
  lever. This is finish-tuning (M6) and should be calibrated **once across
  all four scopes together** for a consistent look — so tackle it after all
  scopes exist, not per-scope.
- **Histogram scale gain: Resolve-style default + exact mode** (M6, together
  with the density curve above) — the three curves always share one scale
  (derived from the strongest channel), never per-channel. The option is the
  **gain** on that shared scale: *Resolve-style* (default) amplifies past the
  band height so the tallest peak clips while edge detail becomes readable;
  *Exact* (gain 1.0, M4's current behavior) makes the tallest peak touch the
  top. Same draw path, one parameter — possibly a slider rather than a
  toggle. Before fixing details, **measure Resolve**: fixed vs. adaptive
  gain, and whether its plot is linear or slightly non-linear (sqrt-ish).
  Test with known bin ratios. Rationale: `architecture.md`, "Histogram
  vertical scaling".
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
  trace-brightness control, and per-scope modes for the parade and the
  vectorscope. These are display options on a scope, separate from the window
  layout (below). M4 built the mechanism (`FTrueScopeDisplayMode` + the
  right-click menu on `STrueScopeWidget`) and shipped its first two users — the
  histogram's Separate Channels / RGB Overlay pair and the waveform's RGB /
  Luma pair. Further modes only need the three overrides.
- **Minimum panel size / graceful degradation** — below a certain panel size the
  vectorscope stops being drawn. Reading it there is pointless anyway, so the
  behaviour is acceptable, but a scope that simply vanishes reads as a bug.
  **First find the actual cause** (measure, do not assume — intentional guard,
  clamp on non-positive size, or accident). Then replace the empty panel with a
  short centred hint, e.g. *"Panel too small — enlarge to use the Vectorscope"*
  (exact wording M6); below an even smaller threshold, degrade to nothing rather
  than clipped text. **Build it on the base scope widget**, not in the
  vectorscope, so every current and future scope inherits it — the vectorscope is
  just the first to hit the limit because it is aspect-locked.
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
- **Orthographic viewports are never measured — tell the user (deferred UX gap;
  belongs to the M6 status line).** Found during M4.1: with the level viewport in
  an ortho view (Top/Front/Left…), no capture ever happens — the log shows no
  "First viewport capture" at all and the scopes stay blank, until the viewport
  is switched back to Perspective, at which point capture starts immediately. The
  post-tonemap hook (`EPostProcessingPass::Tonemap`) evidently does not run on the
  ortho path; investigate against 5.8 source before assuming a cause.

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

## Window layout options (post-M6 feature)

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
