# Phase 6.3 — Appearance calibration: density transfer + histogram gain

Read `architecture.md` first (rows "Density transfer & Trace Brightness", "Two
distinct count mappings", "Histogram vertical scaling", "Gain slider law",
"Per-scope display modes"), and `dev-rules.md` ("Resolve cross-check — conditions &
procedure", the evidence-status rules, parity rules). This phase makes the scopes
*look* like Resolve, using transfers that were measured, not guessed.

M6.1 fixed raster resolution; M6.2 added graticule and feature work. M6.3 calibrates
the **intensity and height transfers** and adds the two artist controls:

1. **Density fields (parade, waveform, vectorscope): count → brightness changes from
   logarithmic to LINEAR × a per-scope gain, hard-clipped at white.** The log
   transfer (`ln(1+16·C)/ln(1+16·C_max)`) over-lifts sparse cells — a single pixel in
   a 540-row column reached 31 % brightness — which is the cause of "structure
   drowns in dense areas". Resolve's transfer was measured strictly linear.
2. **Per-scope "Trace Brightness" control** = that linear gain, one per density
   scope. Per-scope rather than global, because the three scopes present their
   values differently and one setting does not fit all.
3. **Histogram: a fixed shared scale with a single "Histogram Gain" slider.** The
   scale no longer follows the frame's own peak.

Both sliders use the same logarithmic law (`architecture.md`, "Gain slider law").
CPU stays the shipped default; both paths must produce identical values. If a
decision-level surprise appears, stop and ask.

## Task 0 — Confirm the ground truth in code, then proceed

Re-confirm in the current tree — a fresh session, so do not trust remembered
numbers. Record file/function/line for each:

- The density mapping: `CountToIntensity` (CPU) and its GPU mirror in the density
  shader, the gain constant, and the vectorscope's separate normalisation basis (it
  normalises against total pixel count, not column height).
- The histogram height normalisation (`GetSharedMaxCount` / the shared-max draw
  path, CPU + GPU). This is what the fixed threshold replaces: today the scale is
  derived from the frame's tallest bin, which is exactly the behaviour being
  removed.
- How a **per-scope** parameter reaches both the CPU analysis and the GPU shader for
  each density scope. The analysis-path switch is global; Trace Brightness is not,
  so it needs a per-scope route. Reuse the M6.1 toolbar + settings/persistence
  mechanism and the M4 `FTrueScopeDisplayMode` per-scope plumbing.
- The diagnostic test `TrueScope.Diagnostics.DensityTransfer` — it currently pins the
  log transfer and must be changed to pin the linear one.

**Two traps to check before writing any transfer code — both are stop-and-report if
they do not hold:**

- **How many bins does the histogram use?** This is not documented anywhere and it
  changes the meaning of the default. The 1.67 % threshold was measured against
  Resolve's binning of the 0–1023 range. If TrueScope bins the same range into a
  different number of containers, the same image produces different counts per
  container and the same threshold gives a different picture. Confirm the count, and
  if it is not 1024, report before proceeding — the threshold has to be rescaled by
  the ratio, and that is a decision, not an implementation detail.
- **What pixel count is the threshold a fraction of?** It must be the total pixel
  count of the frame actually analysed (the capture resolution), not the viewport
  size and not the scope texture size. Getting this wrong makes the histogram change
  appearance when the user resizes the panel, which is precisely the resolution
  independence this design exists to provide.

You may then implement directly.

## Task 1 — Density transfer: log → linear × per-scope gain, hard clip

- Replace the log transfer with **`Intensity = saturate(Count × Gain)`** (hard clip
  at 1.0 = white) for all three density fields, in **both** CPU and GPU paths, kept
  formula-identical (parity). `Gain` is the per-scope Trace Brightness from Task 2.
- The bin **counts are unchanged** — this is a transfer/draw change only. The
  integer-exact bin-equivalence tests must stay green untouched.
- Retire the log path; it is not kept as a mode (the gain covers lifting the low
  end). Keep the mapping **separately named** from the histogram's height mapping.
- The vectorscope keeps its own normalisation basis; only the curve family changes.
  Its default gain is tuned separately in Task 5, which is what per-scope gain is
  for.

## Task 2 — Per-scope "Trace Brightness" control (UI + plumbing + persistence)

- Add a **Trace Brightness** slider to each density scope (parade, waveform,
  vectorscope), operable from the **visible M6.1 tab toolbar/menu**, not only from
  the right-click menu — the end user is an artist.
- Use the shared logarithmic slider law: ±3 doublings, centre = that scope's
  calibrated default gain (Task 5), continuous, centre marked and snapping,
  double-click resets to centre, **no numeric readout**.
- Plumb the per-scope gain to the CPU analysis and the GPU shader for that scope.
- **Persist per-scope** via the M6.1 settings mechanism; survives editor restart.
  Persist the **gain multiplier** (1.0 = that scope's default), not the slider
  position — so a later change to the slider range does not move every existing
  user's setting.

## Task 3 — Histogram: fixed shared scale + Histogram Gain slider

- Replace the frame-peak normalisation with a **fixed clip threshold**: a bin whose
  count reaches **1.67 % of the analysed frame's total pixel count** draws at full
  band height; more than that is **hard-clipped** at the band top. Linear count →
  height throughout.
- **Shared single scale** across all channels (never per-channel) — unchanged from
  M4. Applies to both RGB and YRGB display modes.
- Add a **Histogram Gain** slider using the same logarithmic law: ±3 doublings
  around the default, so the effective threshold runs 13.3 % (far left) → 1.67 %
  (centre) → 0.21 % (far right). Continuous, centre marked and snapping,
  double-click resets, **no numeric readout**. Operable from the visible toolbar.
- Both CPU and GPU paths, value-identical. Persist the gain multiplier (M6.1).
- Keep count → height **separately named** from the density count → brightness
  mapping.

## Task 4 — Tests

Each regression below must be **proven by deliberately breaking the thing it guards,
confirming the test fails, and reverting**. A test never seen to fail proves nothing.

- **Density:** update `TrueScope.Diagnostics.DensityTransfer` to pin the **linear**
  transfer, and add a regression asserting `Intensity == saturate(Count × Gain)`
  across a range of counts and gains on **both** paths.
- **Histogram threshold:** a bin holding exactly the threshold fraction reaches the
  band top; the same bin plus one pixel clips; half the threshold draws at half
  height (linearity).
- **Histogram resolution independence:** the same *fractions* in two different frame
  sizes produce the same heights. This is the property that makes an HD and a UHD
  project look alike, and it is cheap to pin — the earlier assumption that the scale
  might be an absolute pixel count was only ruled out by measurement.
- **Slider law:** `gain(0.5) == 1.0` exactly; equal travel produces equal
  multiplication (e.g. `gain(p+0.1)/gain(p)` is constant across `p`); endpoints are
  0.125× and 8×.
- **Persistence:** the stored quantity is the gain multiplier — a test that changes
  the slider range must not change what an existing stored setting means.
- Integer-exact **bin-equivalence** tests stay green unchanged (counts untouched).
- Full suite green on `-nullrhi` and real RHI (`-RenderOffscreen`); count checked
  against the expected number.

## Task 5 — Visual calibration against Resolve (iterate)

Per `dev-rules.md`, "Resolve cross-check — conditions & procedure".

- **Tune the default per-scope Trace-Brightness gains headless first**, against the
  measured linear Resolve reference, so the visual pass is a confirmation rather
  than a search. This is the agreed division of labour: the human owns the Resolve
  comparison, so minimise the number of rounds it takes.
- Confirm the histogram default (1.67 % threshold) looks right on a real frame, and
  confirm the ±3-doubling range feels usable at both ends — that range is a chosen
  working value, not a measured one, and this is the step where it gets confirmed or
  adjusted.
- Produce the comparison PNGs yourself via the console `HighResShot` route in
  `dev-rules.md` (Game View on, Realtime off, anti-aliasing off, height ≥ 1080).
  Hand the human finished PNGs only for the final sign-off pass.
- Final side-by-side on the D3 composite probe plus a real frame.

## Acceptance criteria

- [ ] Density transfer is **linear × per-scope gain, hard-clipped**, CPU and GPU
      value-identical, all three density scopes — verified in code and test.
- [ ] Per-scope **Trace Brightness** operable from the visible toolbar, plumbed to
      both paths per scope, persists across restart.
- [ ] Histogram uses a **fixed shared scale**, default clip threshold **1.67 % of
      all frame pixels**; frame-peak normalisation is gone; linear, shared single
      scale, hard clip at the band top; both paths; persists.
- [ ] Both sliders follow the **logarithmic law**: ±3 doublings, 1.0× exactly at
      centre, continuous, centre marked and snapping, double-click resets, no
      numeric readout; the **gain multiplier** is what gets persisted.
- [ ] Bin count and threshold reference confirmed in Task 0 (or reported).
- [ ] Tests present and each proven by deliberate breakage; bin-equivalence
      unchanged; suite green on both RHIs, count checked.
- [ ] No steady-state readback; uint atomics only, no wave intrinsics; no new
      warnings.
- [ ] **Visual:** TrueScope at default matches Resolve at default (density and
      histogram) on the same still; CPU vs GPU indistinguishable — MCP screenshots
      plus human review.
- [ ] Human sign-off.

## Explicitly out of scope

Keeping the log transfer as an optional mode (rejected — the gain covers lifting the
low end); the self-adjusting fit-the-peak histogram view (`backlog.md`); making the
GPU path the shipped default (M7); the Resolve-parity candidates (Extents, low-pass
filter, Reference Levels, %/IRE scale, graticule brightness, Hue Vectors / Simplified
graticule, CbCr waveform, Qualifier Focus — `backlog.md`); the capture-RT dump for
true 1:1 (`backlog.md`); logo/icon integration (`backlog.md`). Ideas → `backlog.md`.

## Verification summary

*(fill after sign-off: bin count found, measured default gains per scope, test
counts, the Resolve cross-check result, CPU-vs-GPU deltas, commit hashes.)*
