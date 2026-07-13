# Phase 5.1 — GPU path: switch responsiveness (Observation 2) + Vectorscope

Read `architecture.md` (Analysis path selection; Two count mappings; parity
rule), `phase-5.md` (the M5 GPU parade — infrastructure this phase reuses), and
`backlog.md`. This phase continues the GPU compute path. Two things: (1) fix the
path-switch responsiveness gap found in the M5 review, and (2) bring the
**Vectorscope** onto the GPU path as the next through-line scope.

Waveform and Histogram on the GPU path are deferred to the following sub-phase
(phase-5.2) — they inherit directly from the parade (waveform) and are simple 1D
(histogram), so the risk lives in the vectorscope's Cb/Cr scatter, which this
phase proves. The CPU path stays the default; the flip to GPU-default is still an
M7 decision.

Same hard constraints as M5: **uint atomics only, no wave intrinsics** (parity);
**no runtime readback on the GPU steady-state path**; `count -> texel brightness`
is **log**, never linear. If a decision-level surprise appears, stop and ask per
the `dev-rules.md` Rule 3 self-check.

## Task 1 (FIRST) — Path-switch responsiveness without a Realtime Viewport

**The problem (from the M5 review).** Switching the parade between CPU and GPU
only takes effect on the next capture. With a Realtime Viewport (the default) a
capture arrives within ~0.4 s, so the switch feels instant. But if the user has
**turned Realtime off** on their viewport, no new capture arrives until they
click the viewport, move the camera, or re-enable Realtime — so the switch
appears to do nothing, reads as a hang, and the user waits in vain.

**Hard constraint:** the user's Realtime-Viewport setting must never be changed.
Changing their viewport state to work around this is not acceptable.

### Task 0 (research first, then STOP and report before coding)

The fix approach depends on facts about the capture pipeline that must be read in
the 5.8 source and our own code, not assumed. Determine:

- **Does the last captured frame persist after a switch?** The capture RT
  (GPU-resident, pre-readback) is overwritten each capture, not cleared — confirm
  it still holds the last frame's pixels at switch time. This decides whether
  approach A works for the CPU->GPU direction.
- **Can a one-time GPU->CPU readback be triggered on demand** (to seed the first
  CPU-path frame after switching to CPU) without altering the steady-state
  throttle or the viewport? The human has accepted a single readback at the
  switch moment (it hits only the manual switch, not steady state).
- **For approach B (fallback):** can a single forced redraw be coupled with a
  one-time capture-gate open, so the forced frame is actually captured (a redraw
  alone may land on a throttled, un-captured frame — the gate must open for that
  one frame)? Confirm the mechanism without disabling the throttle permanently.

**Then stop and report:** the findings, and the chosen approach **per direction**
(A preferred; B as fallback where A is not feasible). Wait for confirmation before
implementing. This is the risky-phase Task-0 discipline from the handover.

### Implementation (after the Task-0 report)

- **Approach A (preferred): re-analyse the existing frame.** On switch, run the
  already-captured last frame through the newly selected path once — no redraw, no
  new capture, no viewport change.
  - CPU->GPU: feed the resident capture RT to the GPU compute pass (expected
    cheap — the RT is already on the GPU).
  - GPU->CPU: perform the accepted **one-time** readback of the last frame to seed
    the CPU path, then resume normal CPU operation. This one readback is a
    deliberate, documented exception — it hits only the manual switch, and the
    steady-state GPU path still never reads back.
  - Bonus, worth noting in the docs: because A re-analyses the *identical* frame,
    it is also the cleanest possible CPU-vs-GPU comparison (same input, no jitter).
- **Approach B (fallback, only where A is infeasible for a direction):** force one
  viewport redraw **and** open the capture gate for exactly that frame, so a new
  capture is guaranteed. Must leave the Realtime setting untouched afterwards.
- Whichever is used per direction, the switch must take visible effect promptly
  **regardless of the Realtime-Viewport setting**, without changing that setting.

### Task 1 acceptance

- [x] With Realtime Viewport **off**, switching parade CPU<->GPU takes visible
      effect promptly (no multi-second dead wait) — verified live via MCP with
      Realtime disabled.
- [x] The user's Realtime-Viewport setting is unchanged after the switch —
      verified.
- [x] Steady-state GPU path still performs **no** readback (the one-time
      GPU->CPU switch readback, if used, is the only exception and occurs only at
      the switch) — verified in logs.
- [x] Regression test at the switch/state layer with teeth.

## Task 2 — Vectorscope on the GPU path (through-line scope)

Port the Vectorscope to the GPU compute path, reusing the M5 parade
infrastructure (compute pass + GPU-resident intensity texture + path selection +
`FTrueScopeAnalysisResult` producer swap). The Vectorscope is the real new ground:
its scatter is into a 2D **Cb/Cr** plane, not column x luma.

- **Math on the GPU:** BT.709 R'G'B' -> Y'CbCr per pixel (same coefficients the
  CPU vectorscope uses); scatter each pixel into its (Cb, Cr) bin via
  `InterlockedAdd` on a `RWBuffer<uint>` / `RWTexture<uint>`. uint atomics only.
- **Density mapping:** `count -> texel brightness` is **log** (density field, like
  the parade) — not the linear curve-height mapping. Do not reintroduce the M4
  confusion.
- **GPU-resident draw:** the accumulation becomes the intensity texture Slate
  draws directly, same as the parade — no readback on the runtime path.
- **Graticule stays a fixed-size UI overlay** (75% targets, circular, aspect-
  locked) exactly as the CPU vectorscope draws it — the GPU path changes only how
  the density field is produced, not the graticule or the circularity.

### Equivalence test (GPU vs CPU, strict — as in M5)

- On the synthetic images (esp. **SMPTE color bars**, the vectorscope's key test,
  plus seeded noise), run the real runtime GPU scatter and compare Cb/Cr bin
  counts against the CPU vectorscope: per-bin `|diff| <= 1` with compensation only
  by an adjacent bin; a shifted, dropped, or mis-mapped distribution must fail.
- May read back in the **test harness** only (runtime stays readback-free).
- **Prove teeth:** perturb the scatter (swap Cb/Cr, or offset one axis by a bin)
  and confirm failure with a precise diagnostic, then revert. Colorbars must be in
  the set — a solid frame cannot reveal an axis swap.
- GPU tests need a real RHI (drop `-nullrhi`); CPU tests stay headless.

### Task 2 acceptance

- [x] Vectorscope renders live from the GPU path (selectable), visually
      indistinguishable from the CPU vectorscope — MCP screenshot done (pixel
      diff below the same-path temporal control); human review at sign-off.
- [x] Circular/aspect-locked at extreme panel shapes, graticule unchanged — no
      regression from the CPU vectorscope (MCP wide-panel screenshot; the
      aspect-lock draw path is untouched by this phase).
- [x] No runtime readback on the GPU vectorscope path — verified.
- [x] Equivalence test passes on the synthetic set incl. colorbars; teeth proven.
- [x] uint atomics only, no wave intrinsics, nothing platform-specific.

## Note (not a task this phase) — uneven empty-state, from the M5 review

With the GPU parade active, the still-CPU scopes behave inconsistently when their
path is idle: waveform/vectorscope hold the last trace, the histogram shows only
its axes. This is expected (they are on the stopped CPU path) and is **not fixed
here**. It becomes relevant when all four scopes are on the GPU path; the unified
"no data for this path" empty-state belongs to the final GPU sub-phase (phase-5.2
or a dedicated cleanup), not this one. Recorded so it is not rediscovered as a
bug.

## Overall acceptance

- [x] Builds clean (no new warnings).
- [x] Task 1 and Task 2 acceptance met.
- [x] Full automation suite green (CPU headless + GPU-RHI tests), count checked.
- [x] No new TrueScope warnings/errors in the Output Log.
- [ ] Human sign-off: visual CPU-vs-GPU vectorscope comparison + a switch test
      with Realtime Viewport off.

## Verification results (2026-07-13, this machine)

**Task 1 (switch responsiveness, Realtime off)** — driven via the
`TrueScope.AnalysisPath` CVar (routed through the same switch handling as the
UI; Slate context menus are not reachable over MCP), Realtime toggled off via
Ctrl+R with the capture-cycle log as the truth signal:

- Realtime off confirmed: 71 s with zero capture cycles before the first
  switch; the cycle counter stayed frozen through both switches.
- CPU->GPU at 10:18:28: switch log immediate, **no** readback cycle; the
  histogram (still CPU-fed) emptied to axes promptly — the GPU re-analysis of
  the resident frame replaced the result instantly.
- GPU->CPU at 10:20:14.755: the one-time seeding readback completed **64 ms**
  later (cycle 1174), histogram refilled promptly; no further cycles in the
  10 s after (Realtime still off, steady state clean).
- Steady-state GPU with Realtime **on**: 10 s of live capture on the GPU path
  produced zero readback cycles.
- Regression test teeth: cold-start-guard removal and direction swap each
  fail `TrueScope.Capture.PathSwitchDecision`.

**Task 2 (GPU vectorscope)** — all 25 automation tests green with a real RHI
(`-RenderOffscreen`), count checked (23 previous + 2D tolerance rule +
vectorscope equivalence; the switch-decision test added in Task 1 included).

- Equivalence: exact on the full synthetic set incl. SMPTE colorbars and
  seeded noise, after making the chroma binning **integer-exact on both
  paths** (`PixelToChromaBins` + shader mirror). Float binning was proven
  unstable by the test itself: every neutral pixel's chroma is 0 — exactly on
  the boundary between the two center bins — and CPU-vs-Metal FMA/ordering
  differences flipped whole gray-value populations between bins in bulk.
- Teeth: Cb/Cr axis swap and one-bin Cb offset in the shader each fail with a
  precise cell diagnostic (colorbars caught the swap, as designed).
- Visual: MCP screenshots CPU vs GPU — cross-path pixel difference of the
  vectorscope region (mean 1.33/255, 93.6 % identical) is *below* the
  same-path temporal control (GPU vs GPU seconds apart: 1.42/255, 93.4 %),
  i.e. indistinguishable beyond normal scene animation. Extreme-wide-panel
  screenshot: circle stays circular, graticule unchanged.

## Explicitly out of scope

Waveform and Histogram on the GPU path (phase-5.2); making the GPU path the
shipped default (M7); the unified empty-state cleanup (noted above); M6 status
line / settings / measurement-rate; density-curve tuning (M6); the deltaE2000
perceptual comparison (backlog). Ideas -> `docs/backlog.md`.
