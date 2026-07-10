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
- **Cheaper readback retrieval (render-thread cost)** — M3 measured the
  render-thread step that maps the staging texture and copies it to CPU
  (`Lock` + row-by-row copy + `Unlock` in `FTrueScopeAnalyzer::
  PollReadback_RenderThread`) at ~41 ms per capture on this machine (Metal,
  960×540 BGRA8) — the dominant per-cycle CPU cost and far above the ~7 ms
  analysis. It only runs on throttled capture frames (~2.5 Hz), so the editor
  stays responsive, but it is a periodic render-thread hitch. Options to
  explore: `Lock` on the render thread but hand the mapped pointer + pitch to
  the background task for the slow copy (then `Unlock`); investigate whether
  the Metal `Lock` itself (not the memcpy) dominates and whether a
  persistently-mapped buffer readback avoids it; or reduce the capture
  resolution. Tie-in with the settable measurement rate (M6) and the GPU path
  (M5, which removes the CPU copy entirely for the GPU analysis result).
  **Status after the M5 path decision:** with the GPU path as the internal
  default from M5 on, the ~41 ms hitch no longer affects normal use, so this is
  **not a release blocker**. It does still affect the shipped CPU compatibility
  path. **M7 acceptance question:** is the CPU path, as shipped, tolerable for a
  user who has to fall back to it? If not, do the optimization there, or lower
  the capture resolution for the CPU path only. Independently, the
  instrumentation experiment that precedes `phase-5.md` answers a question that
  **decides M5's architecture**: if the ~41 ms are a fixed cost of the map/lock
  call rather than of the memcpy, then the GPU path must contain **no readback
  at all** (results stay GPU-resident), because even a few kilobytes would cost
  the same.
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
- **Vectorscope graticule extensions** — the M4 graticule ships the six 75 %
  targets only. Missing, all standard in Resolve/FCP and all display-only
  (no new analysis): the **skin-tone line** (I-line, ≈123°), **100 % targets**
  in addition to the 75 % boxes, and a **2× zoom** for reading low-saturation
  material. Candidates for the per-scope display-mode mechanism (M4) plus a
  "Show Guides" toggle.
- **PIE in a separate editor window** — unknown behaviour. PIE launched as
  "New Editor Window" runs inside the editor process, so the SceneViewExtension
  may well see it, but the "last focused **Level Editor** viewport" selection
  likely filters it out — meaning TrueScope would keep measuring the level
  viewport while the user looks at the PIE window. To be verified by
  experiment (see the readback/PIE work order). Not a deal-breaker either way:
  if it works, it is a bonus for end users; if not, it becomes a documented
  limitation. **Standalone Game is not a candidate** — separate process, no
  editor module, out of scope by construction; document it in the README.

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
