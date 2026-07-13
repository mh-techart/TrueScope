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

- [ ] Builds clean, no new warnings.
- [ ] Waveform and Histogram each render live from the GPU path, visually
      indistinguishable from their CPU versions (both display modes each) — MCP
      screenshots, human review.
- [ ] Histogram uses count→height (linear, shared single scale as M4 shipped);
      NOT the log density mapping — verified visually (no flat-topping) and the
      two mappings are separately named in code.
- [ ] No runtime readback on any GPU scope path — verified (the only readback is
      the one-time GPU→CPU switch seed).
- [ ] Global path switch: one control flips all four scopes together, promptly,
      with Realtime off, viewport setting untouched — verified live via MCP.
- [ ] Unified empty-state: all four scopes present "no data" identically via
      `DrawCenteredHint` — verified.
- [ ] Equivalence tests pass integer-exact for Waveform and Histogram incl. the
      neutral/boundary frames; teeth proven per scope.
- [ ] `FTrueScopeGpuParade` renamed to `FTrueScopeGpuAnalysis` in its own commit;
      suite still green afterwards.
- [ ] uint atomics only, no wave intrinsics, nothing platform-specific.
- [ ] Full automation suite green (CPU headless + GPU-RHI), count checked; no new
      TrueScope warnings/errors in the Output Log.
- [ ] Human sign-off: visual CPU-vs-GPU for waveform + histogram (both modes) and
      a global switch test with Realtime off.

## Explicitly out of scope

Histogram Resolve-style-vs-Exact gain tuning and density-curve calibration (M6);
making the GPU path the shipped default (M7); the M6 status line / settings /
measurement-rate; HDR / non-Rec.709 scale behaviour (backlog); the ΔE2000
perceptual comparison (backlog); per-scope path override (rejected — global is
the decision). Ideas → `docs/backlog.md`.
