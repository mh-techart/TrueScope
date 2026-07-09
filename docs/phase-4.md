# Phase 4 — All four scopes complete

Read `architecture.md` first (Settled decisions → Scales / Color math; Testing
strategy → scope cross-check). Phase 3 delivered Histogram + Parade on the CPU
path together with the architecture every scope reuses
(`FTrueScopeAnalysisResult`, `ETrueScopeType` + factory, per-scope space-usage
policy). This phase completes the four planned scopes by adding **Waveform**
and **Vectorscope**, changes the Histogram to the film/video channel layout,
and brings the scales to their settled form.

Still the CPU path — no compute shaders (M5). Correctness and consistency over
final polish: Resolve-grade density/curve tuning is deliberately M6 (backlog:
"Density-to-brightness curve tuning"), to be calibrated across all four scopes
at once.

If a decision-level surprise appears, stop and ask per the CLAUDE.md
self-check.

## Task 1 — Histogram: separate RGB channels (default) + overlay option

M3 draws the three histogram channels superimposed — the photo-editing
convention (Photoshop). Film/video tools (Resolve, FCP) stack the channels
separately, and UE for virtual production sits in the film/video camp with
Resolve as the reference standard. Therefore:

- **Default:** R, G and B drawn **separately, vertically stacked**, as in
  Resolve.
- **Keep the current superimposed view as a selectable display option**
  ("RGB overlay"), so users choose. Model it as a *display mode of the
  histogram*, not a separate scope type — the first concrete user of the
  "scope display modes" backlog idea, and where M3's extensible scope
  architecture pays off.

## Task 2 — Waveform

The waveform is the parade's mechanism **without channel separation**: density
per image-column × luma-bin, all channels combined into one trace (Resolve's
default waveform draws the combined RGB trace). Reuse the parade code path and
add it to the `ETrueScopeType` enumeration and factory — it should be small,
since the parade already solved the 2D mechanics.

## Task 3 — Vectorscope

The first **aspect-locked** scope: this is where M3's `bMaintainSquareAspect`
mechanism gets used for real.

- Math: BT.709 R'G'B' → Y'CbCr; plot each pixel at its (Cb, Cr) position,
  density-accumulated like the other scopes.
- **Always circular**, at any panel shape — it scales uniformly and centers
  itself with margins, never an ellipse, no matter how narrow or wide the
  panel (an ellipse would falsify hue angle and saturation). Verify at extreme
  panel proportions.
- Draw the standard graticule with 75 % targets (R/Mg/B/Cy/G/Yl), anti-aliased
  and as a fixed-size UI overlay (not part of the scaled graphic).
- **SMPTE color bars now become the key test** (deferred from M3 for exactly
  this milestone): a color-bars image must place its density into the six
  graticule target boxes.

## Task 4 — Scales to settled form

Per architecture.md "Scales":

- Waveform / Parade / **Histogram** all on the normalized **0–1023** axis
  (the histogram moves from M3's 0–100). One shared value axis across all
  scopes, matching Resolve; 0–1023 is a normalization convention, not a claim
  about the source's bit depth.
- Parade / Waveform gridlines every **128 (8 divisions)** for finer reading
  (M3 used 256 / 4 divisions).
- Axis labels remain fixed-size UI elements, legible at any panel size.

## Automation tests (headless, synthetic images)

Keep the seven existing tests (four original + three pure-channel no-bleed);
add:

- **SMPTE color bars:** vectorscope density lands in the six graticule
  targets; parade/waveform show the expected bar levels.
- **Histogram separate channels:** a known image produces the three expected
  stacked distributions on the 0–1023 axis; the overlay display mode renders
  the same data superimposed.
- **Waveform:** a horizontal gradient produces the diagonal trace (as the
  parade does, but unseparated).

## Acceptance criteria

- [ ] Builds via the CLAUDE.md Build.sh command with no new warnings.
- [ ] Histogram defaults to separate stacked RGB channels; the superimposed
      "RGB overlay" mode is selectable and works.
- [ ] Waveform and Vectorscope render live from the viewport capture and are
      registered in the scope-type enumeration/factory.
- [ ] Vectorscope stays circular with margins at extreme panel shapes (never
      an ellipse); 75 % graticule drawn anti-aliased.
- [ ] All four scopes read on the 0–1023 scale (histogram included);
      parade/waveform gridlines at 128 intervals.
- [ ] All automation tests pass (the seven existing plus the new SMPTE-bars,
      histogram-channel and waveform tests).
- [ ] **Scope cross-check vs. DaVinci Resolve** (architecture.md method,
      point 5) performed by the human on a ColorCalibrator frame for all four
      scopes; values agree on their scales (appearance may differ by mapping).
- [ ] Anti-aliased, no blocky pixelation (MCP screenshot, human review).
- [ ] No TrueScope warnings/errors in the Output Log.

## Explicitly out of scope (do not build yet)

GPU compute path (M5); settings UI / selectable measurement rate (M6);
interpolated draw rate (backlog); density-curve and opacity finish-tuning
(M6 — calibrate across all scopes together); HDR / other-color-space scales
(backlog); window layout options and per-panel scope dropdowns (post-M6); the
~41 ms render-thread readback optimization (backlog). Ideas →
`docs/backlog.md`.
