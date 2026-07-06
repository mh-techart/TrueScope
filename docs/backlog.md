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
- **Higher refresh rates / per-frame mode** — trivial once the GPU path (M5)
  exists; expose as a setting with a resource-use warning.
- **Fab listing (free)** — companion distribution channel to GitHub for
  Blueprint-only users; verify current Fab submission terms at M7 time.
- **Engine version ports (±1 minor)** — after M6; per-version binaries in
  GitHub releases.
- **RGB overlay / luma-only waveform display modes** — small UI/analysis
  variants once M4 is stable.
