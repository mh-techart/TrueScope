# Phase 6.2 — Scope features: vectorscope graticule, histogram YRGB, scale ticks

Read `architecture.md` (Settled decisions → Color math, Per-scope display modes;
the M6 row), `backlog.md` (Vectorscope graticule extensions; Histogram YRGB;
parade/histogram scale ticks), and `phase-6.1.md` (the toolbar, the status line and
— crucially — the device-resolution raster this phase draws onto).

M6 is split. M6.1 delivered the foundation (raster at display resolution, toolbar,
settings, status line, hints). **This is M6.2 — scope features**: everything that
is *built and verified* against a known reference. **Appearance calibration is
M6.3** (density-to-brightness curve; histogram gain Resolve-style vs. Exact) and is
deliberately not here: `backlog.md` requires "measure Resolve" *before* fixing any
calibration detail, and that reference measurement does not exist yet. Calibrating
before the reference exists, or before all scope elements are drawn, means doing it
twice.

All of this is **drawing only** — no analysis change, no new binning. The luma data
YRGB needs already exists (phase-5.2 computes integer-exact BT.709 luma). Both
paths (CPU and GPU) must show the same result; the equivalence tests stay green
untouched. CPU stays default; GPU-default flip is M7.

If a decision-level surprise appears, stop and ask (Rule 3 self-check). Where a
control is only reachable via a Slate right-click menu that MCP cannot open, pause
and ask the human to click (approved).

## Task 1 — Vectorscope graticule extensions

The M4 graticule ships the six 75 % target boxes and a plain outer circle. Add the
rest, all display-only, all behind a **"Show Guides"** toggle exposed in the M6.1
toolbar (not only the right-click menu).

**Angular reference frame — settle once, reuse everywhere.** All angles are in the
**BT.709 Cb/Cr plane**: Cb on x (positive right), Cr on y (positive up), measured
counter-clockwise from +Cb. This is the frame the vectorscope already plots in and
the frame Resolve uses — Resolve applies **no** NTSC/YIQ rotation. **Compute** these
angles; the table is a self-check, not a source of constants:

| Target | Angle |
|---|---|
| Magenta | 49.7° |
| Red | 102.9° |
| *(skin tone line)* | *123.0°* |
| Yellow | 174.8° |
| Green | 229.7° |
| Cyan | 282.9° |
| Blue | 354.8° |

*(Measured against a Resolve screenshot in the planning chat: least-squares circle
fit, then the six target boxes read 48.6 / 102.4 / 175.1 / 230.3 / 283.3 / 354.3° —
offsets are the letter labels pulling the centroid. This confirmed both the frame
and that Resolve does not rotate.)*

### 1a — Skin tone line

A line from the desaturated centre to the fully saturated rim at **123.0°**, between
red and yellow, closer to red; neutral white, full radius (as Resolve draws it).

A plausibility guide: human skin of every ethnicity clusters at this hue angle
(melanin/haemoglobin physics), which matters here because MetaHumans put photoreal
skin into Unreal viewports.

**This angle is a convention, not a standard.** Blackmagic publishes no number;
Resolve's docs describe the indicator only qualitatively. 123.0° is the historical
NTSC **I axis** expressed in the Cb/Cr plane, and it is what SMPTE flesh-tone
practice, Resolve, FCP, Premiere and Media Composer converge on. **Three independent
confirmations, recorded so nobody "corrects" the constant later:** (1) the I-axis
derivation; (2) a representative skin colour (sRGB 0.80/0.60/0.50) computes to
124.9°, i.e. on the line; (3) direct measurement of Resolve's own white line —
**123.08°**, σ = 0.09° over 1546 pixels.

### 1b — Hue ring on the rim

Replace the plain outer circle with a coloured ring showing the fully saturated hue
at each angle, so a user reads angle → hue at a glance.

- The ring's **inner edge sits exactly on today's circle radius**. It grows
  **outward only** and must never intrude into the plotting area — that circle is
  the 100 % saturation reference and the reading of the scope must not change.
- Suggested width ≈ half the height of a 75 % target box; scale as a fraction of the
  radius with a pixel min/max; final value by eye.
- **Construction — this is the whole point:** the ring is **not** an HSV or RGB
  colour wheel. Sweep fully saturated RGB hues, convert each to Cb/Cr, and place its
  colour at `atan2(Cr, Cb)`. The angular spacing then comes out non-uniform by
  itself (red→yellow spans 72°, yellow→green 55°), it matches Resolve/FCP because
  they do the same, and the ring is automatically in register with the six targets —
  it *cannot* be mis-rotated, because it is generated from the same formula. Do not
  hand-place hues around a circle.
- A fully saturated ring is visually loud next to the density trace on the dark
  editor theme. Keep it narrow and somewhat muted.
- **Resolve has no hue ring** — this is a TrueScope addition, not a reproduction.
  Nothing to copy; make it fit the editor's theme.

### 1c — 5° tick marks

Every 5° (72 ticks), alternating long/short so they are countable.

- **Measured from Resolve:** ticks are drawn **inward** from the circle — long
  ≈ 2.7 % of the radius, short ≈ 1.5 %, alternating (every second tick long).
- This settles the layout: **ticks inward, hue ring outward** — they do not compete.
  (Resolve's ticks do enter the plotting area slightly; that is fine and does not
  disturb reading. The hue ring is a different matter and must stay outside.)
- **Expect the ticks not to line up with the targets.** 102.9° and 49.7° are not
  multiples of 5. This is correct and will look like a bug to a future reader —
  **add a comment saying so. Do not rotate the ticks to "fix" it.**

### 1d — 100 % targets and 2× zoom

- **100 % targets** in addition to the 75 % boxes.
  **Caution, measured:** the six targets do **not** share one radius. In the Resolve
  screenshot, magenta and green sit at 0.745 R, red and cyan at ≈ 0.64 R, yellow and
  blue at ≈ 0.63 R. The outer circle is a fixed chroma radius, not a per-hue gamut
  boundary, so each hue reaches it at a different saturation. **Placing the 100 %
  targets on a common radius would be wrong.** Derive each target's radius from its
  chroma magnitude, exactly as M4 already does for the 75 % boxes.
- **2× zoom** as a display mode, for reading low-saturation material. Scales the
  plotted density (and the graticule with it); the aspect lock and circularity are
  untouched.

### Task 1 acceptance
- [x] Skin tone line at 123.0°, centre to rim, neutral white; the derivation +
      three confirmations recorded next to the constant.
- [x] Hue ring: inner edge on the existing circle radius, grows outward only, never
      intrudes into the plotting area — **verify geometrically, measure it**; hues
      generated from the Cb/Cr formula (not hand-placed) and therefore in register
      with the six targets.
- [x] 5° ticks: 72, alternating, inward; the non-alignment with targets is correct
      and commented.
- [x] 100 % targets each on their own chroma-derived radius (not a shared one).
- [x] 2× zoom works; circle stays circular and aspect-locked at extreme panel
      shapes (no regression).
- [x] All of the above behind "Show Guides" in the toolbar; SMPTE-colorbars test
      still lands the density in the six targets (graticule changes must not touch
      the density). *(Superseded by the work-order-15 amendment: ring + ticks are
      permanent, the skin line has its own "Show Skin Tone Line" toggle, and the
      markers are a 75 %/100 %/Both selector — see the Amendments section.)*

## Task 2 — Histogram YRGB (4-channel) display mode

Add a YRGB histogram mode showing four channels — R, G, B **and Luma (Y)** — as
Resolve does.

- Display modes become: **"Separate Channels RGB"**, **"Separate Channels YRGB"**,
  **"RGB Overlay"**.
- **Default stays RGB, not YRGB.** The human considered YRGB-as-default (Resolve
  does it) but a four-way split reduces per-curve height and gets cramped on small
  panels; RGB remains the default and YRGB is opt-in. Record the reasoning.
- The luma data already exists: phase-5.2 computes **integer-exact BT.709 luma**
  for the waveform. Reuse it — do **not** add a second luma computation, and do not
  reintroduce float binning.
- Same shared-scale rule as today: the curves share **one** scale derived from the
  strongest channel, never per-channel. (Whether the luma curve participates in
  that shared maximum, or is scaled alongside it, is an implementation detail —
  pick the one that matches Resolve's YRGB reference screenshot and record it.)
- Mapping stays `count → curve HEIGHT` (linear), **not** the log density mapping.
- Must work identically on both CPU and GPU paths, via the same producers.

### Task 2 acceptance
- [x] YRGB mode selectable from the toolbar; shows four channels; RGB stays default.
- [x] The luma curve derives from the existing integer-exact luma bins (verify: its
      bins match the waveform's luma bins for the same frame).
- [x] Identical on CPU and GPU paths — MCP screenshots both.
- [x] Shared-scale rule preserved; no flat-topping (still count→height, linear).

## Task 3 — Scale intermediate ticks (parade + histogram)

Resolve's parade and histogram scales show **three short intermediate ticks** between
the labelled steps (0, 128, 256, 512, …), which aids reading values off the scale.
Adopt the same on both scopes.

- Three minor ticks between each labelled major.
- **Histogram: keep our scale at the bottom edge — do not move it to the top.**
- Fixed-size UI elements, as the labels already are (legible at any panel size).

### Task 3 acceptance
- [x] Parade and histogram scales show three minor ticks between majors; histogram
      scale still at the bottom edge; legible at small and large panel sizes.

## Overall acceptance
- [x] Builds clean, no new warnings.
- [x] Tasks 1–3 acceptance met.
- [x] Full automation suite green (CPU headless + GPU-RHI), count checked; the
      bin-equivalence tests unchanged and still green (this phase draws, it does not
      bin).
- [x] No new TrueScope warnings/errors in the Output Log.
- [x] Human sign-off: vectorscope graticule vs. Resolve (skin line angle, ring
      register, ticks), YRGB mode, scale ticks — visual check on a real viewport.

## Explicitly out of scope

**Appearance calibration is M6.3**, not here: the density-to-brightness curve tuning
across all four scopes, and the histogram gain (Resolve-style default vs. Exact).
Both require the "measure Resolve" reference the backlog mandates — fixed vs.
adaptive gain, linear vs. sqrt-ish plot, tested with known bin ratios — which is
produced in the planning chat first (the same way the skin-tone angle was settled by
measurement rather than guesswork). Calibrating before that reference exists, or
before this phase's graticule/YRGB elements are drawn, means doing it twice.

Also out: making the GPU path the shipped default (M7); HDR / non-Rec.709 scale and
graticule behaviour (backlog); the ΔE2000 perceptual harness (backlog); the
testpattern generator (backlog); window layout options (post-M6). Ideas →
`docs/backlog.md`.

## Amendments from the visual review (work order 15, 2026-07-18)

Three Task-1 details were corrected after the human's Resolve comparison; where
they contradict the task text above, this section wins:

1. **Radial calibration** *(corrected again by work order 16 — this is the
   final form)*: the graticule circle is the **derived maximum 100 % chroma
   magnitude** — magenta/green, 0.5957 — computed from the BT.709 matrix in
   code (`TrueScope::GetVectorscopeCircleChroma`), never a literal. No 100 %
   marker can exceed the rim by construction; magenta/green 100 % sit exactly
   ON the circle (correct, matches Resolve). Derived radii: 100 % at
   1.0000/0.8612/0.8429 R, 75 % at 0.7500/0.6459/0.6322 R (Mg·G / R·Cy / Yl·B)
   — the 75 % values match this spec's measured Resolve radii (0.745/0.64/
   0.63 R), which had been mislabelled as the 100 % set; that misreading
   produced work order 15's interim 0.8 guess, and the original circle-at-0.5
   (the per-axis Cb/Cr bound) had pushed Mg/G outside the rim. Density and
   both marker sets share the one scale, so the trace stays in register with
   the boxes; zoom scales everything together. Full derivation and pitfalls:
   `architecture.md`, "Color standard reference".
2. **Markers option.** Target boxes are selectable in the toolbar:
   **75 % / 100 % / Both**, default **75 %** (SDR broadcast convention; 100 %
   serves HDR/cinema/streaming targets). Letters go on the outermost displayed
   set.
3. **"Show Guides" split.** The hue ring and the 5° ticks are **always
   visible** (permanent orientation, like Resolve); only the skin tone line is
   toggled, via **"Show Skin Tone Line"** (default on). The Task-1 acceptance
   line "all of the above behind Show Guides" is superseded accordingly.

Deferred from the same review → `backlog.md`: **Vectorscope 2× zoom orientation
aids** (markers/ring currently scale out of view when zoomed; Resolve keeps
them fixed).

## Verification record — phase complete (human sign-off 2026-07-18)

Tasks 1–3 landed 2026-07-17 (work order 14); the vectorscope corrections from
the visual review landed 2026-07-18 across work orders 15 and 16. Editor access
for live verification ran over the direct-HTTP MCP fallback (harness tools
unavailable those sessions; permitted path, stated per the dev-rules).

- **Task 1 (vectorscope graticule):**
  - *Skin tone line* at the conventional 123.0° (`SkinToneLineAngleDegrees`,
    derivation + three confirmations recorded at the constant); measured on the
    live screenshot at 122.8°. Behind its own **"Show Skin Tone Line"** toggle
    (work order 15); with it off the ring and all 72 ticks remain (verified).
  - *Hue ring* generated from the Cb/Cr formula (`BuildHueRingStops`), so it is
    in register with the six targets by construction — the graticule test
    confirms each target angle carries its own hue, and the red→yellow 72° /
    yellow→green 55° non-uniform spacing (a hand-placed HSV wheel fails the
    test). Inner edge measured +0.6 px outside the circle radius: outward only,
    no intrusion.
  - *5° ticks*: 72, alternating long/short (measured ≈5.8 px vs the 2.7 % of
    radius spec), inward; the deliberate non-alignment with the targets is
    commented in code.
  - *Marker radii — the calibration that took two revisions.* The graticule
    circle is the **derived maximum 100 % chroma** (magenta/green, 0.5957),
    computed from the BT.709 matrix (`GetVectorscopeCircleChroma`), never a
    literal. Derived r/R pinned by `TrueScope.UI.VectorscopeGraticule`; magenta
    and green 100 % sit exactly ON the circle, all others inside. On-screen
    measurement of all 12 boxes agreed with the derived table to 0.022 R
    (measurement-limited by the box outline width and the letter labels; the
    exact placement is pinned analytically to ±0.0005 R). Teeth proven: hard-
    coding the circle to the earlier 0.8 or 0.5 guesses each fails the test.
    Full derivation + pitfalls in `architecture.md`, "Color standard reference".
  - *2× zoom* scales the density, markers and ring by one shared factor around
    the neutral centre; everything outside the content square clips, the aspect
    lock is untouched (verified: 75 % boxes at doubled radius, 100 % + ring
    correctly clipped, zero stray saturation).
  - *Markers option* 75 % / 100 % / Both (default 75 %, work order 15), operated
    live from the toolbar.
- **Task 2 (histogram YRGB):** four stacked bands (Luma on top, then R, G, B);
  the luma curve is the column sum of the waveform's integer-exact luma bins on
  both paths — `TrueScope.GPU.HistogramLumaBinEquivalence` shows the GPU luma
  histogram equals the CPU one bit-exactly. RGB stays the default. Luma
  participates in the YRGB shared maximum (recorded decision). Verified live on
  CPU and GPU paths.
- **Task 3 (scale ticks):** three minor ticks between the labelled majors on the
  parade (and waveform, via the shared axis drawer) and the histogram, whose
  scale stays at the bottom edge; probe-verified 24/24 and 12/12 expected
  positions. Test `TrueScope.UI.ScaleMinorTicks` (incl. the uneven 896–1023
  division).
- **Suite:** 36/36 green on `-nullrhi` and on the real Metal RHI
  (`-RenderOffscreen`), count sanity-checked each run; the phase-5.2
  bin-equivalence tests unchanged and green; zero new warnings; no TrueScope
  warnings/errors in the live Output Log.
- **New docs:** `architecture.md` gained the top-level **"Color standard
  reference"** (BT.709-6, sRGB == Rec.709 for scopes, the pipeline schema, the
  five pitfalls, the derived radius table) as the source of truth for scope
  colour math (work order 16).
- **Human sign-off:** confirmed by the human on 2026-07-18.

Next: **M6.3** — appearance calibration (density-to-brightness curve, histogram
gain), which stays deferred until the "measure Resolve" reference exists (see
"Explicitly out of scope" above).
