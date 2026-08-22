# TrueScope — Architecture & Decisions

**TrueScope — Viewport Scopes for Unreal Engine.**
Waveform, RGB Parade, Vectorscope and Histogram in a dockable Unreal Editor
window, so exposure and color can be judged inside the engine instead of after
rendering. Reference look & layout: the Scopes window of DaVinci Resolve.

This document records *settled decisions with rationale*. Phase specs
(`phase-<n>.md`) define the current work package; future ideas live in
`backlog.md`. Claude Code: do not re-litigate these decisions; if an
instruction conflicts with them, flag it.

## Product constraints (from project goals)

- Editor-only tool. Must never ship with a packaged game (guaranteed by using
  an Editor-type module, which packaging excludes).
- Must work with the prebuilt Epic Launcher engine. No engine source changes,
  no custom engine builds — neither for developers nor for users.
- Cross-platform: Windows, macOS, Linux.
- **Zero level footprint:** no actors, components or volumes are placed in
  the user's level. Teams sharing a level must be unaffected by whether a
  member has TrueScope installed.
- **The end user is an artist, not an engine programmer.** Every user-facing
  capability must be discoverable and operable from the TrueScope UI. Console
  variables are a diagnostic and developer channel, never the only way to
  reach a feature. Defaults must produce a correct, pleasant result without
  configuration.
- Measures **exactly one viewport at a time**: the last focused Level Editor
  viewport. There is no viewport picker and the user never chooses — this is
  intentional. Cinematic Viewport and PIE running *inside* a level viewport
  are covered, since these are level editor viewports. Exact camera
  measurement is achieved by piloting a Cine Camera or using the Cinematic
  Viewport.
- **Standalone Game is out of scope by construction:** it is a separate
  process without the editor module, so TrueScope does not load there.
- **Behaviour during PIE (understood and intended).** The selection logic
  measures the last valid **non-PIE** level-editor viewport and never adopts a
  PIE viewport as a source. Focusing a PIE viewport while another valid viewport
  exists changes nothing — the existing source keeps updating. The scopes hold
  their last frame in exactly one case: the viewport **currently being measured**
  itself enters PIE and thus drops out as a source. All PIE variants
  (in-viewport, undocked window, "New Editor Window") behave identically;
  separate-window PIE captures mouse/keyboard input (Shift+F1 releases it — an
  Unreal behaviour, not TrueScope's). **Simulate** (rather than Play) drives
  animated/runtime objects while the scopes keep measuring normally.
- **No automatic fallback (deliberate).** When the measured source falls away,
  TrueScope does **not** auto-switch to another open viewport. Another viewport
  may be in a scope-unsuitable view mode (Unlit, Wireframe, Lighting-Only), so a
  silent switch could measure a meaningless image; and WYSIWYG requires TrueScope
  never measure a source the user did not consciously focus. The held last frame
  is intended and useful (the lighting baseline stays visible during PIE). Phase
  4.1 adds only a visible paused indicator over the held frame, telling the user
  it is paused, that the measured viewport is in PIE, and that focusing another
  viewport resumes measurement — leaving the choice to the user.
- Refresh rate ~2–3 Hz by default (configurable); resource-friendly.
- Smooth, anti-aliased rendering — no blocky/pixelated scopes.
- Distribution: GitHub (this repo, source + prebuilt binaries per release);
  Fab submission is a later option.

## Core measurement principle: WYSIWYG (display-referred)

TrueScope taps the render pipeline **as late as possible** — after tonemapping
and post-processing — so the scopes analyze the same signal the monitor
receives. Consequences, by design:

- Toggling the tonemapper, changing exposure, show flags or color grading
  changes the viewport image **and** the scopes identically. There is no
  hidden measurement point that disagrees with the eye.
- The project's **Working Color Space** (sRGB/Rec709, ACEScg, Rec2020, …) is
  upstream of the measurement point and therefore cannot break TrueScope:
  users configure it per Epic's documentation, the scopes always analyze the
  final display-encoded viewport image.
- **OCIO viewport/PIE transforms are part of the measured signal.** UE applies
  them via its own post-tonemap scene view extension — TrueScope's sampling
  pass must run *after* the OCIO pass (pass selection / extension priority),
  otherwise scopes silently diverge from the viewport. Verified by an M2
  acceptance test.
- **The hook sits after Tonemap and after OCIO, but before FXAA.** TrueScope
  subscribes to the after-Tonemap callback at extension priority 0 — below the
  OCIO display extension's 100, so it samples the post-OCIO image (above). FXAA,
  however, runs *later* in the chain than this hook, so the scopes measure the
  **pre-FXAA** image. Practical consequence for the Resolve cross-check: use
  **AA off** (`r.AntiAliasingMethod 0`), never FXAA — otherwise the reference
  PNG (post-FXAA) and the scope capture (pre-FXAA) differ at edges. TAA/TSR are
  excluded outright (sub-pixel jitter frame-to-frame).
- **Capture exists only while the viewport's `PostProcessing` show flag is on**
  (verified in 5.8 source + live diagnostic, work order 13 / 2026-07-17). With
  the flag off the renderer takes its *minimal* post-processing path, and that
  branch never collects scene-view-extension pass callbacks at all (the
  subscribe loops in `PostProcessing.cpp` sit inside the
  `bPostProcessingEnabled` branch only) — the hook is not skipped, it is never
  registered. Consequences: (a) orthographic views and view modes that
  `ApplyViewMode` maps to `PostProcessing=0` (Wireframe, Unlit, most debug
  modes) are structurally uncapturable through this hook — measuring them would
  need a different mechanism, not a different pass choice; (b) the engine has a
  gap where switching an ortho viewport straight to a placed camera sets only
  the actor lock and never re-runs `ApplyViewMode`, leaving the flags in their
  ortho state while the view renders perspective — the scopes froze there.
  TrueScope repairs exactly that stale state (`IsStaleOrthoShowFlagState` +
  re-running the engine's own `ApplyViewMode`, the same call the
  ortho→Perspective menu path makes; human-approved 2026-07-17). A deliberate
  Show > Post Processing uncheck in a perspective viewport is recognizably
  different (its stored viewport type is `Perspective`) and is never
  overridden.
- Values are display-referred (0–1 → scope scale 0–1023). With the tonemapper
  disabled, scenes clip exactly as the viewport shows them clipping.
- **Calibration note:** percentages and vectorscope graticule targets are
  calibrated for Rec.709/sRGB-encoded output. With OCIO view transforms
  targeting other displays (P3, PQ/HDR), TrueScope still measures the actual
  display signal (WYSIWYG), but its colorimetric interpretation is only
  guaranteed for the SDR Rec.709 case. Scene-referred / HDR / OCIO-aware
  modes are explicitly deferred (see backlog) and documented as a V1
  limitation. **End goal (backlog):** the scopes must eventually handle every
  selectable UE Working Color Space (sRGB/Rec709, Rec2020, ACES AP0,
  ACES AP1/ACEScg, P3DCI, P3D65), since these affect the viewport image; scale
  and graticule calibration become color-space-aware there. Note that color
  space and bit depth are independent concerns. Related user-facing note for
  the README: Movie Render Queue
  exports with their own OCIO output transform are a different signal than
  the viewport — TrueScope measures the viewport.

## Color standard reference

The source of truth for all scope color math (work order 16) — for developers
and agents (the pitfalls below already bit this project) and for artists and
studios (a documented standard builds trust; echo it in README/tutorials).
The vectorscope draws a mathematical standard, not a matter of taste: every
constant here is derived from the standard, never tuned by eye.

### Standards

- TrueScope's Rec.709 math follows **ITU-R BT.709-6** — matrix from items
  3.2/3.3: https://www.itu.int/rec/R-REC-BT.709-6-201506-I
- **sRGB == Rec.709 for scope purposes.** sRGB shares BT.709 primaries and
  white point; its Y'CbCr (sYCC) uses the same BT.709 matrix. sRGB differs
  only in the transfer function (gamma), which shifts perceived brightness,
  NOT chroma angle/radius. So sRGB and Rec.709 produce identical vectorscope
  geometry, and Unreal's combined "sRGB/Rec.709" project option is exact for
  scope purposes.
- Future: Rec.2020 / HDR (PQ, HLG) will need their own matrices and target
  radii — a separate phase (see backlog, incl. the BT.2111 colorbar
  verification entry). Each color space TrueScope supports must document which
  standard it follows and how it is implemented.

### Pipeline

```
RGB (Rec.709 primaries)
  ↓  [transfer function: scopes work on gamma-encoded R'G'B' — display-referred.
  ↓   TrueScope samples AFTER the tonemapper, so the image is already display-referred.]
R'G'B' (gamma-encoded)
  ↓  ITU-R BT.709 matrix (Kr=0.2126, Kg=0.7152, Kb=0.0722; denom 1.8556 / 1.5748)
Y'CbCr
  ↓  take Cb, Cr only (Y' = luma, not plotted on the vectorscope)
(Cb, Cr)  — Cartesian; THIS is the plotted plane
  ↓  screen mapping ONLY: Cb→x, Cr→y (flip y-up→y-down); axis orientation so
  ↓  red is upper, blue right (broadcast convention)
  ↓  ⚠ NO NTSC/YIQ rotation — Rec.709 scopes plot Cb/Cr un-rotated
pixel position
  ↓  polar reading is DERIVED, not an extra transform:
  ↓  hue = atan2(Cr, Cb), saturation = sqrt(Cb² + Cr²)
markers at standard color-bar (Cb,Cr) positions; circle radius = max 100% chroma
```

### Common pitfalls (each of these already bit this project)

1. **No NTSC/YIQ rotation.** Old NTSC vectorscopes rotated to the I/Q axes
   (skin line at 123° via rotation). Modern Rec.709 Cb/Cr scopes (Resolve,
   FCP) do NOT rotate. Adding a rotation shifts every marker. Verified:
   measured Resolve targets match un-rotated Cb/Cr (magenta 49.7°, not the
   NTSC 61°).
2. **Cartesian compute vs. polar display.** The scope computes in Cartesian
   Cb/Cr and only *reads* as polar (angle = hue, radius = saturation). Don't
   conflate them.
3. **Gamma-encoded vs. linear.** Scopes operate on gamma-encoded R'G'B'
   (display-referred), not linear RGB. Feeding linear RGB shifts all results.
4. **Circle radius = max 100 % chroma, not an abstract value.** It equals the
   magenta/green 100 % magnitude (0.5957) and is computed from the matrix in
   code (`TrueScope::GetVectorscopeCircleChroma`), never a literal. Guessed
   values (0.5, 0.8) put markers outside or misplace them — this is exactly
   what went wrong before (work orders 15/16).
5. **Skin tone line 123.0° is a convention, not a standard** (see the
   vectorscope graticule notes: the NTSC I-axis expressed in Cb/Cr; confirmed
   three independent ways, recorded at the constant).

### Marker radii (derived)

Normalized radii r/R with R = circle radius = max 100 % chroma (0.5957).
Magenta/green 100 % sit exactly ON the circle — correct and matching Resolve;
do not "pull them inward". Pinned by `TrueScope.UI.VectorscopeGraticule`.

| Target  | angle° | 100 % r/R | 75 % r/R |
|---------|--------|-----------|----------|
| Magenta | 49.7   | 1.0000    | 0.7500   |
| Red     | 102.9  | 0.8612    | 0.6459   |
| Yellow  | 174.8  | 0.8429    | 0.6322   |
| Green   | 229.7  | 1.0000    | 0.7500   |
| Cyan    | 282.9  | 0.8612    | 0.6459   |
| Blue    | 354.8  | 0.8429    | 0.6322   |

## Settled decisions

| Topic | Decision | Rationale |
|---|---|---|
| License | **Apache-2.0** | Permissive like MIT, plus explicit patent grant and contribution terms — better default for a community project. Fab-compatible. |
| Engine version | **UE 5.8** (Launcher build) | Current version; best MCP/ModelContextProtocol support for AI-assisted development. Port to ±1 version only after M6. |
| Primary dev platform | macOS (Metal); Windows/Linux verified later (M7) | Developer's machine. Metal quirks surface early as a side benefit. |
| UI framework | **Slate**, dockable **Nomad Tab** (registered via `FGlobalTabmanager`), menu entry under *Window → TrueScope* | Native editor integration, docking + layout persistence for free, anti-aliased line/polygon drawing solves the "no pixelated look" requirement. No content assets needed → clean code-only plugin. |
| UMG / Editor Utility Widgets | Rejected for the scopes UI | Would still need a C++ backend, and lacks crisp vector drawing; a Slate tab already docks next to any Editor Utility Widget. |
| Common UI plugin | Not used | Runtime/game UI framework (input routing, gamepad); irrelevant for editor tools. |
| Image source | **Active Viewport via `FSceneViewExtensionBase`**, subscribed after the tonemap pass; the post-tonemap image is copied/downsampled into a plugin render target. Single source in V1. | Zero level footprint; WYSIWYG for the last focused Level Editor viewport, which covers the Cinematic Viewport and PIE running inside a level viewport (Standalone Game out of scope, PIE source behaviour understood and intended — see the product constraints, "Behaviour during PIE"); camera measurement via piloting. Engine reference for the pattern: the OpenColorIO plugin (post-tonemap view-extension pass) and other in-engine `SceneViewExtension` users. |
| Cine Camera via SceneCapture | **Deferred to backlog** (optional convenience: measure a camera without piloting it) | Original MVP path; superseded as primary source by the decision above. Would reintroduce a scene-side component — acceptable only as an opt-in extra, never a requirement. |
| Viewport overlays | Not in the scopes window scope of V1, **but planned (M8)**: Zebra, over/under-exposure warning (red/blue), False Color — implemented as a *writing* pass on the same SceneViewExtension, per-viewport toggle from the TrueScope tab | Reuses M2 (view extension) + M5 (plugin shader) infrastructure; thresholds are display-referred % and therefore consistent with the waveform. No PostProcessVolume, no material asset, no level changes. |
| Analysis, stage 1 (MVP) | **CPU**: async GPU readback (`FRHIGPUTextureReadback`) of the downsampled capture, C++ histogram building in a background task | No shaders → fully debuggable and unit-testable in text; at 2–3 Hz and ~0.5 MPix per update the cost is negligible. Becomes the ground truth for the GPU path. |
| Analysis, stage 2 | **Compute shaders** (plugin `.usf`, virtual shader dir via `AddShaderSourceDirectoryMapping`, module loading phase `PostConfigInit`), RDG passes, `InterlockedAdd` binning, second pass maps counts → intensity texture | Full-resolution precision, Resolve-grade density look, headroom for higher refresh rates. Must produce results matching the CPU path on test images. |
| Color math | **BT.709** R'G'B' → Y'CbCr for vectorscope and luma waveform; measurement point = display-referred output (sRGB/Rec.709 case) | Matches what Resolve shows for SDR delivery. OCIO / HDR / working-color-space variants deferred (backlog). |
| Scales | Waveform/Parade/Histogram: normalized **10-bit 0–1023** — a convention, not a claim about source bit depth (Resolve shows it for 8-bit material too; our A2B10G10R10 capture is already 10-bit-capable). Parade/Waveform gridlines every 128 (8 divisions) for finer reading. Vectorscope: standard graticule with 75 % targets (R/Mg/B/Cy/G/Yl) | Colorists are used to 0–1023, and one shared value axis across all scopes beats mixed units. Histogram moved from 0–100 % to 0–1023 in M4 for this reason. Scale behavior for HDR / non-Rec.709 color spaces is deferred (backlog). **Why the scale is 0–1023 regardless of source bit depth** (confirmed against Resolve's documentation and behaviour, 2026-08-18): a scope scale is a *reading convention*, not a statement about the source file. Resolve processes internally in 32-bit floating point, and its scopes read that processed signal, then display it on a normalised 10-bit code-value scale — by default 0–1023 for 8-bit sources as much as for 12/14/16-bit camera raw, and independently of the project's Video/Data Levels setting, because the scope shows Resolve's internal full-range state rather than the file's encoding. The reason is cross-camera comparability: if each format's native range drove the axis (0–4095, 0–16383, 0–65535), a colorist would have to relearn where black, mid-grey and white sit for every camera. Resolve does offer alternative scale styles (Percentage 0–100 — the old IRE convention —, millivolts, 12-bit, and an HDR style reading ST.2084/HLG nit values); 10-bit code values are the *default*, not the only option. HDR generally changes the *reference system* (nits, PQ, HLG) rather than widening the number range. **This is separate from the bin count** — how many counting containers span that axis — which is an independent implementation choice; see the note in the "Histogram vertical scaling" row. TrueScope follows the same convention for the SDR display-referred case; HDR and non-Rec.709 scale behaviour is deferred (`backlog.md`). |
| Anti-pixelation recipe | (1) **density accumulation mapping count → texel brightness** for density fields (parade, waveform, vectorscope) — no binary on/off pixels. The transfer is **linear × a per-scope user gain, hard-clipped at white (M6.3)**. It shipped as **log** in M4–M6.2 (`ln(1+16·C)/ln(1+16·C_max)`, measured D1 2026-07-20, pinned in `TrueScope.Diagnostics.DensityTransfer`; **D1** = the 2026-07-20 diagnostic that measured both tools' density transfer curves against a neutral count wedge of known per-column pixel counts, establishing Resolve = linear and TrueScope = log) and was changed to linear in M6.3 to match Resolve's measured-linear transfer — see the "Density transfer & Trace Brightness" decision row; (2) scope textures at display resolution or higher; (3) graticules/scales as anti-aliased Slate lines (`FSlateDrawElement::MakeLines`, `MakeCustomVerts`) | This is why Resolve/FCP scopes look smooth and most free solutions don't. |
| Two distinct count mappings — never confuse them | **count → brightness** for density fields (parade, waveform, vectorscope) and **count → curve height** for the histogram are two distinct mappings — keep them separately named in code. **Both are linear (M6.3):** density maps count → pixel **brightness** with a *per-scope user gain*, hard-clipped at white; the histogram maps count → curve **height** against a *fixed shared* clip threshold with a *single shared user gain*, clipped at the band top. They differ in target (brightness vs height), in gain scope (per-scope vs shared), and in what the clip means (white vs band top) — not in curve family, and not in slider law (both use the logarithmic law above). | Applying the log brightness curve as a curve *height* flat-topped the M4 histogram (a 22-pixel bin drew at 47 % height) and made the apparent channel fall-off order depend on stray outlier pixels rather than the distribution. An earlier version of these docs stated the log mapping without this distinction and caused the bug. The M5 GPU path inherited the distinction; M6.3 changes the density transfer from log to linear, but the *separation* stays. |
| Histogram vertical scaling | The three curves **always share one scale**, never scaled per channel. The scale is **fixed**, not derived from the frame's own peak: a bin whose pixel count reaches the **clip threshold** draws at full band height, and anything above it is **hard-clipped** at the band top. **Clip threshold = the pixel count at which a single bin fills the band, expressed as a fraction of the analysed frame's total pixel count.** Default = **1.58 % of all frame pixels** (for a 960×540 capture that is ≈8 191 pixels in one bin). Because it is a *fraction*, an HD and a UHD project of the same content look identical. count → curve height is **linear** throughout. The user adjusts it with a **Histogram Gain** slider (see "Gain slider law"); centre = default. | A shared scale preserves the true ratio between channels; per-channel scaling would inflate a nearly empty channel to full height and mislead. A *fixed* scale (rather than one normalised to the frame's own peak) is what makes two shots comparable: the same curve height always means the same pixel count, so a colorist walking a sequence reads one consistent yardstick. It also keeps shadow and highlight detail readable — a broad low distribution (grey ramp) stays a low flat line instead of being stretched to fill the band — while a clipped mid-tone peak is harmless, because exposure is not in question there. Resolve behaves this way and colorists read histograms this way. **Measured against DaVinci Resolve, 2026-08-17** (method and full evidence in `dev-rules.md`, "Resolve cross-check — conditions & procedure"): (1) *fixed, not peak-derived* — the identical 1.56 %-of-frame stripe drew at exactly the same height (368 screen px) in two images that differed by an added 8 %-of-frame stripe; a peak-derived scale would have shrunk it fivefold; (2) *fraction of total pixels, not an absolute count* — the same four fractions clipped identically in a 960×540 and a 1920×1080 timeline, though the HD counts were 4× larger; (3) *linear* — in the HD reading a count ratio of 1.420 produced a height ratio of 1.421; (4) *clipping confirmed* — two stripes of 1.77 % and 2.19 % drew at exactly the same height, and a clipped peak did not move when the slider halved the scale while unclipped peaks halved. The threshold itself measured **1.58–1.76 % across five readings, mean ≈1.67 %**; the spread is the limit of reading thin curve apexes off screenshots, so only two significant digits are claimed. **Refined 2026-08-19** to **1.58 % of all frame pixels** (from an earlier 1.67 %), measured on purpose-built full-height strips of known pixel fractions in a 1920×1080 timeline: strips of 0.4167 % and 0.8333 % drew at 26.3 % and 52.7 % of band height, giving 1.584 % and 1.581 % independently, and the 1.6667 % strip clipped. Earlier readings of 1.69–1.76 % came from very narrow one-bin spikes whose apex is cut by line anti-aliasing when read off a screenshot, biasing the derived threshold upward; the wide strips do not have that bias. Unproven but consistent with every reading: the underlying constant may be 1/64 = 1.5625 %, which the measurements bracket. **Scale and bin count are different things.** The 0–1023 axis is a labelling convention; the *bin count* is how many counting containers span it, and the axis label does not reveal it. **Measured (2026-08-17):** Resolve resolves ~1024 distinct containers across 0–1023 — an 8-bit test image leaves three empty containers between each populated one, which would not happen with 256. **TrueScope uses 256 bins and the threshold is not rescaled (settled M6.3 Task 0, 2026-08-18).** This supersedes the earlier reasoning in this row, which held that 256 bins would gather four times the pixels per container and draw everything four times taller: that holds only where the source has more distinct values than there are bins, which an 8-bit capture does not. **The clip threshold is defined per distinguishable source value, not per bin index.** It transfers between tools unchanged as long as each bin holds exactly one source value — i.e. as long as the binning is at least as fine as the source. TrueScope captures 8-bit (`RTF_RGBA8`) and uses 256 bins: one code value per bin, the finest binning the data supports. Resolve bins the same 8-bit image into 1024 containers, so three out of every four sit empty and each populated one holds exactly one code value's pixels. Both therefore count the same thing, so the measured threshold applies to both unchanged. A rescale would only be needed if bins were *coarser* than the source, so that one bin merged several distinct values. Going to 1024 bins here would add no information — three quarters of them would be provably empty. |
| Gain slider law (histogram gain and Trace Brightness) | Every gain slider in TrueScope is **logarithmic**: equal travel always means the same *multiplication*, the way photographic stops work (one stop = one doubling). Range = **±3 doublings**, so gain runs **0.125× … 8×** with **1.0× exactly at centre**; formally `gain = 2^(6p−3)` for slider position `p ∈ [0,1]`. The slider is **continuous** — the ±3 figure defines the endpoints, not detents. Centre carries a **visible mark and snaps**; **double-click resets** to centre. **No numeric value is displayed.** What is persisted is the **gain multiplier** (1.0 = default), never the slider position and never a derived percentage. For the histogram this maps the clip threshold from 12.6 % of frame pixels (far left) through 1.58 % (centre) to 0.20 % (far right). | Resolve's histogram slider is linear in the *threshold*: measured 3.16 % of frame pixels at the left end, 1.58 % at centre, ≈0 at the right end. **What was measured in Resolve is the *ratio*, not the left-end figure**: its slider's left end sits at exactly **twice** the default threshold. The left-end percentage is therefore derived, and moves whenever the default moves — which is why it reads 3.16 % here and 3.34 % in earlier notes: only the default was re-measured (1.67 % → 1.58 % on 2026-08-19) and the doubled figure followed it. Nothing about Resolve was re-measured and no measurement was silently altered. That keeps its default at centre — but it makes the two halves behave completely differently: the left half spans a factor of 2, the right half spans a factor of infinity, so almost all of the right-hand effect happens in the last few millimetres and the right end is unusable (everything clips, nothing is readable). A logarithmic law fixes both problems at once: the default lands at centre *by construction* rather than by forcing it there, and both halves respond equally. It also lets us extend the useful range in both directions — the left end reaches 12.6 % (vs Resolve's 3.16 %), enough that even dense content can be viewed without clipping, which is why no separate "fit the frame's peak" mode is needed (that idea is recorded in `backlog.md`). **No numeric readout is deliberate** (human decision): a displayed "1.0×" reads as the officially correct setting, and less experienced artists then feel obliged to leave it alone instead of adjusting to what they need to see. **The ±3-doubling range is a chosen working value (estimator), not a measured one** — Resolve's range is not a target to copy here, since copying it is what produces the unusable end. Confirm or adjust it during visual calibration. |
| Density transfer (count → brightness) & Trace Brightness | **M6.3 decision (2026-07-20):** density fields map count → **brightness linearly × a user gain, hard-clipped** at trace max — replacing the log transfer of M4–M6.2. **Measured, not assumed:** Resolve's density transfer is **strictly linear** (neutral count-wedge of known per-column pixel counts, read off Resolve's Y-waveform at three brightness-slider settings → linear-through-origin R² = 0.9999, power exponent p = 1.00 each; the slider is a pure linear gain multiplier; above trace max it hard-clips). TrueScope's shipped transfer was measured **log** (`ln(1+16·C)/ln(1+16·C_max)`, D1, pinned in `TrueScope.Diagnostics.DensityTransfer`) — and that log is the cause of "structure drowns in dense areas": it lifts one pixel in a 540-row column to 31 % brightness, pushing sparse cells Resolve draws near-black a third of the way to white before real density exists. **Not** a compositing difference — D2 (**D2** = the 2026-07-20 diagnostic that ruled out RGB compositing as the cause of the divergence) confirmed TrueScope's RGB compositing already desaturates on overlap like Resolve; the divergence is the transfer curve alone. **The gain is a per-scope "Trace Brightness" control** (artist-adjustable; **per-scope, not global** — the scopes present their values differently and one setting does not fit all; human decision 2026-07-20; slots into the M4 `FTrueScopeDisplayMode` per-scope menu). Default gain calibrated to Resolve's default look (implementation iterates visually). Clip is **hard**, as Resolve does. Regression tests pin the linear transfer against the measured slopes, each proven by deliberately breaking it and confirming the test fails. The gain uses the shared logarithmic slider law — see "Gain slider law". | The log transfer was a carry-over never measured until D1; measuring both sides (Resolve linear, TrueScope log) is what settled it. |
| Per-scope display modes | Scopes may offer **display modes** — alternative renderings of the same analysis data, switched per scope via right-click (`FTrueScopeDisplayMode` + menu on `STrueScopeWidget`, built in M4). Shipped: histogram *Separate Channels* / *RGB Overlay*; waveform *RGB* / *Luma*. Planned: parade and vectorscope modes, Monochrome, Show Guides, **per-scope Trace Brightness (M6.3 — a gain slider on the density scopes)**, **histogram gain (M6.3 — a slider on the fixed shared scale)**; both sliders follow the shared logarithmic law (see "Gain slider law") | A display mode is not a scope type: it changes presentation, not what is measured. Neither member of a pair is "the" correct one (luma for exposure, RGB for colour balance; stacked channels for video, overlay for photo work), so nothing is hardcoded and the user chooses. Defaults follow Resolve so the scope cross-check stays apples-to-apples. |
| Layout | Fixed 2×2 `SSplitter` grid: Waveform / Parade (top row), Vectorscope / Histogram (bottom row) | Resolve reference layout. Per-panel scope-type selection and alternative layouts are deferred (see `backlog.md`, "Window layout options"). Not to be confused with per-scope **display modes**, which exist since M4 and change presentation, not which scope a panel shows. |
| Data interface | `FTrueScopeAnalysisResult` (bin arrays and/or textures) decouples analysis from UI | Lets CPU → GPU migration happen without touching the UI layer. |
| Repo scope | This repo contains **only** `Plugins/TrueScope/`; the host project stays local to each developer | Users clone straight into their own project's `Plugins/` folder. |
| Menu entry | Window menu only in V1; toolbar button deferred (backlog) | Toolbar space is contested; Window menu is where editor panels are expected. |
| Cross-platform parity outranks optimization | Windows, macOS and Linux must all work. If an optimization would restrict, degrade or bypass one platform, it is not taken — the portable path is chosen even at a measurable performance cost. Platform-specific measurements (e.g. Metal readback timings) inform the *problem statement*, never the architecture alone; before designing around a number, ask whether the number is platform-specific. | A tool the developer can only test on one of its three target platforms must not depend on that platform's characteristics. Portability is a product promise, performance is a quality. |
| Status line: OCIO view-transform name via a hard OpenColorIO plugin dependency | The status line names the **live OCIO view transform** when one is enabled for the measured viewport (e.g. "OCIO: sRGB - Display - Un-tone-mapped"), and the plain **"sRGB / Rec.709"** otherwise. Source: `FOpenColorIODisplayManager::GetDisplayConfiguration(ViewportClient)` — the same store the viewport's "OCIO Display" menu writes on every change, keyed by the same viewport client TrueScope measures, so the label can never disagree with the menu. **TrueScope declares the OpenColorIO plugin as a dependency** (`TrueScope.uplugin` plugin reference + module link). Decided by the human 2026-07-17 (work order 13, third and final revision of this decision), with this reasoning: (1) the OCIO plugin ships with every Unreal version — free, always installed, never missing; (2) the primary target audience (Film & TV, VFX, Automotive/Commercials, Broadcast & Live Events) practically always has OCIO in use already; (3) users outside that audience get no workflow intrusion worth the name. | **Why the soft alternatives lost — findings of 2026-07-17, kept so this is not re-litigated blind:** (a) The reflection-readable store (`UOpenColorIOLevelViewportSettings`) **persists only on viewport teardown** (`OnLevelViewportClientListChanged` — layout switch or editor exit), *not* on menu changes (`OnDisplayConfigurationChanged` writes only the live display manager). A label read from it lags in-session toggles and was observed contradicting the OCIO menu live — worse than no label for a status line whose job is trust. (b) The live state lives **only** in `FOpenColorIODisplayManager`, a plain C++ class whose accessors are DLL-exported and non-virtual — reachable exclusively by linking module `OpenColorIO`; the module interface's sole virtual (`GetDisplayManager()`) does not help without it. There is no third path. (c) Also learned: the OCIO plugin keys viewport presets by **layout + pane** (`FourPanes2x2.Viewport 1.Viewport1` vs `Viewport 1.Viewport0`), so each layout's pane remembers its own transform and switching layouts can *appear* to flip the OCIO setting by itself — engine behaviour, pre-existing, merely made visible by the status line. The V1 calibration caveat stands: percentages and graticule are calibrated for Rec.709/sRGB (see "Calibration note"); naming the active transform makes that more honest, not less. |
| Analysis path selection | Both paths ship. From M5 on, the **GPU compute path is the internal default**; the CPU path remains available as a user-selectable compatibility option. UI labels in the TrueScope tab: `GPU (performance)` / `CPU (compatibility)` — neither is labelled "default". A console variable exists **in addition** for diagnostics, never as the only access. Both paths must produce identical values on the synthetic test images. The M6 status line shows which path is active. **Switching paths takes effect promptly regardless of the viewport's Realtime setting** (phase-5.1): the resident last capture is re-analysed once through the newly selected path — no redraw, no new capture, the user's viewport settings untouched. The GPU→CPU direction seeds the CPU path with a **one-time readback at the switch moment**, the sole documented exception to the GPU path's no-readback rule (human-approved in phase-5.1; steady state stays readback-free). Because the switch re-analyses the *identical* frame, it doubles as the cleanest possible CPU-vs-GPU comparison: same input, no temporal jitter. **The selection is global (phase-5.2): one switch sets all four scopes at once**, offered in every scope's right-click menu ("Analysis Path (all scopes)") so it is discoverable from any scope — a per-scope switch would make the user set the same option four times; simple operation for the artist takes priority. A per-scope override, if ever wanted, is a debug-only concern and stays out. | The shipped path is the path that gets exercised: defaulting to CPU would leave GPU-path bugs undiscovered on the two platforms the developer cannot test. The CPU path's render-thread readback costs **1–2× host frame-time per capture** (measured ≈2× median — see "Measured: controlled readback benchmark" below and `phase-3.md`, "Correction") — a periodic render-thread block, not a free hitch — and must not be the out-of-the-box experience. The compatibility option exists for driver/platform failures, **not** for different results — say so in the README, so "performance" is never read as "less accurate". The GPU path must contain **no readback at all** (results stay GPU-resident): on Metal the readback `Lock` drains the in-flight GPU queue regardless of payload size, so even a few-kilobyte bin readback would pay the same 1–2 frame-time block — measured and verified in engine source (`phase-3.md`, "Correction"). The GPU-resident design is also the portable one, so the platform-specific finding and the parity rule agree. |
| UI terminology | **main bar** (always-visible row at the top of the tab) · **settings popup** (gear at its right; TrueScope-wide options) · **panel** (one scope area) · **panel header** (always-visible row on each panel) · **panel menu** (opened from the panel header; that panel's options) · **scope canvas** (the drawing surface). Adopted 2026-08-21 (M6.4) for docs, code and work orders alike. | Three different people-or-agents called the same row "toolbar", "top bar" and "main menu" in the same week. One vocabulary removes a whole class of misunderstanding at no cost. |
| Settings ownership is **per panel**, not per scope type | Display mode, Trace Brightness, Histogram Gain, vectorscope zoom, marker selection and the skin tone toggle belong to the **panel**, stored as an array keyed by panel index (four entries today), including the panel's scope type. **Analysis path and capture rate stay global.** Human decision 2026-08-21, implemented in M6.4 before the menus were rebuilt. | A later phase lets each panel pick its scope type freely, and showing the *same* type twice with *different* settings is explicitly wanted (two waveforms at different Trace Brightness). Under type-ownership that is impossible. Deciding it while the menus were being rebuilt anyway made it an ordering choice; afterwards it would have meant rewriting the settings store and every menu binding. |
| Two-level UI: global vs. per panel | TrueScope-wide options are reachable from exactly **one** place (the settings popup); per-panel options from exactly one place (that panel's menu). No option appears at both levels. The scope-canvas right-click menu was removed in M6.4.1 once the panel header was confirmed working; right-clicking a scope canvas deliberately does nothing, and the panel header is the single entry point. | The main bar had accumulated global and per-scope controls side by side, with the analysis path duplicated in the main bar *and* in every scope's right-click menu, and several identically labelled Trace Brightness sliders whose ownership was invisible. One control, one home. |
| Engine icons are referenced by style name, never copied | Engine artwork (`settings.svg`, `menu.svg`, `ResetToDefault.svg`) is used through its `FAppStyle` style name. No engine file is copied into this repository. | A style name survives engine content reorganisation, where a file path does not — and no copy means no licensing question to answer. The plugin's own artwork (`Resources/Icon128.png`, `Resources/Icons/TrueScope_Icon_mono.svg`) is of course in the repository. |
| Continuous controls give immediate visual feedback | Moving Trace Brightness or Histogram Gain re-renders the panel's scope texture at once from the last analysis result — no new capture, no re-analysis, no readback, continuously during the drag, both analysis paths. Showing the previous capture while a menu is open is correct and carries no "stale" indicator. Introduced in M6.4.1. | While any Slate menu is open the editor viewport does not tick, so no fresh capture can arrive; without an explicit redraw a slider only takes visible effect when the menu closes, and a value that cannot be seen cannot be judged. The frozen viewport is also why the previous capture is the *right* thing to show: the image the user compares against is frozen at that same moment, so scope and image stay consistent and WYSIWYG holds. Applies to any future continuous control, not just these two. |
| Gain changes re-derive, never re-scale | A gain change re-runs analysis on the retained frame (GPU: `ReanalyzeLastCapture` on the resident capture RT; CPU: rebuild intensity arrays from retained counts). The finished scope texture is never re-scaled at draw time, and GPU bin buffers are not retained across captures. Settled in M6.4.1. Neither route blocks: the GPU route enqueues a render command, the CPU route launches a task, and both return to the caller immediately. **A's affordability was measured with the level viewport frozen, which is the condition every gain change occurs in today, since every gain control lives inside a popup menu. A gain control placed outside a menu — a dockable settings panel, for instance — would run this work in frames that are also rendering the scene, and would need its own measurement before being accepted.** | Both count-to-appearance transfers hard-clip at the top — density at white, histogram at the band top — so a clipped texture carries no information to recover; scaling it would silently invent or destroy detail. Re-deriving from the frame we still hold keeps one source of truth, where a retained bin buffer would be a second copy that must be kept consistent with the last capture (the M4 bugs were of that family). Retaining the bins would have cost 4.00 MiB VRAM; the memory was affordable, the extra lifetime was the objection. |

## Architecture overview

```
Last focused Level Editor viewport (incl. Cinematic, in-viewport PIE)
        │
        ▼
FSceneViewExtensionBase (plugin, no level footprint)
  post-tonemap hook → copy/downsample into render target   ── M8: same hook,
        │                (throttled to ~2–3 Hz)                writing pass:
        ▼                                                      Zebra / clip
┌─────────────────────────────┐                                warn / False
│ Analysis                    │                                Color overlay
│ Stage 1: CPU histograms     │
│ Stage 2: compute shaders    │
│ → FTrueScopeAnalysisResult  │
└──────────────┬──────────────┘
               ▼
┌─────────────────────────────┐
│ Slate Nomad Tab "TrueScope" │
│ 2×2: Waveform | Parade      │
│      Vector   | Histogram   │
│ AA graticules, density map  │
└─────────────────────────────┘
```

Planned source layout:

```
TrueScope/
├── TrueScope.uplugin            (Type: Editor)
├── LICENSE                      (Apache-2.0)
├── docs/                        (this file, phase specs, backlog.md)
├── Shaders/                     (stage 2: TrueScope.usf; M8 overlay shaders)
└── Source/TrueScope/
    ├── TrueScopeModule.cpp      (tab spawner, shader dir mapping)
    ├── Capture/                 (scene view extension, throttling, RT)
    ├── Analysis/                (CPU analysis; later RDG passes)
    ├── UI/                      (STrueScopeTab, STrueScopePanel, per-scope widgets)
    └── Tests/                   (automation tests)
```

## Phase plan (milestones)

| Phase | Deliverable |
|---|---|
| M0 | Environment ready: build loop, VS Code, CLAUDE.md, git repo, MCP plugin |
| M1 | Plugin skeleton: empty dockable "TrueScope" tab, Window-menu entry, 2×2 splitter with placeholders |
| M2 | Capture via **SceneViewExtension**: post-tonemap image of the active viewport copied/downsampled into a render target at 2–3 Hz. Acceptance includes: (a) toggling the viewport tonemapper / show flags is reflected in the captured image; no crash or stall when the tonemap pass is disabled (verify hook behavior — fallback: hook a later pass in the chain); (b) with an OCIO viewport transform active, the captured image includes that transform — TrueScope's pass runs after the OCIO extension's pass |
| M3 | CPU MVP: async readback, histogram + parade drawn anti-aliased in Slate, automation tests on synthetic images. **Space-usage policy per scope type (architecture guardrail, established here because M3 draws the first scope):** each scope widget declares how it uses the space it is given. Waveform / Parade / Histogram are *responsive* — they fill the available area (X = image columns or luma bins, Y = luma or frequency; both stretch meaningfully). The Vectorscope is *aspect-locked* — it MUST stay circular; it scales uniformly and centers itself with margins, never distorting into an ellipse, no matter how narrow or wide the panel (a circle becoming an ellipse would falsify hue angle and saturation). Implement as a per-widget policy (e.g. `bMaintainSquareAspect`, true only for the vectorscope), not four hardwired special cases. Scope *types* are an extensible enumeration + factory (a fifth type like CIE Chromaticity, or the M8 overlays, must not require reworking the UI). Axis labels (0–1023, %, IRE) are UI elements of fixed size — not part of the scaled graphic — so they stay legible at any panel size (FCP behavior; Resolve scales them, which is the worse UX) |
| M4 | All four scopes complete (BT.709 math, vectorscope graticule, log mapping *(shipped M4; → linear in M6.3)*, scales) |
| M5 | GPU compute path (validated against CPU ground truth on test images) |
| M6 | Polish: settings, persistence, perf check, **appearance calibration across all four scopes at once** (density-to-brightness curve tuning; histogram **gain on the shared scale** — Resolve-style default vs. Exact — the three curves always share one scale, never per-channel), **TrueScope tab menu/toolbar** exposing user-facing options (analysis path, display modes, measurement rate) per the "end user is an artist" constraint, **measurement status line** showing what is being measured (viewport type/name; display encoding) and which analysis path is active (GPU / CPU). Split into **M6.1** (foundation & operation: raster resolution, main bar, settings, status line, panel-size hint — done), **M6.2** (appearance & features — done), **M6.3** (appearance calibration — done), **M6.4** (UI reorganisation — done: panel headers, settings popup behind a main-bar gear, settings ownership moved from scope type to panel, "Reset to Defaults" per panel, black backgrounds on all four scopes, the full nine-level histogram scale, and the plugin's window/tab icon) and **M6.4.1** (immediate slider feedback and menu polish — done: gain changes re-derive from the resident frame so the sliders respond continuously during a drag, the scope-canvas right-click menu removed, one uniform text column and one vertical rhythm across all four panel menus and the settings popup, the reset icon restored; see `phase-6.4.md`). **M6 is complete — signed off 2026-08-22.** M6.3 in detail: density transfer changed from log to **linear × per-scope Trace-Brightness gain, hard clip** (Resolve match); **histogram gain slider** on a fixed shared scale (default clip threshold 1.58 % of all frame pixels), using the shared logarithmic slider law. Both calibrated against measured Resolve references (wedge method). *The active OCIO view transform is named in the status line, live, via a human-decided hard OCIO plugin dependency (work order 13, final revision after the soft path proved unable to track live state) — see the settled decision below.* |
| M7 | Release: `RunUAT BuildPlugin` for Win64+Mac+Linux (Win/Linux built on a Windows machine — Linux via Epic's cross-compile toolchain), README, binaries; optional Fab. **Close the GPU draw-pass coverage gap (from M6.3):** the density transfer's HLSL is applied inline inside `AddAnalysisPasses_RenderThread` with no test-reachable entry point, and no test reads back a drawn texture — so CPU/GPU parity is currently pinned at the level of bins and the shared scalar threshold only, with the transfer itself covered by formula inspection and the visual pass. Expose a draw pass for readback-based testing before the GPU path becomes the default. Test-time readback is permitted; the no-readback rule governs the steady state, not the test suite. This is the layer both M4 bugs lived in (`dev-rules.md`, "Test the mapping layer, not just the analysis"). **Cross-platform parity check:** verify the same defaults and the same values on Windows 11 / NVIDIA RTX 5070 as on the M1 Mac Mini / Metal — both analysis paths, both platforms, same result. This is the first real test of the uint-atomics / no-wave-intrinsics constraint rather than an assumption. |
| M8 | **Overlays: Zebra, over/under-exposure warning, False Color** — writing SceneViewExtension pass, thresholds consistent with waveform %, per-viewport toggles in the TrueScope tab (details: backlog.md) |

## Testing strategy

Scopes are mathematically verifiable — exploit that:

1. **Automation tests on synthetic images** (headless): solid red frame → one
   histogram peak in R at max bin; horizontal gradient → diagonal waveform
   line; SMPTE color bars → vectorscope points land in the six graticule
   targets. CPU results later serve as ground truth for GPU results.

   Two lessons paid for with real bugs:
   - **Test the mapping layer, not just the analysis.** Analysis tests feed a
     pixel array into `AnalyzePixels` and assert on bin arrays. Both M4 bugs
     lived *after* that: in how bin arrays were assigned to curves and how
     counts were mapped to height. Add a test per scope that pins *which data
     lands in which drawn element* (e.g. `UI.HistogramChannelAssignment`,
     `UI.WaveformModeSelection`).
   - **Prove a regression test has teeth.** After writing a test for a bug,
     deliberately reintroduce the bug and confirm the test fails, then revert.
     A test that never failed proves nothing. Also design cases that *can*
     expose the fault: the M3 parade bleed survived a solid-red test because
     pure red has no other channel to bleed — pure-blue was needed.
   - **Keep both analysis paths honest.** From M5 on, the CPU path is only
     reachable through a compatibility option, so it can rot unnoticed. The
     headless automation tests keep exercising it (no RHI required), and one
     test compares CPU and GPU results on the same image. Neither path may
     break silently.
2. **Pixel-exact image check — macOS Digital Color Meter** (fast verification
   that two versions of the *same image* carry identical values — e.g.
   viewport vs. captured image in M2 — rather than merely "looking the same"):
   read the value at a point in one image and at the corresponding point in
   the other; they must match. This caught two real M2 bugs by measurement
   that the eye would miss — an undefined-alpha wash-out and a Slate
   display-gamma re-encode (`preview ≈ pow(viewport, 1/2.2)`).
   **Applicability:** image-to-image only. From M3 on, scopes are *graphical
   representations*, not a second copy of the image, so this method does
   **not** verify scopes — use the scope cross-check (point 5) for those.
   (Windows/Linux equivalents exist; the check is the principle, not the
   specific tool.)
3. **WYSIWYG tests (M2+):** change viewport state (tonemapper on/off, exposure
   compensation, show flags) and assert/inspect that captured image and scopes
   follow.
4. **Editor screenshots** via Epic's Claude Code MCP plugin for layout/visual
   checks during development.
5. **Scope cross-check — TrueScope vs. DaVinci Resolve** (*the* scope
   verification method from M3 on; acceptance ritual for scope work, repeated
   after major analysis changes). Both tools analyze the same still, so their
   scopes must agree on the values they report.
   1. Put a test subject in the viewport — the engine's **ColorCalibrator**
      asset works well (includes a color-chart texture). Use **fixed
      exposure** (manual / fixed EV100); auto eye adaptation would make the
      screenshot and the scope moment differ.
   2. Operational capture conditions and the screenshot procedure live in
      `dev-rules.md`, "Resolve cross-check — conditions & procedure".
   3. Place Resolve's scope window next to TrueScope's, size them similarly,
      and compare both the visual impression **and the values read off each
      scale**. Screenshot both to document the comparison.
   - **Beware scale/mapping differences:** the two can look different yet both
     be correct — e.g. a scope drawn on a different intensity mapping or gain
     looks different while reading the same values on its scale. Compare
     values on the scale, not raw appearance.
   - Photoshop's histogram is a quick second opinion without setup, but only
     Resolve provides waveform, parade and vectorscope.
   - Backlog: also cross-check against Final Cut Pro's scopes before a public
     release (not installed on the dev machine yet).
   - **Verified capture geometry (measured 2026-07-20).** TrueScope samples the
     post-tonemap `SceneColor.ViewRect` — the view's *render rectangle*, not the
     whole viewport window — and bilinearly downscales it, aspect-stretched,
     into a **fixed 960×540** working target (`TrueScope::CaptureWidth/Height`,
     `TrueScopeViewExtension`; RGB only). Two consequences: (a) piloting a
     **CineCameraActor whose filmback aspect differs from the viewport** does
     **not** leak its black letterbox bars into the scopes — the ViewRect is the
     constrained image region (logged 1132×637 = 16:9 from a viewport that was
     *not* 16:9), so the bars lie outside it and the artist's viewport size is
     free. (b) The scopes are resolution-normalized, so agreement does not
     require matching resolutions; but for a **pixel-exact** comparison the PNG
     should be **960×540** (the capture's own pixel count). Literal
     one-PNG-pixel = one-measured-pixel would need dumping the capture RT itself
     — see `backlog.md`.
   - **Diagnostic instrument — verify what TrueScope is actually sampling.** Set
     `LogTrueScope` to Verbose; each accepted capture logs
     `Capture pass ran (WxH)` with `SceneColor.ViewRect`'s dimensions (and the
     one-time `First viewport capture (…→…)` line reports source→target). This
     is how the constrained-camera bar question above was settled without
     guessing: a camera constrained to 16:9 in a non-16:9 viewport logged
     `1132x637` (= 16:9, the camera aspect), proving the ViewRect is the
     constrained region. The same line is the truth signal for Realtime state —
     its timestamp stops advancing when Realtime is off.

## Licensing red line

`norihiro/obs-color-monitor` (GPL-2.0) and ffmpeg scripts are *behavioral
references only* (what should scopes show for a given input). No code may be
copied or transliterated from GPL sources. All math here is textbook-standard
(BT.709 matrices, histogram binning) and is implemented from scratch.

## Dev environment notes

- **MCP toolsets (host `.uproject`, dev-only):** the `ModelContextProtocol`
  plugin only provides the server + meta-tools; each actual toolset is a
  separate plugin under `Engine/Plugins/Experimental/Toolsets/`. We enable
  exactly three instead of Epic's `AllToolsets` aggregator (which pulls in all
  ~21, most irrelevant to this project):
  - `SlateInspectorToolset` — Slate UI automation/inspection (Snapshot, Click,
    PressKey, **Screenshot**, Windows, WaitFor). This is what verifies the
    TrueScope tab visually. `ToolsetRegistry` is pulled in automatically.
  - `EditorToolset` — spawning/inspecting test scenes (color charts,
    gradients) for the WYSIWYG and scope tests from M2 on.
  - `AutomationTestToolset` — running TrueScope automation tests from the live
    editor over MCP.

  These live in the host `UEscopes.uproject` (not under version control), not
  in `TrueScope.uplugin` — they are development tooling, never a plugin
  dependency shipped to users.
- **Two development machines (as of 2026-08-18).** The primary one is an Apple
  M1 Mac Mini (2020, Metal); a second is now available — **Windows 11, NVIDIA
  RTX 5070, 16 GB VRAM**. TrueScope already builds and runs there, but the
  scopes have not yet been compared in detail between the two. This is the
  machine that makes "cross-platform parity outranks optimization" *verifiable*
  rather than merely observed: same defaults, same values, both analysis paths,
  Metal and D3D12 (see the M7 row).
- **Reference hardware is modest by design.** All timings so far were measured
  on a low-end development machine (Apple M1 Mac Mini, 2020, Metal). Treat
  every performance figure as a conservative upper bound — but note that the
  readback `Lock`, the dominant cost, is **not** fixed in the way raw compute
  is. On Metal it drains the **in-flight GPU queue** (see `phase-3.md`,
  "Correction"), so it costs **1–2× host frame-time** and therefore *shrinks on
  faster hardware* in proportion to frame rate: roughly the depth of the GPU
  queue, whatever a frame costs. Express readback-related costs as
  *frame-times*, not milliseconds — a millisecond figure from this machine does
  not transfer; the frame-time multiple does.

### Measured: controlled readback benchmark (2026-07-11)

The reference figures for the CPU path, from the controlled A/B benchmark run
under the `dev-rules.md` handshake. **These supersede all earlier millisecond
figures**, which were taken under uncontrolled conditions.

*Conditions (verified live, nothing altered):* Apple M1 Mac Mini / Metal, UE
5.8, reference scene `StartMap` (Viewport 1, Perspective, camera parked on the
`CineCameraActor`, no interaction during the window). Scalability **Epic**
(`sg.*Quality=3`, `sg.ResolutionQuality=100`), `r.ScreenPercentage 100`,
`t.MaxFPS 0`, editor VSync off, "Use less CPU in Background" off, editor-wide
High DPI **off**. Capture at the shipped ~2.5 Hz throttle (measured 2.38 Hz).

| Quantity | Result |
|---|---|
| **A/B felt impact** | Run 1, TrueScope *not loaded* (`-DisablePlugins=TrueScope`, the only provably-idle baseline — see below): 24.15 FPS (2727 frames / 112.90 s), median frame 34.77 ms. Run 2, tab open: 25.68 FPS (3728 frames / 145.19 s), median frame 33.81 ms. **Delta +1.5 FPS in favour of the TrueScope run** — i.e. TrueScope's cost is below this machine's run-to-run noise floor. **No measurable felt impact at 2.5 Hz.** |
| **Readback `Lock` alone** | 345 samples over the same window: **median 65.92 ms**, min 39.14 ms, max 227.94 ms, mean 71.87 ms. |
| **`Lock` as frame-time** | Against the 33.81 ms median frame: **median ≈1.95×**, min ≈1.16×, i.e. **1–2× frame-time**. |

*Verdict:* the earlier "≈ one frame-time per capture" estimate **does not hold**
— one frame-time is the *floor*. The transferable rule is **"the `Lock` drains
the in-flight GPU queue, i.e. 1–2× frame-time (≈2× median here)"**, still
independent of texture size. This does not change the M5 decision, it hardens
it: a GPU path with even a few kilobytes of readback would pay the same block,
so the GPU path carries **no runtime readback at all**.

*Method note (why the baseline needed a disabled plugin):* the view extension
gates capture on viewport focus and the capture throttle only — **not** on the
TrueScope tab's visibility — so closing the tab does not stop the readback and
cannot serve as an idle baseline. That gap is itself a product bug; it is filed
in `backlog.md` ("Capture runs at full cost while the tab is closed") for M5,
where the capture chain is reworked anyway.

*Caveat:* a macOS memory-pressure warning dialog appeared during run-1 *setup*
and was dismissed before the measurement window opened; run 1 was not otherwise
disturbed. Run 1's chart also reported an implausible 0.00 ms average
render-thread frame-time (run 2: 38.80 ms) — unexplained, and not load-bearing:
neither the FPS delta nor the `Lock` figures depend on it.

## References

- DaVinci Resolve scopes (visual reference; see reference screenshot)
- Frame.io — Introduction to Video Scopes:
  https://blog.frame.io/2017/09/27/introduction-to-video-scopes/
- Epic docs: Slate Overview / Slate Architecture / Slate Widget Examples,
  Cinematic Cameras, Cinematic Viewport (dev.epicgames.com)
- In-engine SceneViewExtension patterns: OpenColorIO plugin and other
  `FSceneViewExtensionBase` users under `Engine/Plugins` and `Engine/Source`
- Epic's Claude Code plugin (MCP editor control):
  https://github.com/EpicGames/unreal-engine-skills-for-claude-code-plugin
- Prior art (commercial, closed): Scope Insight (Fab), Exposure Tools —
  VideoScopes, Drastic ScopeDirect. TrueScope's niche: open source,
  Apache-2.0, all three desktop platforms, zero level footprint.
