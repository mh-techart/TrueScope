# Phase 6.1 — Foundation & operation: texture resolution, status line, tab menu, settings

Read `architecture.md` (M6 row; "UI framework"; "Analysis path selection";
anti-pixelation recipe at the "scope textures at display resolution or higher"
line), `backlog.md` (esp. "Render scope textures at display resolution", the
status-line / menu / settings notes, ortho-view hint, minimum-panel-size hint),
and the phase-4.1 paused-indicator mechanism (`DrawCenteredHint`).

M6 is split into two phases. **This is M6.1 — foundation and operation**: the
things that are built and verified (texture resolution, status line, tab menu,
settings/persistence, the remaining empty-state hints). Appearance calibration
and new scope features (density-curve tuning, histogram gain, vectorscope
graticule, YRGB mode, scale ticks) are **M6.2**, written after this phase, and
deliberately depend on M6.1's texture-resolution fix being done first — calibrating
on an upscaled, distorted raster would have to be redone.

CPU path stays the default; the GPU-default flip remains M7. Same hard constraints
where GPU code is touched: uint atomics only, no wave intrinsics, no steady-state
readback. If a decision-level surprise appears, stop and ask (Rule 3 self-check).

**Tooling note:** MCP cannot open Slate right-click menus. Where a control is only
reachable that way and needs live verification, **pause and ask the human to
click / change the option** — the human has approved this. The tab menu built in
Task 2 should reduce how often this is needed (visible controls, not only
right-click), which is a deliberate side benefit.

## Task 1 (FIRST — foundation) — Render scope textures at display resolution

The root cause logged from phase-5.2: all four GPU scopes rasterize into fixed
targets (histogram 512×144; parade/waveform/vectorscope at `CaptureWidth × NumBins`)
that Slate then upscales to the panel, violating `architecture.md`'s anti-pixelation
rule ("scope textures at display resolution or higher") and producing thick,
slope-dependent, soft strokes. Fix it **uniformly for all four GPU scopes**, not
per scope.

- Plumb each widget's **plot rect in device pixels** into the GPU analysis/draw
  passes; size the scope render targets to it so **one texel = one device pixel**
  (the anti-pixelation rule). The bilinear upscale becomes identity; strokes match
  the CPU/Slate line; the slope-dependent anisotropy disappears.
- **Bucket the target sizes** to avoid render-target reallocation churn while the
  user drags the splitter (resize in steps, not every pixel).
- Accept the documented cost (larger render targets, ~few MB each, dynamic
  resizing) — it is the correct trade on the target hardware and is what the
  architecture already prescribes.
- **Bins/values are untouched** — this is a raster-resolution change only. The
  integer-exact bin-equivalence tests must stay green unchanged.

### Task 1 acceptance
- [x] All four scopes rasterize at panel (device-pixel) resolution; no fixed
      upscaled raster remains — verified in code.
- [x] GPU stroke matches the CPU/Slate line (no thick, no slope-dependence) —
      MCP screenshots per scope, and the phase-5.2 histogram stroke residual is
      gone (measure, don't eyeball, as phase-5.2 did).
- [x] No render-target reallocation churn while dragging the splitter (bucketed).
- [x] Bin-equivalence tests still green (values unchanged); no steady-state
      readback introduced; uint atomics / no wave intrinsics preserved.
- [x] No new warnings.

## Task 2 — TrueScope tab menu / toolbar (visible, not only right-click)

Per the "end user is an artist" constraint: user-facing options must be operable
from a visible TrueScope UI, not only the Slate right-click menu. Add a menu/
toolbar **inside the TrueScope tab** exposing:

- **Analysis path**: `GPU (performance)` / `CPU (compatibility)` — global, one
  control sets all four scopes (the decided global behaviour); neither labelled
  "default"; reflects/sets the same state as the diagnostic CVar.
- **Per-scope display modes** currently in the right-click menu (waveform RGB/Luma,
  histogram Separate/Overlay, etc.) surfaced here too, so they are MCP-testable and
  discoverable.
- A home for the measurement-rate control (the control itself can be minimal now;
  the adaptive-rate logic is backlog).

Keep it clean and artist-legible; this is not a settings dump. Match the editor's
dark theme.

### Task 2 acceptance
- [x] Analysis-path and display-mode options operable from the visible tab UI
      (not only right-click) — MCP-verified where possible; human click only where
      a residual Slate menu remains.
- [x] Global path control flips all four scopes; state consistent with the CVar.
- [x] No new warnings; matches editor theme.

## Task 3 — Settings & persistence

- Persist the user-facing options (analysis path, per-scope display modes, and any
  appearance toggles that already exist) across editor restarts, using the editor's
  standard per-user settings mechanism (the same class family the docking layout
  already persists through — read the source, don't assume the key).
- Docking position already persists (M1); this adds the scope options.

### Task 3 acceptance
- [x] Options survive an editor restart — verified live (set, restart, confirm).
- [x] No settings written to the level or project (editor-only, zero level
      footprint — the standing constraint).

## Task 4 — Measurement status line

Per `architecture.md` M6 row: a status line showing **what is being measured** and
**which path is active**. Content:

- Viewport type/name being measured (e.g. "Perspective", or the cine camera).
- Display encoding: e.g. "sRGB / Rec.709", or the active **OCIO view-transform
  name** if one is set (read how the editor exposes the active view transform;
  do not assume — report the source).
- Active analysis path (GPU / CPU).

This status line is also the natural home for the **not-measuring** states, unifying
the hints from earlier phases:

- **Paused (PIE):** reuse the phase-4.1 paused state — but now the status line can
  state it explicitly ("Paused — measured viewport in PIE") in addition to the
  in-panel hint.
- **Ortho view:** the logged gap — in an orthographic view TrueScope captures
  nothing. Surface it here ("No signal — orthographic view; switch to Perspective")
  rather than leaving blank scopes unexplained.
- **No viewport / no source:** the existing empty state, named here.

### Task 4 acceptance
- [x] Status line shows viewport, display encoding (incl. OCIO view-transform name
      when set), and active path — verified live.
- [x] Ortho view shows an explanatory status instead of silent blank scopes —
      verified live (switch a viewport to Top/Ortho).
- [x] Paused-in-PIE and no-viewport states are named in the status line,
      consistent with the in-panel hints.

## Task 5 — Minimum-panel-size hint (finish the empty-state family)

The backlog minimum-panel-size hint, built on the same `DrawCenteredHint` base-
widget mechanism as the paused/empty hints: below a size where a scope is
unreadable, show a short centered hint ("Panel too small") instead of a broken
render; below an even smaller threshold, degrade to nothing rather than clipped
text. Applies to all scopes via the base widget (vectorscope hits it first, being
aspect-locked).

### Task 5 acceptance
- [x] Below the readable threshold, each scope shows the hint via the shared
      mechanism; below the smaller threshold, nothing rather than clipped text —
      verified.

## Overall acceptance
- [x] Builds clean, no new warnings.
- [x] Tasks 1–5 acceptance met.
- [x] Full automation suite green (CPU headless + GPU-RHI), count checked; bin
      equivalence unchanged.
- [x] No new TrueScope warnings/errors in the Output Log.
- [x] Human sign-off: texture-resolution stroke match (all four scopes), tab menu
      operation, settings survive restart, status line incl. ortho + OCIO, panel-
      size hint.

## Verification record — phase complete (human sign-off 2026-07-17)

Tasks 1–3 landed 2026-07-14; Tasks 4–5 and the pre-sign-off fixes (work
order 13) landed 2026-07-17. Editor access for live verification ran over the
direct-HTTP MCP fallback (harness tools unavailable that session; permitted
path, stated per the dev-rules).

- **Task 1 (raster resolution):** stroke re-measured at device pixels — GPU
  1.43/1.02 px vs CPU 1.28/0.97 px, slope-dependence gone (record in
  `backlog.md`). Bucketed allocation covered by `TrueScope.UI.RasterBucket`.
- **Task 2 (toolbar) & Task 3 (persistence):** operated live throughout the
  work-order session (path/mode/rate switches from the visible toolbar);
  options confirmed surviving several editor restarts during the
  build-relaunch loops.
- **Task 4 (status line):** all states observed live — Measuring (viewport,
  piloted CineCameraActor and plain CameraActor by name), ortho "No signal",
  and the display encoding incl. the **live OCIO view-transform name**
  (human-verified: instant flip on Enable-Display toggle and transform
  change). Test `TrueScope.UI.StatusLine`.
- **Task 5 (panel-size hint):** shared rule on the base widget; test
  `TrueScope.UI.PanelSizeHint`.
- **Work order 13 fixes**, each verified live with the human in the loop:
  1. *Ortho → placed-camera resume* — root cause was an engine gap (show
     flags never re-applied on the camera-lock path; minimal render path
     registers no SVE callbacks — settled insight in `architecture.md`,
     "Capture exists only while…"). Repair verified for CineCameraActor and
     plain CameraActor; ortho → Perspective control case confirmed untouched
     (zero repair activations). Test `TrueScope.Capture.ShowFlagRestore`,
     teeth proven.
  2. *Histogram line brightness* — GPU line now receives Slate's
     vertex-colour encoding from one shared conversion; measured pixel-exact
     on MCP screenshots: secondary-channel medians agree within 0–3/255 in
     both paths × both display modes (pre-fix delta ≈ 47/255).
  3. *OCIO view-transform name* — final form reads the live
     `FOpenColorIODisplayManager` via the human-decided hard OCIO plugin
     dependency (decision + full findings in `architecture.md`); the interim
     reflection soft path was rejected after live testing showed it lags
     in-session toggles.
- **Suite:** 32/32 green on `-nullrhi` and on the real Metal RHI
  (`-RenderOffscreen`), count sanity-checked each run; zero new warnings.
- **Human sign-off:** final visual check of all Overall-acceptance items
  confirmed by the human on 2026-07-17 ("checks done, perfect").

## Explicitly out of scope (this is M6.2)

Density-to-brightness curve calibration; histogram gain (Resolve-style vs. Exact);
vectorscope graticule extensions (skin-tone line, hue ring, 5° ticks, 100% targets);
histogram YRGB mode; parade/histogram scale intermediate ticks. All of these are
appearance/feature work that depends on Task 1 (texture resolution) being done, and
belong to M6.2. Also out: making the GPU path the shipped default (M7); adaptive
measurement-rate logic (backlog); the ΔE2000 perceptual harness (backlog). Ideas →
`docs/backlog.md`.
