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

> **Superseded during the phase:** the waveform ships with both an RGB and a
> Luma display mode — see "Post-review addition — waveform Luma / RGB display
> modes" below. RGB remains the default.

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
add at minimum:

- **SMPTE color bars:** vectorscope density lands in the six graticule
  targets; parade/waveform show the expected bar levels.
- **Histogram separate channels:** a known image produces the three expected
  stacked distributions on the 0–1023 axis; the overlay display mode renders
  the same data superimposed.
- **Waveform:** a horizontal gradient produces the diagonal trace (as the
  parade does, but unseparated).

*(In the end eleven were added — the three above plus `Chroma709`,
`ColorBarsParade`, `UI.ValueScale`, `UI.HistogramChannelLayout`, and the four
post-review tests listed in the sections below. Final total: 18.)*

## Post-review addition — waveform Luma / RGB display modes

Task 2 originally specified only the combined RGB trace, which conflicted with
the `architecture.md` "Color math" row ("BT.709 R'G'B' → Y'CbCr for vectorscope
and **luma waveform**"). Resolved by giving the waveform both, user-switchable:

- **RGB** (default, as in Resolve) — the three channel densities superimposed;
  the view for colour balance. Keeping Resolve's default keeps the scope
  cross-check apples-to-apples.
- **Luma** — BT.709 luma only, drawn as a neutral trace; the classic exposure
  waveform.

Neither is "the" waveform, so neither is hardcoded. This is the second user of
the `FTrueScopeDisplayMode` mechanism from Task 1 and needed only the three
overrides. No new colour maths: `RgbToChroma` already derived the BT.709 luma
per pixel for the vectorscope and discarded it, so it was widened to
`RgbToYCbCr` and the analysis loop bins the luma it was already computing.
With this, the architecture's Color-math row is literally true and the
`backlog.md` "luma-only waveform" idea is delivered.

Two tests, split across the layers the histogram bug taught us to separate:

- `Analysis.LumaWaveform` — luma bins match the published BT.709 weights
  (pure R/G/B land on 54/182/18), independent of the RGB path.
- `UI.WaveformModeSelection` — each mode draws its own dataset: on a pure-green
  frame the RGB trace sits at bin 255 in green and the luma trace alone at bin
  182 in neutral. Verified to fail when the two modes are deliberately crossed.

## Post-review fix — histogram vertical scale

The M4 review (Resolve cross-check on a ColorCalibrator frame) found the three
stacked curves reading their channels in the wrong order — red appearing to
fall off last where Resolve shows it falling first.

**The channels were not swapped.** Measured against the same frame, each drawn
band correlated best with its own channel (band 0 vs R 0.90, vs B 0.65) and its
right edge landed exactly on that channel's true maximum bin.

The cause was the **vertical mapping**: the histogram ran its bin counts
through `TrueScope::CountToIntensity`, the log curve that maps a count to the
*brightness* of a density texel (parade, waveform, vectorscope), where lifting
sparse cells into visibility is the whole point. As a curve *height* it drew a
bin holding 22 pixels at 47 % of the band, and one holding 0.04 % of the frame
at 61 %. That flat-topped every curve (234 of 256 bins above half height) and
made each channel appear to run out to its rarest outlier pixel — so the
apparent fall-off order was decided by stray highlights, not by the
distribution, and the R/B order inverted.

Fixed by plotting the count linearly against the shared per-frame maximum. The
shared maximum stays: it is what keeps the three curves comparable. `Count →
brightness` (log) and `count → curve height` (linear) are now distinct and
documented, so the M5 GPU path cannot inherit the confusion.

> **Follow-up decision (M6):** the shared maximum is correct and stays — the
> three curves always share one scale, never per-channel, so their true ratio
> is preserved. What becomes optional is the **gain** on that shared scale:
> Resolve-style amplification (default; tallest peak clips, edge detail
> readable) vs. exact scaling (gain 1.0, M4's current behavior). Rationale
> and the open question of Resolve's exact curve: see the "Histogram vertical
> scaling" row in `architecture.md`. Implemented in M6 together with the
> density-curve tuning, so all scopes are calibrated at once.

Two tests close the gap the M4 suite left — the analysis tests stopped at the
bin arrays, and `UI.HistogramChannelLayout` only checked geometry:

- `UI.HistogramChannelAssignment` — a flat R=64/G=128/B=192 frame must peak
  each curve on its own value, and each curve's tint must be dominated by its
  own component. Verified to fail on a deliberately swapped `GetChannelBins`.
- `UI.HistogramCurveScale` — the curve height is proportional to the count, and
  the 116-pixel bin that inverted the order stays under 2 % of band height.

## Acceptance criteria

- [x] Builds via the CLAUDE.md Build.sh command with no new warnings.
      *(Self-verified: clean build, zero warnings.)*
- [x] Histogram defaults to separate stacked RGB channels; the superimposed
      "RGB overlay" mode is selectable and works. *(Self-verified via MCP: the
      right-click Display menu shows "Separate Channels" checked by default;
      selecting "RGB Overlay" superimposes the three curves on the same axis
      and the radio state follows. Modelled as a display mode on the base
      scope widget, so other scopes can add modes without new plumbing.)*
- [x] Waveform and Vectorscope render live from the viewport capture and are
      registered in the scope-type enumeration/factory. *(Self-verified via
      MCP screenshot of the live ColorCalibrator scene; the factory's
      placeholder branch now only serves future scope types.)*
- [x] Vectorscope stays circular with margins at extreme panel shapes (never
      an ellipse); 75 % graticule drawn anti-aliased. *(Self-verified by
      measuring the graticule in MCP screenshots: in a 400×170 panel the
      circle is 142×142 px, in an 80×168 panel 50×50 px — centred with
      margins in both.)*
- [x] All four scopes read on the 0–1023 scale (histogram included);
      parade/waveform gridlines at 128 intervals. *(Self-verified: the
      TrueScope.UI.ValueScale test pins the shared axis and both gridline
      sets; visible in the screenshot.)*
- [x] All automation tests pass (the seven existing plus the new SMPTE-bars,
      histogram-channel and waveform tests). *(Self-verified headless: 18/18
      Result={Success} — the 7 from M3 plus Chroma709, ColorBarsVectorscope,
      ColorBarsParade, WaveformHorizontalGradient, HistogramChannels,
      UI.ValueScale, UI.HistogramChannelLayout, the two post-review histogram
      tests UI.HistogramChannelAssignment and UI.HistogramCurveScale, and the
      two waveform-mode tests Analysis.LumaWaveform and
      UI.WaveformModeSelection.)*
- [x] **Scope cross-check vs. DaVinci Resolve** (architecture.md method,
      point 5) performed by the human on a ColorCalibrator frame for all four
      scopes; values agree on their scales (appearance may differ by mapping).
      *(First pass found the histogram scale bug above; after the fix the drawn
      curves match the frame's own per-channel histograms — peaks at 706/674/664
      vs. a ground truth of 702/674/666, correlation 0.99 per channel.
      **Human-confirmed:** waveform, vectorscope, parade and — after the fix —
      histogram all agree with Resolve on their scales. Remaining visual
      differences are the known density-weighting question, deferred to M6.)*
- [x] Anti-aliased, no blocky pixelation (MCP screenshot, human review).
      *(**Human-confirmed:** signed off, with and without High DPI.)*
- [x] No TrueScope warnings/errors in the Output Log. *(Self-verified: only
      Log-level capture/cost lines. Per-cycle cost after the vectorscope was
      added: analysis ~7.9 ms, unchanged within noise.)*

## Explicitly out of scope (do not build yet)

GPU compute path (M5); settings UI / selectable measurement rate (M6);
interpolated draw rate (backlog); density-curve and opacity finish-tuning
(M6 — calibrate across all scopes together); HDR / other-color-space scales
(backlog); window layout options and per-panel scope dropdowns (post-M6); the
~41 ms render-thread readback optimization (backlog). Ideas →
`docs/backlog.md`.

## Verification summary

Self-verified: clean build (0 warnings), 18/18 automation tests green (7 from
M3 + 11 added, count checked against expectation), all four scopes rendering
live, vectorscope circularity measured at extreme panel shapes, display modes
working, per-cycle analysis cost ~7.9 ms, no log entries above Log level.

Two correctness bugs were found in review and fixed within the phase (see the
post-review sections above): the histogram's vertical mapping, and — resolving
a spec conflict rather than deciding alone — the waveform's missing Luma mode.
Both got regression tests that were proven to fail on the reintroduced bug.

Incidental refactor, accepted: the parade's column→intensity pass was factored
into a shared helper so all four traces use one density curve instead of four
copies. Slightly beyond the phase scope, but it is a precondition for M6's
"calibrate the curve across all scopes at once" and is covered by the existing
parade tests.

**Human-confirmed:** Resolve cross-check for all four scopes, anti-aliased
finish, both display-mode switches. **Phase 4 signed off by the human as
complete.**
