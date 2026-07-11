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
  `Lock` costs ≈ one host frame-time, its editor impact is self-scaling if the
  measurement rate adapts to frame-time: throttle so the readback never consumes
  more than a set fraction of editor time (e.g. "at most one lost frame per
  second"). On a slow machine this yields ~1 Hz automatically; on a workstation
  ~2.5 Hz or more — one rule, correct everywhere. Only relevant to the shipped
  CPU path (the GPU path has no readback). Ties into the settable rate (M6) and
  answers the M7 "is the CPU fallback tolerable?" question. Prefer expressing the
  budget in frame-times, not milliseconds.
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
  PollReadback_RenderThread`). The readback step costs **about one host
  frame-time per capture** (early millisecond figures on this machine were
  taken under uncontrolled conditions and have been discarded; the cost is the
  Metal GPU-drain inside `Lock`, not the copy — see `phase-3.md`,
  "Correction"). It runs on throttled capture frames (~2.5 Hz), so the editor
  stays interactive, but it is a periodic render-thread block of roughly one
  frame-time. Options to
  explore: `Lock` on the render thread but hand the mapped pointer + pitch to
  the background task for the slow copy (then `Unlock`); investigate whether
  the Metal `Lock` itself (not the memcpy) dominates and whether a
  persistently-mapped buffer readback avoids it; or reduce the capture
  resolution. Tie-in with the settable measurement rate (M6) and the GPU path
  (M5, which removes the CPU copy entirely for the GPU analysis result).
  **Status after the M5 path decision:** with the GPU path as the internal
  default from M5 on, the one-frame-time block no longer affects normal use, so this is
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
  the GPU-drain inside `Lock` (≈ one frame-time, size-independent), not the memcpy
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
