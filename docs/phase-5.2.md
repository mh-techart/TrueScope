# Phase 5.2 — GPU path: Waveform + Histogram, global path selection, unified empty-state

Read `architecture.md` (esp. "Two distinct count mappings", "Histogram vertical
scaling", "Analysis path selection", "Per-scope display modes"), `phase-5.md` and
`phase-5.1.md` (the GPU infrastructure and integer-exact binning this phase
reuses), and `backlog.md`. This phase **completes the GPU compute path for all
four scopes** and unifies their behaviour.

After this phase all four scopes (Parade, Vectorscope, Waveform, Histogram) run on
the GPU path. The CPU path stays the default and fully working; the flip to
GPU-default remains an M7 decision gated on all-platform verification.

Same hard constraints as M5/M5.1: **uint atomics only, no wave intrinsics**
(parity); **no runtime readback on the GPU steady-state path**; integer-exact
binning (see Task 1). If a decision-level surprise appears, stop and ask per the
`dev-rules.md` Rule 3 self-check.

## Task 1 — Waveform on the GPU path

The waveform is the parade's near-twin: column × luma accumulation. It inherits
the M5 parade compute/draw infrastructure. Two things differ:

- **Full width, not thirds.** The parade splits the width into three side-by-side
  channel plots; the waveform plots the full width. In **RGB** display mode all
  three channels overlay in one plot; in **Luma** mode a single luma channel is
  plotted (see the per-scope display modes in `architecture.md`).
- **Integer-exact binning from the start (mandatory).** The luma binning MUST be
  integer-exact on both CPU and GPU, exactly as the chroma binning was fixed in
  phase-5.1 — this is the same bin-boundary instability class (neutral/black
  pixels land exactly on a bin boundary, and CPU-vs-Metal FMA/ordering
  differences otherwise flip whole populations between adjacent bins). Do not
  port float binning and "fix it later"; do it integer-exact on day one. (Backlog
  already flags this.)
- Density mapping is **count → brightness, log** (density field, like the parade).

## Task 2 — Histogram on the GPU path

The histogram is 1D (luma-bin → count per channel), the simplest accumulation.
The trap is the **draw mapping**, which is different from the other three scopes
and already caused the M4 bug:

- **`count → curve HEIGHT` (linear, against the frame maximum) — NOT the log
  density brightness mapping.** The parade/waveform/vectorscope map count → pixel
  brightness (log); the histogram maps count → curve height (linear). Applying the
  log brightness curve as a height flat-topped the M4 histogram. Keep the two
  mappings **separately named in code** (architecture guardrail). The GPU path
  must inherit the distinction, not the confusion.
- **The three curves share ONE scale**, derived from the strongest channel's peak
  — never scaled per channel (settled decision). Reproduce the **current M4
  shared-scale behaviour**; do **not** implement the Resolve-style-vs-Exact gain
  option here — that gain tuning is explicitly **M6** (`architecture.md`,
  "Histogram vertical scaling"). This phase only moves bin *production* to the
  GPU; the draw/scale stays as M4 shipped it.
- Display modes (Separate Channels / RGB Overlay) already exist (M4) and must keep
  working through the GPU producer.

## Task 3 — Global analysis-path selection (decided: global, not per-scope)

With all four scopes GPU-capable, the `GPU (performance)` / `CPU (compatibility)`
option is **global — one switch sets all four scopes at once**, not per scope.
Decision rationale (record it): a per-scope switch would make the user set the
same option four times; simple operation for the artist takes priority. A
per-scope override, if ever wanted, is a debug-only concern and stays out.

- Extend the phase-5.1 switch-responsiveness fix (Approach A: re-analyse the
  resident last frame once, no redraw/new capture, Realtime setting untouched;
  one-time GPU→CPU readback at the switch) so **all four scopes** switch promptly
  and together, regardless of the viewport's Realtime setting.
- The diagnostic CVar remains, now global.

## Task 4 — Unified empty-state (Observation 1 from the M5 review)

With scopes on mixed paths during earlier phases, empty behaviour was
inconsistent (waveform/vectorscope held the last trace; histogram showed only
axes). Now that all four are GPU and switch together, make the "no data for the
active path / no valid source" presentation **consistent across all four scopes**,
reusing the `DrawCenteredHint` mechanism built in phase-4.1. One clear, uniform
empty-state; no scope silently holds a stale trace while another blanks.

## Task 5 — Rename `FTrueScopeGpuParade` → `FTrueScopeGpuAnalysis`

The class is no longer parade-specific (it now serves all four scopes). Rename the
class and its files (`git mv` + identifier rename), as flagged in the backlog.
Keep it a **separate, mechanical commit** so the rename diff does not tangle with
the functional changes above.

## Equivalence tests (GPU vs CPU, strict — per scope)

For **each** of Waveform and Histogram, on the synthetic image set (solid colours,
gradients, **SMPTE colorbars**, flat mid-gray, seeded noise, and — critically —
neutral/black frames that sit on bin boundaries):

- Compare bin counts GPU vs CPU: **integer-exact** (the phase-5.1 standard), with
  at most a compensating ±1 only where genuinely unavoidable. A neutral/black
  frame must not flip populations between center bins — that is the exact failure
  phase-5.1 exposed; assert it cannot recur.
- May read back in the **test harness** only (runtime stays readback-free).
- **Prove teeth** per scope: perturb the binning (offset one bin, swap a channel)
  and confirm a precise failure, then revert. Include the frames that actually
  exercise the perturbation (a solid frame cannot reveal a channel swap).
- GPU tests need a real RHI (`-RenderOffscreen`); CPU tests stay headless
  (`-nullrhi`). Check the test count against expectation.

## Acceptance criteria

Verification record 2026-07-13 (self-verified except where marked human):

- [x] Builds clean, no new warnings.
- [x] Waveform and Histogram each render live from the GPU path, visually
      indistinguishable from their CPU versions (both display modes each) — MCP
      screenshots, human review. *(See "Verification results" below; human
      review is the sign-off item at the end of this list.)*
- [x] Histogram uses count→height (linear, shared single scale as M4 shipped);
      NOT the log density mapping — verified visually (no flat-topping) and the
      two mappings are separately named in code (`CountToCurveHeight` in
      `TrueScopeHistogram.usf` / `GetCurveHeight` on the widget vs.
      `CountToIntensity` in the density shaders).
- [x] No runtime readback on any GPU scope path — verified (the only readback is
      the one-time GPU→CPU switch seed).
- [x] Global path switch: one control flips all four scopes together, promptly,
      with Realtime off, viewport setting untouched — verified live via MCP.
- [x] Unified empty-state: all four scopes present "no data" identically via
      `DrawCenteredHint` — verified by the automation test
      `TrueScope.UI.EmptyState` (drives the four real widgets and the paint
      branch). **A live screenshot of the empty state was not produced:** the
      editor always captures a frame within the first few frames of startup, so
      the scopes always hold data; forcing a never-capturing session would have
      required persistently changing the human's viewport settings, which the
      settings-hygiene rule forbids. Left to the human sign-off below.
- [x] Equivalence tests pass integer-exact for Waveform and Histogram incl. the
      neutral/boundary frames; teeth proven per scope.
- [x] `FTrueScopeGpuParade` renamed to `FTrueScopeGpuAnalysis` in its own commit;
      suite still green afterwards.
- [x] uint atomics only, no wave intrinsics, nothing platform-specific.
- [x] Full automation suite green (CPU headless + GPU-RHI), count checked; no new
      TrueScope warnings/errors in the Output Log.
- [ ] Human sign-off: visual CPU-vs-GPU for waveform + histogram (both modes) and
      a global switch test with Realtime off. *(Also worth a look: the live
      empty-state, which automation covers but no screenshot shows.)*

## Verification results (2026-07-13, this machine)

**Automation suite** — 28/28 green headless (`-nullrhi`) and 28/28 with a real
RHI (`-RenderOffscreen`). Count checked: 25 before this phase + 3 new
(`GPU.WaveformBinEquivalence`, `GPU.HistogramBinEquivalence`,
`UI.EmptyState`).

**Equivalence (GPU vs CPU, integer-exact).** Both new tests run the *real*
runtime passes (column binning; column binning → histogram reduce) and read the
bins back in the harness only. On the synthetic set — solid red, horizontal and
vertical gradients, SMPTE 75 % colorbars, flat mid-gray, seeded noise at capture
resolution, plus a **new solid-black 960×540 boundary frame** (neutral chroma 0
and value 0: the exact failure class phase-5.1 exposed) — GPU and CPU bins match
**exactly**, no ±1 compensation needed anywhere. The histogram's shared
strongest-channel maximum (the one scale all three curves share) matches the CPU
`GetSharedMaxCount` exactly on every frame.

*Teeth, per scope (each perturbation applied to the real shader, test failed with
a precise diagnosis, then reverted; suite re-run green):*

| Scope | Perturbation | Failure |
|---|---|---|
| Waveform | luma bin +1 (off-by-one) | `channel 3 column 0 bin 54: count diff -540 exceeds +/-1 (cpu 540, gpu 0)` |
| Waveform | luma replaced by the green channel | `channel 3 column 0 bin 0: count diff 540 (cpu 0, gpu 540)` |
| Histogram | reduce reads bin+1 (one-bin shift) | `channel 0 column 0 bin 254: count diff 518400` |
| Histogram | reduce reads the next channel (swap) | `channel 0 column 0 bin 0: count diff 518400` |

**Live MCP verification** (editor running, Slate-inspector + logs toolsets; the
CVar `TrueScope.AnalysisPath` drives the same switch path as the UI menu, which
Slate popup menus still prevent MCP from clicking — unchanged since M5):

- *CPU vs GPU, both display modes.* Region-wise pixel comparison of the panel
  quadrants, always against a same-path temporal control (two frames seconds
  apart), because the scene keeps animating:

  | Region | Cross-path (CPU vs GPU) | Same-path control |
  |---|---|---|
  | Waveform RGB | 2.34/255 | 1.38 (CPU) / 1.98 (GPU) |
  | Waveform Luma | 1.84/255 | 2.11 (CPU) / 1.89 (GPU) |
  | Histogram Separate | 3.89/255 | 0.57 (CPU) / 2.17 (GPU) |
  | Histogram RGB Overlay | 4.66/255 | 1.09 (CPU) / 1.86 (GPU) |
  | Parade (regression) | 0.77/255 | 0.45 (CPU) |
  | Vectorscope (regression) | 0.25/255 | 0.28 (CPU) |

  Waveform, parade and vectorscope sit **at or below** their temporal controls,
  i.e. indistinguishable beyond scene animation. The histogram sits slightly
  above its control: its curves are now *rasterized* rather than drawn as Slate
  vectors, so the anti-aliased outline lands on marginally different sub-pixels.
  Curve shapes, peak positions and the shared scale are identical by eye and by
  the exact bin equivalence above. **One real bug was found this way and fixed:**
  the GPU rasters were first drawn without `NoGamma`, so Slate gamma-corrected
  the sampled texels while the CPU path's vertex colours were not corrected — the
  GPU fills rendered visibly lighter (own commit).
- *Histogram mapping.* No flat-topping on either path; the peaks reach the band
  top and the low-count regions stay low, exactly as M4 shipped it.
- *No runtime readback.* With the GPU path active and Realtime on, the analyzer's
  per-cycle readback log goes **completely silent** while all four scopes keep
  updating live (verified with Verbose logging on the analysis category); the
  only cycle that appears at all is the single seeding readback logged at a
  GPU→CPU switch.
- *Global switch with Realtime off.* Realtime disabled (Ctrl+R; capture cycles
  confirmed frozen in the log), then CPU→GPU: the switch log appears immediately,
  **no** readback cycle, and **all four scopes** re-render from the resident last
  frame at once. GPU→CPU: the one-time seeding readback fires (one cycle logged,
  ~25 ms) and all four scopes refill promptly. The viewport's Realtime setting was
  untouched by the switch itself and restored afterwards.

*Editor settings touched during verification and restored afterwards:* Realtime
Viewport (toggled off for the switch test, restored on), the analysis-path CVar
(back to -1), the viewport view type (back to Perspective), the TrueScope tab
(back to open + foreground). The editor was left running with the CPU path
default and the default display modes.

## Explicitly out of scope

Histogram Resolve-style-vs-Exact gain tuning and density-curve calibration (M6);
making the GPU path the shipped default (M7); the M6 status line / settings /
measurement-rate; HDR / non-Rec.709 scale behaviour (backlog); the ΔE2000
perceptual comparison (backlog); per-scope path override (rejected — global is
the decision). Ideas → `docs/backlog.md`.
