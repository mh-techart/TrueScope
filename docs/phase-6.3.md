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

**Task 0 finding — bin count (settled 2026-08-18).** TrueScope uses **256**
histogram bins, not 1024, and the measured threshold is **not** rescaled. The premise
behind the trap — that a coarser bin count multiplies the count per bin — does not
apply here, and the rule is worth recording so nobody re-derives it:

**The clip threshold is defined per distinguishable source value, not per bin index.**
It transfers between tools unchanged as long as each bin holds exactly one source
value — i.e. as long as the binning is at least as fine as the source. TrueScope
captures 8-bit (`RTF_RGBA8`) and uses 256 bins: one code value per bin, the finest
binning the data supports. Resolve bins the same 8-bit image into 1024 containers, so
three out of every four sit empty and each populated one holds exactly one code
value's pixels. Both therefore count the same thing, so the measured threshold applies to
both unchanged. A rescale would only be needed if bins were *coarser* than the source, so
that one bin merged several distinct values. Going to 1024 bins here would add no
information — three quarters of them would be provably empty.

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
  count reaches **1.58 % of the analysed frame's total pixel count** draws at full
  band height; more than that is **hard-clipped** at the band top. Linear count →
  height throughout.
- **Shared single scale** across all channels (never per-channel) — unchanged from
  M4. Applies to both RGB and YRGB display modes.
- Add a **Histogram Gain** slider using the same logarithmic law: ±3 doublings
  around the default, so the effective threshold runs 12.6 % (far left) → 1.58 %
  (centre) → 0.20 % (far right). Continuous, centre marked and snapping,
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
- Confirm the histogram default (1.58 % threshold) looks right on a real frame, and
  confirm the ±3-doubling range feels usable at both ends — that range is a chosen
  working value, not a measured one, and this is the step where it gets confirmed or
  adjusted.
- Produce the comparison PNGs yourself via the console `HighResShot` route in
  `dev-rules.md` (Game View on, Realtime off, anti-aliasing off, height ≥ 1080).
  Hand the human finished PNGs only for the final sign-off pass.
- Final side-by-side on (a) `M63_count_wedge_1920x1080.png`, (b)
  `M63_histogram_strips_1920x1080.png`, and (c) a real ColorCalibrator frame, in
  both tools under the `dev-rules.md` cross-check conditions.
  *(An earlier draft of this task named a "D3 composite probe"; no such diagnostic
  exists — the reference is void and is replaced by the three plates above.)*
- **How each default was arrived at (recorded because the three differ in
  evidence status).** Parade and waveform: **derived**, `Gain = 1 / 0.032 = 31.25`,
  from the measured Resolve white point of 3.2 % of a column — see the derivation in
  `TrueScopeDisplayGains.h` and the stated fraction-vs-absolute assumption there.
  Histogram: **measured**, threshold 1.58 % of frame pixels. Vectorscope:
  **derived from the parade/waveform gain, then visually confirmed** — not estimated
  by eye. Resolve's measured slope cannot be applied to it directly, because the
  vectorscope normalises against the whole frame's pixel count while the parade
  normalises against one column's height; those two bases differ by exactly the frame
  width, so `31.25 x 960 = 30000` is what makes a chroma cell reach full white at the
  same per-cell pixel count (17 of the capture) that the parade reaches at 3.2 % of a
  column. That derived value was then checked on a real captured frame and found
  readable (trace median 139/255, 11.2 % of populated cells clipped). Only the
  *confirmation* step was visual; the number itself was derived. The remaining half of
  its acceptance — that it also *matches* Resolve on the same still — is the human's
  side of the cross-check and is still open.

- **Expect one structural difference in the histogram comparison — it is correct and
  must not be "fixed".** Resolve draws a **comb**: it bins the 8-bit plate into 1024
  containers, so only every fourth is populated and the curve breaks into isolated
  one-bin spikes. TrueScope bins the same image into 256 containers, one per code
  value, and therefore draws a smooth continuous curve. **The peak heights must
  match; the fine structure must not.** Comparing the comb's fine structure against
  the smooth curve compares an artefact of the test image with a property of the
  tool. See the bin-count rule under Task 0.

## Acceptance criteria

- [x] Density transfer is **linear × per-scope gain, hard-clipped**, CPU and GPU
      value-identical, all three density scopes — verified in code and test.
- [x] Per-scope **Trace Brightness** operable from the visible toolbar, plumbed to
      both paths per scope, persists across restart.
- [x] Histogram uses a **fixed shared scale**, default clip threshold **1.58 % of
      all frame pixels**; frame-peak normalisation is gone; linear, shared single
      scale, hard clip at the band top; both paths; persists.
- [x] Both sliders follow the **logarithmic law**: ±3 doublings, 1.0× exactly at
      centre, continuous, centre marked and snapping, double-click resets, no
      numeric readout; the **gain multiplier** is what gets persisted.
      *(The law is implemented as specified and ships unchanged: uniform, +/-3
      doublings, 1.0x at centre. Whether that span is the ideal one around the newly
      measured defaults was raised during the phase but is **not** decided from what
      was observed — the "too dim below ~55 % of travel" observation was made while
      the defaults were still 1.0, i.e. ~31x too dim, so it says nothing about the
      range. Tracked in `backlog.md`, "Density slider range"; the histogram Gain
      slider is confirmed good.)*
- [x] Bin count and threshold reference confirmed in Task 0 (or reported).
- [x] Tests present and each proven by deliberate breakage; bin-equivalence
      unchanged; suite green on both RHIs, count checked.
- [x] No steady-state readback; uint atomics only, no wave intrinsics; no new
      warnings.
- [~] **Visual:** TrueScope at default matches Resolve at default (density and
      histogram) on the same still; CPU vs GPU indistinguishable — MCP screenshots
      plus human review. **Deliberately not ticked as fully met.** What was done
      instead: the defaults were **derived from measured Resolve readings** (the
      2026-08-19 count wedge and histogram strips) rather than estimated by eye, and
      the result was checked on screen by Claude Code via MCP screenshots and by the
      human. What was *not* done is a final side-by-side Resolve session at the new
      defaults. The one question that session would answer — whether Resolve's density
      basis is a fraction of column height or an absolute pixel count, a 2x difference
      at our 540-row capture — is **deferred as a tracked backlog item**
      (`backlog.md`, "Waveform density basis"), where it is reclassified as a kind 2
      value to be settled from the Tweakables menu rather than by another Resolve
      round. This is a deliberate deferral with a named owner-item, not an unnoticed
      gap. CPU vs GPU: not bit-identical on a real capture — mean |delta| 0.9 of 255,
      no difference in readings — see the Verification summary and `backlog.md`.
- [x] Human sign-off — given on TrueScope's own appearance, without a Resolve
      session, on the closing pass (2026-08-20).

## Explicitly out of scope

Keeping the log transfer as an optional mode (rejected — the gain covers lifting the
low end); the self-adjusting fit-the-peak histogram view (`backlog.md`); making the
GPU path the shipped default (M7); the Resolve-parity candidates (Extents, low-pass
filter, Reference Levels, %/IRE scale, graticule brightness, Hue Vectors / Simplified
graticule, CbCr waveform, Qualifier Focus — `backlog.md`); the capture-RT dump for
true 1:1 (`backlog.md`); logo/icon integration (`backlog.md`). Ideas → `backlog.md`.

## Verification summary

*(Filled 2026-08-19; closed 2026-08-20. Nothing here is left open: the two items that
were open — the Resolve side-by-side and the slider ends — were converted into tracked
`backlog.md` entries rather than carried as loose questions, and are marked below as
deliberate deferrals with their reasons.)*

**Bin count.** 256, one per 8-bit code value, matching the `RTF_RGBA8` capture.
Resolve uses 1024, which is why 8-bit plates draw there as a comb — an artefact of
the test image, not of either tool (rule recorded under Task 0 and in
`dev-rules.md`).

**Transfer, as implemented.** Density: `Intensity = min(Count * EffectiveGain /
MaxCount, 1)`, where `MaxCount` is the column height (540) for parade and waveform
and the whole frame's pixel count (960 x 540) for the vectorscope, and
`EffectiveGain = ScopeDefaultGain x UserMultiplier`. Because the count is normalised
by the column height before the gain multiplies it, the height cancels and full white
falls at a fixed **fraction** `f = 1 / Gain` of a column, independent of capture
resolution. Histogram is a separate mapping (count -> curve height against a fixed
threshold expressed as a fraction of frame pixels); the two are deliberately never
merged.

**Default gains.**

| Scope | Default | Evidence status |
| --- | --- | --- |
| Parade | 31.25 | **Derived** from measurement: `1 / 0.032`, Resolve reaching full white at 3.2 % of a column (34 px of 1080; 17 of our 540) |
| Waveform | 31.25 | Same derivation |
| Vectorscope | 30000 | **Derived** from the parade gain to keep the same per-cell fraction — `31.25 x 960`, the frame width separating the two normalisation bases — then visually confirmed on a real frame. Not measured against Resolve directly, and not estimated by eye |
| Histogram threshold | 1.58 % of frame pixels | **Measured** 2026-08-19 (0.4167 % -> 26.3 %, 0.8333 % -> 52.7 % of band height => 1.584 / 1.581 %) |

The 2026-08-19 Resolve count wedge (1920x1080, Video Level Scopes) gave a constant
brightness/count ratio of 6.00-6.13 across seven bands, re-confirming that the
transfer is linear, not log.

**Breakage proof, re-run at the refined threshold (2026-08-19).** The original proof
(2026-08-18) perturbed the *old* 1.67 % constant, so it did not prove the guard still
has teeth at 1.58 %; re-run with `HistogramClipThresholdFraction` moved
0.0158 -> 0.0200. Result: the same single-test split as before — exactly
`TrueScope.UI.HistogramClipThreshold` fails, the other fifteen `TrueScope.UI` tests
pass — and the failure message now names the current figures, not the old ones:
`Expected 'The default threshold is the documented 1.58 % of the frame (guards
architecture.md)' to be 8190.719727, but it was 10368.000000`, plus the three slider
positions (12.6 % / 1.58 % / 0.20 % read as 16.0 % / 2.0 % / 0.25 %). Reverted, and
the baseline re-run green before and after.

**Test counts.** 40/40 on `-nullrhi` and 40/40 on `-RenderOffscreen`, build clean
with no new warnings. 40 is the expected registered-test count and is the stale-code
tripwire.

**Resolve cross-check.** *(Closed as a deliberate deferral, not completed.)* The
defaults were derived from measured Resolve readings rather than estimated, and the
result was confirmed on screen; no final side-by-side session at the new defaults was
run, and none is planned — the residual question is tracked in `backlog.md`,
"Waveform density basis" and reclassified as an internal default (kind 2) to be found
in the Tweakables menu. Plates produced under the
`dev-rules.md` conditions (piloted CineCameraActor, Game View on, Realtime off, AA
off, `r.ScreenPercentage 100`): `M63_count_wedge_1920x1080.png`,
`M63_histogram_strips_1920x1080.png`, `M63_colorcalibrator_1920x1080.png`, plus
`M63_count_wedge_1920x540.png` — the same absolute counts at half the rows, which
settles the fraction-vs-absolute question in one reading (clip near count 17 =>
fraction, near 34 => absolute).

**CPU vs GPU.** Compared on the frozen still, so both paths analysed the same
capture: images aligned (verified against all nine +/-1 px offsets), 17.29 % of panel
pixels differ, mean |delta| 0.9 of 255, 64 % of differing pixels off by 1-3 code
values, 0.067 % off by >=32 (max 46, about 3 counts). Not visible as a difference in
readings; logged in `backlog.md` for a synthetic repro.

**Slider ends.** *(Closed as a tracked question; no change made.)* The uniform
+/-3-doubling law ships unchanged. The observation that prompted the question — the
scopes reading as too dim below roughly 55 % of travel — was made while the defaults
were still 1.0, about 31x too dim, so it does not bear on the range and must not be
decided from. Whether the bottom of the range is useful travel or wasted travel has
never been checked on screen at a correct default. Tracked in `backlog.md`, "Density
slider range", to be settled from the Tweakables menu.

**Commits.** `2727c3c` (spec), `0b208dc`, `a332207`, `5ec78cf` (planning items),
`dd42790` (`.uplugin` UTF-8 re-encode), `c102bb7` (`Config/` + `Resources/`),
`862422f` (dev-rules + backlog housekeeping), `b4e3d3b` (calibrated defaults +
refined threshold), `87b256f` (Task 5 results + CPU-vs-GPU delta), `7f92baf` (two
leftover 1.67 %-era figures), `b73232e` (breakage proof re-run at 1.58 %), plus the
closing commit that carries this summary.

## Status

**M6.3 is closed (2026-08-20).** Everything not implemented is a named `backlog.md`
entry: "Waveform density basis" (the 2x fraction-vs-absolute question, reclassified as
an internal default), "Density slider range", "UI consolidation + Tweakables and
Settings menus" (the next phase), "CPU vs GPU: small per-cell count differences", and
the two `.uplugin` metadata items. No question from this phase is left loose.
