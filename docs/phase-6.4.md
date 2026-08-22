# Phase 6.4 — UI reorganisation: panel headers, settings popup, per-panel state

Read `architecture.md` ("The end user is an artist"; "Per-scope display modes";
"Analysis path selection"; "UI terminology"; "Settings ownership is per panel"),
`phase-6.1.md` (tab toolbar, settings/persistence, status line, minimum-panel-size
hint), `phase-6.2.md` (Amendments — the permanent hue ring, the separate skin tone
toggle, the marker selector), and `phase-6.3.md` (Trace Brightness, Histogram
Gain, the logarithmic slider law).

*(Work order 16 named this file `phase-6_4.md`; the repository's existing files are
`phase-6.1.md` … `phase-6.3.md`, so it is written under the repository's own
convention. Same for the `phase-6_1.md`-style references above.)*

M6 is split. M6.1 delivered the foundation, M6.2 the scope features, M6.3 the
appearance calibration. **This is M6.4 — UI reorganisation.** Nothing about what
is measured changes; this phase changes only *where the user reaches it*.

The starting point is a diagnosis by the human: the main bar had grown into a flat
row mixing global options with per-scope options, with several Trace Brightness
sliders whose ownership was not visible ("which slider belongs to which scope?"),
and the analysis path appeared twice — in the main bar and again in every scope's
right-click menu. The fix is a two-level structure: TrueScope-wide options behind
one gear in the main bar, per-panel options in a header on the panel itself.

**Discoverability is why the header exists and not just a right-click menu.**
M6.1 Task 2 deliberately pulled options *out* of right-click menus for the
"end user is an artist" constraint. Moving them back into right-click alone would
have made the plugin harder to operate than before this phase. The panel header is
what keeps that constraint satisfied while emptying the main bar.

## Terminology (adopted project-wide)

| Term | Meaning |
|---|---|
| main bar | always-visible row at the top of the TrueScope tab, above all panels |
| settings popup | menu opened by the gear at the right of the main bar; TrueScope-wide options |
| panel | one scope area in the tab (four, fixed 2×2 in this phase) |
| panel header | always-visible row at the top of each panel |
| panel menu | menu opened from the panel header; options for that one panel |
| scope canvas | the drawing surface itself |

## Icons

All three engine icons are referenced **by their `FAppStyle` style name**, never by
file path, and are never copied into this repository — a style name is stable
across engine layout changes and raises no licensing question.

| Use | Engine SVG | Resolved style name |
|---|---|---|
| main bar gear | `Starship/Common/settings.svg` | `Icons.Settings` (16×16) |
| panel header menu button | `Starship/Common/menu.svg` | *no standalone style name in UE 5.8* — reached as `FAppStyle::Get().GetWidgetStyle<FToolBarStyle>("SlimToolBar").WrapButtonStyle.ExpandBrush` |
| Reset to Defaults | `Starship/Common/ResetToDefault.svg` | `PropertyWindow.DiffersFromDefault` (16×16) |

The plugin's own window/tab icon **is** in the repository:
`Resources/Icons/TrueScope_Icon_mono.svg`, 16×16 viewBox, a filled 2 px ring plus
a "T", drawn for legibility at 16–20 px. Its hole is formed by opposite path
winding, so it renders correctly under either fill rule.

## Task 0 — Engine source research

**0.1 Style names** — as in the icon table above.
`Icons.Settings` is registered in `StarshipCoreStyle.cpp:337` (a 20×20 alternate
`Icons.Toolbar.Settings` sits at :432); `PropertyWindow.DiffersFromDefault` in
`StarshipStyle.cpp:3655`. `menu.svg` has **no** standalone `FAppStyle` name in
UE 5.8: outside toolbar styles it is only registered in other plugins' private
style sets (ControlRigEditor, TraceInsights), which `FAppStyle` cannot reach, so
the `SlimToolBar` wrap-button brush (`StarshipCoreStyle.cpp:2395/2460`) is the
only route that keeps the "style name, no file copy" rule. Treated as an
implementation detail under Rule 3, not a decision-level surprise.

**0.2 SVG rasterizer and `width="100%"`** — measured with a compiled probe against
the engine's own vendored nanosvg:

```
numeric 16x16        image 16.000 × 16.000   bounds (2.00 2.00 14.00 14.00)
percent + viewBox    image 16.000 × 16.000   bounds (2.00 2.00 14.00 14.00)
percent, NO viewBox  image 12.000 × 12.000   bounds (0.00 0.00 12.00 12.00)
```

A percentage resolves against a zero length → 0, and `nsvg__scaleToViewbox` then
substitutes the viewBox. So `width="100%" height="100%"` **with** a viewBox is
equivalent to numeric values; **without** one it collapses to the path bounding
box. The 14 layout icons therefore need no editing as long as they keep their
16×16 viewBox (recorded in `backlog.md`).

**0.3 Header pattern** — `SDetailsView`'s filter-row "View Options" button
(`Editor/PropertyEditor/Private/SDetailsView.cpp` ~370–395): an `SComboButton`
with `.HasDownArrow(false) .ContentPadding(0.0f)
.ForegroundColor(FSlateColor::UseForeground()) .ButtonStyle(FAppStyle::Get(),
"SimpleButton")`, carrying just an `SImage`. Both the panel-header menu button and
the main-bar gear follow it.

**0.4 Settings-array shape** — `UPROPERTY(config) TArray<FTrueScopePanelState>
PanelStates;` in the M6.1 settings class (`config = EditorPerProjectUserSettings`),
with a `USTRUCT()` whose members are individually `UPROPERTY(config)`. Confirmed
against engine precedent, closest `FAssetEditorOptions`/`FViewportConfigOptions`
in `PersonaOptions.h:43,82` — also per-user config, also per-viewport-index state.

**Side findings.** Task 5's rate set matches the human's UI notes exactly —
`MeasurementRatesHz[] = {1.0f, 2.5f, 5.0f}` (`Capture/TrueScopeViewExtension.h:42`),
no discrepancy to report. And no Slate style set existed yet, so Task 1 created
`FTrueScopeStyle` from scratch.

## Task 1 — Style set and the TrueScope window icon

`FTrueScopeStyle` with its content root resolved through `IPluginManager` (no
hardcoded path), registering `Resources/Icons/TrueScope_Icon_mono.svg` as a 16×16
vector brush named `TrueScope.TabIcon`, set on the tab spawner so it appears in
the Window menu entry and in the tab itself. `Resources/Icon128.png` unchanged.

- [x] Style set torn down cleanly on module shutdown; no missing-Slate-resource
      log lines from TrueScope, no new warnings.
- [x] Icon visible on the tab next to the title — MCP screenshot, the ring-and-T
      mark renders correctly at 16 px.
- [x] Icon in the Window menu entry — verified 2026-08-22 (Status step 1): the
      row is `generic "TrueScope"` with a 14 × 14 image in the same icon column as
      its neighbours, rendering as a distinct circled "T".

## Task 2 — Settings ownership: per scope type → per panel

`FTrueScopePanelState` holds the panel's scope type (stored although fixed today),
display mode, Trace Brightness, Histogram Gain, vectorscope zoom, marker selection
and skin tone toggle; four of them live in `PanelStates`, keyed by panel index,
defaulting to today's 2×2 arrangement read from the code. Analysis path and
capture rate stay global. **No migration code:** previously persisted per-type
values are dropped, and because the compiled defaults are the M6.3 calibrated
values, the one-time reset lands on correct values.

- [x] Four independent panel states persist across an editor restart.
- [x] Analysis path and capture rate remain single global values.
- [x] `TrueScope.UI.PanelStateIndependence` pins that two panel states are
      independent — teeth-proven (perturbed, seen to fail, reverted).
- [x] Bin-equivalence tests green, values unchanged.

## Task 3 — Panel header

`STrueScopePanel`: an `SBorder` (`Brushes.Header`) above the scope canvas in a
`SVerticalBox`, so the canvas keeps its full remaining area rather than being
drawn under an overlay. Left, the scope type as static `SmallText`; right, the
menu button in the Task 0.3 pattern.

- [x] All four panels build a header carrying the correct type name and a menu
      button (`TrueScope.UI.PanelMenuContents` reads the real widget tree).
- [x] The vectorscope's aspect-locked policy is unchanged; it now squares against
      the canvas rather than the panel.
- [x] All four headers render with the right type name on the left and the
      hamburger on the right, and the vectorscope is still circular in its
      reduced canvas — MCP screenshot.
- [x] Clicking the button opens the panel menu — verified 2026-08-22 on all four
      panels through MCP, using the `Click` + `PressKey "Enter"` sequence
      (Status step 2).

## Task 4 — Panel menu, and emptying the main bar

Menu contents exactly as work order 16's table, `Both` renamed to **`75% + 100%`**,
`Show Skin Tone Line` last in the `Markers` section, the parade menu deliberately
thin. Sliders keep the M6.3 law unchanged. The scope-canvas right-click menu
survives as a temporary second entry point and calls the *same* builder
(`STrueScopeWidget::BuildPanelMenu`), so the two cannot drift apart.

- [x] Each panel menu contains exactly the specified rows and nothing else
      (`TrueScope.UI.PanelMenuContents`, walking the built menu widget).
- [x] Right-click menu and panel menu are the same content — one builder, pinned
      by test.
- [x] The main bar contains no per-scope control any more.
- [x] Every moved control still works and still persists — radios and the skin-tone
      checkbox verified 2026-08-22 through MCP and re-read from a reopened menu
      (Status step 2); the two gain sliders, which need a held drag, verified by the
      human in the closing walkthrough.

## Task 5 — Main bar settings popup

A right-aligned gear (`Icons.Settings`) opens the settings popup with exactly two
sections: `Analysis Path (All Scopes)` — `GPU (performance)` / `CPU
(compatibility)` — and `Capture Rate` — 1 Hz / 2.5 Hz / 5 Hz from
`MeasurementRatesHz`. The analysis-path control is gone from the main bar and the
`Analysis Path (All Scopes)` section from every scope's right-click menu, so the
path is reachable from exactly one place in the UI; the diagnostic CVar still
exists and still stays in sync.

- [x] Popup shows the two sections and nothing else
      (`TrueScope.UI.SettingsPopupContents`). The rate labels are pinned as the
      rate *set* plus the "Hz" suffix, not the decimal separator — `FText::AsNumber`
      is culture-aware and prints `2,5 Hz` on this machine's German locale.
- [x] The popup's radio state follows the **effective** path in both directions
      (`TrueScope.UI.SettingsPopupEffectivePath`, driving the CVar) — teeth-proven.
- [x] No analysis-path or capture-rate control remains anywhere else in the UI.
- [x] Gear present and right-aligned in an otherwise empty main bar — MCP
      screenshot.
- [x] Both settings survive a restart with the popup as the only entry point —
      verified 2026-08-22 (Status step 5): the editor came back on the GPU path with
      `MeasurementRateHz=5.0`, both as left.

## Task 6 — Reset to Defaults

Last entry of every panel menu, with `PropertyWindow.DiffersFromDefault`. It resets
only that panel's own display options, reading them from the single central source
(`TrueScope::MakeDefaultPanelState` via `UTrueScopeSettings::ResetPanelToDefaults`)
rather than from constants at the call site, so the next phase's Tweakables menu
can move those defaults without the reset fighting it. No confirmation dialog, and
always enabled. The M6.3 double-click reset on an individual slider stays: one
resets a slider, the other the panel.

- [x] Entry present and last in all four panel menus, with the icon.
- [x] `TrueScope.UI.PanelResetToDefault` pins that it resets exactly that panel
      and leaves the neighbour panel and the global settings untouched —
      teeth-proven (dropped the re-read and renamed the entry; both tests failed).
- [x] Default values come from one source (`MakeDefaultPanelState`).

## Task 7 — Three display fixes

All four scopes now share a **black** background. The near-black grey (0.008) they
shared before was invisible behind the waveform's and parade's *opaque* trace
rasters and visible behind the vectorscope's and histogram's *transparent* ones —
so two scopes read as black and two as dark grey although the code drawing the
background was already shared. Measured on a retina screenshot before the change:
background (28, 28, 28) everywhere, waveform/parade plot interior (0, 0, 0).

The histogram now labels the same nine levels as waveform and parade —
0, 128, 256, 384, 512, 640, 768, 896, 1023 — instead of only the quarters, using
the same font, colour and placement helper (`DrawValueAxisX`, unchanged apart from
the level list).

- [x] `TrueScope.UI.ValueScale` asserts the histogram's levels equal the waveform's
      element by element.
- [x] All four backgrounds are black — **measured**, not judged: an empty plot
      area in each of the four panels reads exactly (0, 0, 0) in the MCP
      screenshot, against (28, 28, 28) before the change.
- [x] The histogram's nine numbers sit under their gridlines and match the
      waveform's and parade's styling — MCP screenshot.

## Task 8 — Minimum-panel-size hint

The M6.1 thresholds were `MinReadableScopeWidth = 180`, `MinReadableScopeHeight =
120`, with **no recorded origin anywhere** — neither in code nor in `phase-6.1.md`.
The mechanism was already right: the scope widget is a sibling of the header in a
`SVerticalBox`, not an overlay, so it is handed the canvas and the check was
already canvas-level. What was missing was the derivation, which is what this task
supplies.

**The two measured inputs.**

- **Label line height = 13.0 px** — `GetMaxCharacterHeight` of the value-axis label
  font (`FCoreStyle::GetDefaultFontStyle("Regular", 8)`), measured through
  `FSlateFontMeasure` at DPI scale 1. The widest label the axis ever draws,
  `"1023"`, measures **24.0 × 13.0 px**.
- **Panel header height = 20.0 px** — measured by laying out a real
  `STrueScopePanel` and reading the header row's desired height: a 16 px icon
  inside the `SimpleButton` style's 1 px vertical padding
  (`StarshipCoreStyle.cpp`, `SimpleButtonIconOnly`) inside the header's own 1 px
  padding. A retina screenshot of the tab agrees to within one device row. A 2×2
  grid therefore spends **40 px of panel height on headers**, matching the work
  order's "roughly 40 px".

**The rule.** The threshold is the size at which the nine value-axis labels stop
colliding, taken as the worst case over the scopes — the labels are the first
thing to become unreadable, and they are what the fixed chrome exists for.

- **Width — 204 px, driven by the histogram**, the only scope whose value axis runs
  along X. Nine labels centred on their levels need at least one label width per
  gap: 8 × 24 = 192 px of plot, plus the histogram's horizontal chrome
  (2 × `PlotPadding` = 12) → **204**.
- **Height — 116 px, driven by waveform and parade**, whose labels stack along Y:
  8 × 13 = 104 px of plot, plus their vertical chrome (2 × `PlotPadding` = 12) →
  **116**. (Their left label strip is a width cost, not a height one, and the
  histogram's count axis carries no labels.)

Against the old numbers: the width threshold **rises** from 180 to 204 — a
consequence of Task 7, since the histogram had four labels along X before and now
has nine — and the height threshold **falls** from 120 to 116.

**Canvas vs. panel.** Those are canvas thresholds. The panel that contains the
canvas must additionally afford its header, so `MinReadablePanelWidth = 204` and
`MinReadablePanelHeight = 116 + 20 = 136`. Nothing enforces the panel figure today
(the 2×2 grid has no per-panel minimum); it is recorded because a docking or
layout minimum is what the layout system will need, and it is the number that
answers "how small may the *panel* get", which is what the user actually drags.

The vectorscope still reaches the threshold first: it is aspect-locked, so its
content rect is the largest centred square and a wide flat panel shrinks it on
both axes — and its square has to clear the *width* threshold (204) even though it
draws no value-axis labels of its own.

- [x] The old thresholds are gone from the code; `IsTooSmallToRead` takes a canvas
      size and compares against the derived constants.
- [x] `TrueScope.UI.PanelSizeHint` re-measures both font metrics and lays out a
      real panel to re-measure the header, then asserts the constants still equal
      what the derivation produces — so a derived number cannot silently rot into
      a chosen one. Teeth-proven: width put back to 180 and header to 20.5, both
      assertions failed, reverted.
- [x] Hint triggers at the corrected size on both RHIs — the rule is
      RHI-independent (pure geometry, no render path involved) and is covered
      headless; the on-screen check needs a splitter drag, which synthetic input
      cannot perform, and was done by the human in the closing walkthrough.

## Verification summary

**Test counts.** 40 at the close of M6.3 → **46** after Task 8, all green
(`-nullrhi`, editor closed for every run). Per task: +1 Task 2, +2 Task 4,
+2 Task 5, +1 Task 6; Task 7 and Task 8 extended existing tests instead of adding
new ones. 46 is the stale-code tripwire from here on.

**Visual verification (MCP, 2026-08-21).** Screenshots of the tab and of the 2×2
grid, taken through the running editor's MCP server: tab icon, the empty main bar
with its right-aligned gear, four panel headers, four black backgrounds (measured
per pixel) and the histogram's full scale. What no screenshot can show is anything
behind a popup — see the remaining unticked boxes.

**What is automated vs. human.** Everything that is a fact about the widget tree,
the menu contents, the settings store or the derivation is automated — the panel
menus and the settings popup are read out of the *built* menu widgets, not
described in a comment. What remains human is everything requiring a click on a
Slate popup or a judgement by eye: the icons, the gear's alignment, the black
backgrounds, the histogram numbers, and that each moved control still does what it
says.

*Superseded on 2026-08-22.* At the close of M6.4 this section recorded that MCP
could not open Slate popup menus, which is why so much of the list above was left
to the human. That turned out to be too broad: `Click` followed by
`PressKey "Enter"` opens them reliably, and the click-through below drove all four
panel menus and the settings popup that way with no human input. What is genuinely
out of reach is narrower — anything needing a **held drag** (sliders, splitters).
The rule in `dev-rules.md` has been replaced accordingly.

**MCP access path.** Direct HTTP to the running editor's MCP server (the permitted
fallback in `dev-rules.md`), because the `unreal-mcp` skill harness was
unavailable in this session.

**Commits.** `a87cd70` (Task 1), `d063498` (Task 2), `2aadcee` (Task 3),
`2abeede` (Task 4), `acc08b6` (Task 5), `303594d` (Task 6), `23cc238` (Task 7),
`846ac1d` (Task 8), plus the closing documentation commit.

## Deliberately out of scope

- **Scope-type switcher in the panel header.** The header shows a static type
  name. Free per-panel type selection arrives with the layout system (backlog),
  which is where the design question actually lives.
- **Layout selection menu.** 14 layout icons exist and have been checked, but the
  feature they front — variable panel count and arrangement — is a separate
  feature, and a first release does not need it. Backlog.
- **Migration of previously persisted per-type settings.** Dropped on purpose;
  the compiled defaults are the M6.3 calibrated values, so the one-time reset
  lands on correct values.
- **Greying out "Reset to Defaults" when nothing deviates.** Considered, rejected
  by the human as unnecessary visual noise.

## Addendum — M6.4.1

Three items found in the M6.4 acceptance walkthrough, fixed in a follow-up pass.

### 1. Sliders take effect immediately (regression from this phase)

Moving the sliders into the panel menus made them stop responding until the menu
closed, while the radio entries in the same menus kept responding at once. That is
a regression introduced by M6.4 — in the main bar no menu was open, so feedback
had been immediate. Under `dev-rules.md` §5 it was fixed rather than tracked.

**Why the two behaved differently.** The setters are identical; both persist and
invalidate for repaint. What differs is what a repaint can express. A display-mode
switch selects between images the producer already rasterised for every mode of
that capture. The gain, by contrast, is baked into those images at raster time, so
repainting redraws the same pre-gained pixels. Scaling the finished texture at
draw time is not an option either: both transfers hard-clip at the top (white,
band top), and a clipped texture cannot be brightened or darkened back to the
truth.

**How it is fixed.** On the GPU path a gain change re-runs the analysis on the
**resident capture render target** via `FTrueScopeViewExtension::ReanalyzeLastCapture`
— the path already shipped for "make a change visible while the viewport is not
ticking". No new capture, no readback, no retained bin buffers. The GPU bin
buffers are RDG-scoped and gone by the time a slider moves; retaining them was
weighed (+4.00 MiB VRAM, ColumnBins 3.75 MiB + VectorBins 0.25 MiB) and rejected,
not for the memory but for the retained-state lifetime it would add — the M4 bugs
were of that family, and re-deriving from the frame we still hold has one source
of truth.

On the CPU path the intensity arrays are rebuilt from the retained counts.
**The CPU histogram is deliberately untouched:** it already reads the gain at
paint time and was immediate before this fix. Routing it through the more
expensive path for uniformity's sake would cost work and gain nothing. This is
why the regression covered three scopes on CPU and four on GPU.

**That exemption needed a guard, because the redraw latch is global.** There is one
latch for all four panels, so it also fires when the *histogram* gain moves — and on
the CPU path that request has nothing to do: the histogram maps counts to curve
height at paint time, and the widget's own `Invalidate` has already redrawn it.
Without a guard the re-map would rebuild the three density images at unchanged gains
and repaint an identical picture for ~2–5 ms on every frame of a histogram drag
(measured on the M1, 2026-08-22). `FTrueScopeAnalysisResult::AppliedDensityGains`
records the gains the retained images were built at; `RemapLastResultWithCurrentGains`
compares it against the current gains and returns `NoData` when nothing it controls
has moved. So the exemption is not "the histogram is skipped" but "the CPU re-map
runs only when a gain it actually owns has changed" — the histogram simply never
owns one. The comparison is against the *applied* gains rather than a dirty flag
because the same result object can also be replaced by a fresh capture mid-drag,
which republishes at the current gains and must not leave a stale flag behind.

**Three conditions on the GPU route**, because a drag can fire at frame rate while
normal cadence is 1–5 Hz — roughly a twelve- to twentyfold load increase, on the
deliberately weak reference machine: at most one re-analysis per rendered frame
(events within a frame coalesce, last value wins); the call never blocks; and the
cost was measured on the M1 Mac Mini, not on the RTX machine.

**Measured on the M1 Mac Mini, 2026-08-22.** Four runs in one session, one scene
throughout — `/Game/Maps/StartMap`, whose never-still Niagara actor keeps "live"
distinguishable from "frozen". Conditions: Scalability Epic (all eight `sg.*Quality`
= 3, `sg.ResolutionQuality` 100), `r.ScreenPercentage.Default` 100, `t.MaxFPS` 0,
editor VSync off, High DPI off, "Use less CPU in Background" off — all verified
from the live CVars, because the FPSChart header reports scalability from
`UGameUserSettings` and is meaningless in-editor (`dev-rules.md`). Each analysis
path was selected deliberately via `TrueScope.AnalysisPath` and confirmed from the
panel footer, never left to the persisted ini. **The drag was performed by a human
hand** — synthetic input cannot drive a Slate slider at all (`dev-rules.md`; this
was first read as a consequence of High DPI being off, and later disproved — it
behaves identically with High DPI on) — sweeping Trace Brightness end to end
continuously at 3–5 s per
one-way pass, button held for the whole window. The hand on the slider *is* the
load under test, so it is a condition of the measurement, not extraneous
interaction.

| | idle (90 s) | dragging | multiple |
|---|---|---|---|
| **GPU path** | 28.86 FPS · 34.65 ms · GT 6.90 / RT 34.10 / GPU 32.97 ms | 39.65 FPS · 25.23 ms · GT 17.87 / RT 0.04 / GPU 5.34 ms | **0.73×** |
| **CPU path** | 32.73 FPS · 30.55 ms · GT 6.71 / RT 30.04 / GPU 28.49 ms | 54.00 FPS · 18.52 ms · GT 5.39 / RT 0.04 / GPU 4.14 ms | **0.61×** |

Drag windows were 99.03 s (GPU, 3926 frames) and 93.55 s (CPU, 5052 frames),
trimmed to the interval where gain changes actually occurred so that neither
party's reaction time is measured. The pass criterion — fixed before any number
existed — was ≤1.5× the same path's own idle frame time, with an absolute floor at
15 FPS. **Neither line binds: both paths came in below 1.0×, at 39.65 and 54.00
FPS.** Condition 3 is met on both paths and A is affordable.

**Dragging is faster than idle, and the reason matters.** An open menu freezes the
level viewport, so the 3D scene is not rendered during a drag at all: the render
thread and GPU costs collapse (RT 34.10 → 0.04 ms, GPU 32.97 → 5.34 ms) and the
re-analysis lands in frames that are otherwise nearly empty. That is not an
artefact of the test — it is the condition every real drag happens in, because the
slider exists only inside a menu. But it does mean the multiple understates A's own
price: what it measures is the user's actual experience, not the isolated cost of a
re-analysis. The isolated cost shows up on the game thread instead — GPU
re-derivation adds ≈11 ms of game-thread time per frame at 17.9 re-analyses/s,
while the CPU re-map costs less game-thread time than idle analysis did.

**Coalescing, measured rather than asserted.** 1769 gain changes over 99.03 s
against 3926 rendered frames = **0.45 re-analyses per rendered frame** (CPU: 2034
over 93.55 s against 5052 frames = 0.40). It never exceeded one per frame, as the
once-per-ticker-tick latch guarantees structurally. 136 GPU log lines do share a
frame number with their predecessor, but those pairs are 21–107 ms apart (median
41 ms — one to four frame-times), so they are the logging frame counter stalling,
not two re-analyses inside one frame. **Zero captures occurred inside either drag
window**, which is the direct evidence that no new capture and no readback were
introduced.

*Limit of this measurement, stated so it is not mistaken for a worst case:* a human
sweep at 3–5 s per pass changed the value 17.9–21.7 times per second, roughly
45–55 % of the per-frame ceiling the coalescing latch would allow (the
redraw-only-when-the-value-changed guard suppresses the rest). A sweep fast enough
to change the value every frame would produce about twice this analysis load. The
margin against the 1.5× line was large enough that this was not pursued.

**Condition 2 (“never block”), answered by code inspection rather than by timing.**
Both routes start on the game thread and return without waiting for completion.
GPU: `RedrawFromLastCapture` (`check(IsInGameThread())`) does game-thread
bookkeeping — `CacheResources_GameThread`, whose `ResizeTarget` is bucketed and so
does not fire during a drag — then `ENQUEUE_RENDER_COMMAND` and returns; the RDG
graph is built and executed inside that command, on the render thread. CPU:
`RemapLastResultWithCurrentGains` takes the `bAnalysisInFlight` flag and hands the
work to `UE::Tasks::Launch`, returning `Started` immediately; the task publishes
under a short `FScopeLock` that the caller never holds. There is no
`FlushRenderingCommands`, no `FRenderCommandFence`, and no GPU-idle wait anywhere on
either path — the plugin's only `FlushRenderingCommands()` is in the analyzer's
shutdown, where it is correct.

*This leaves the game-thread rise unexplained, and it is deliberately left so.* GT
went 6.90 → 17.87 ms on the GPU path while the chart reported RT 0.04 ms, which is
what prompted the question. It is not a blocking call in TrueScope's code, since
there is none. The remaining candidates — frame-pacing waits accounted to the game
thread, or the chart mis-attributing render-thread work while the level viewport is
frozen — were **not** measured. *Hypothesis / unverified assumption;* it did not
affect the verdict, because the frame time and FPS that decide the criterion are
measured directly.

*Estimate, not a measurement — what a saturating drag would cost.* The measured load
was 17.9–21.7 value changes per second against a per-frame ceiling of roughly 40
(GPU) to 54 (CPU), so about half the theoretical maximum. Attributing the GPU path's
extra game-thread time to the re-analysis (17.87 − 6.90 = 10.97 ms per frame) and
doubling it gives 25.23 + 10.97 ≈ 36.20 ms, or **≈1.05× the 34.65 ms idle frame
time** — about 27.6 FPS. Still far below the 1.5× line and well above the 15 FPS
floor. This is arithmetic on the reported figures, **not** a measured result: it
assumes the cost is linear in the number of re-analyses and that nothing else
changes, neither of which was tested.

*This is an idle-vs-dragging measurement, **not** the tab-closed-vs-tab-open A/B of
`dev-rules.md` "Benchmark methodology".* Both baselines here have the tab open with
analysis running at ~5.1 Hz, so they are not zero points and must never be compared
against the M5 figures.

*Reported as measured, but not to be built upon:* RT 0.04 ms is not credible as a
literal render-thread cost while Slate is still drawing. Treat the drag-window RT
and GPU figures as indicative and the frame time and FPS as the result.

**Showing the previous capture while a menu is open is correct, not merely
tolerated, and there is deliberately no "stale" indicator.** While any Slate menu
is open anywhere in the editor the level viewport does not tick, so no fresh
capture could arrive in any case — and the image the user is comparing against is
frozen at the same moment. Scope and viewport stay consistent, so WYSIWYG holds.
Setting a trace brightness and precisely measuring a live viewport are not
simultaneous activities.

### 2. Uniform text column in the menus

Slider rows were indented further than radio and checkbox rows, and the amount
differed between menus depending on which entry types a menu contained. All rows
in all four panel menus and the settings popup now share one text column.

### 3. Right-click menu removed

M6.4 kept the scope-canvas right-click menu as a temporary second entry point
while the panel header was new. With the header menu confirmed working and
identical, right-click was removed. **Right-clicking a scope canvas now does
nothing, deliberately** — the panel header is the single entry point. The menu
builder is unchanged and now has one caller.

### 4. Menu polish (found in the human walkthrough)

Two presentation defects introduced by the uniform-text-column work, plus a label
correction.

**The reset icon came back.** It was present in M6.4 and lost while the text
column was straightened. It is not decoration: the reset row is the only *action*
row in menus otherwise built of *state* rows — a radio dot and a checkbox report
how things stand, the arrow says what happens on click — and the same artwork
appears throughout the editor (details panel, viewport show-flags), so it is
recognised before the words are read. Icon, radio dot and checkbox share one left
column; all row text starts at one common x.

**Even vertical rhythm.** Slider rows sat closer to the row above them than other
row pairs did. Rows within a group now share one gap, in every panel menu and the
settings popup; section headings and the pre-reset separator keep their larger
spacing.

**Row pitch differs between menus on purpose.** The *gap* between rows is
identical everywhere; the *row height* follows the tallest control in that menu,
because `SLinkedBox` sizes every row to the tallest check widget present. The
vectorscope menu contains a check box ("Show Skin Tone Line") and runs at 48
units per row; the other three contain only radio dots and run at 44. Each menu
is internally even (48/48/48/48, 44/44/44), and two menus are never open at once,
so the difference is not observable in use. Equalising it would mean overriding
row heights against the framework for four pixels nobody sees. Deliberately left
as it is.

**"Reset to default" → "Reset to Defaults".** Unreal uses the singular where one
value is reset and the plural where several are; this entry resets several at
once. Human decision, verified against the engine by the human.

#### How both were reached, since neither is what the engine does by default

*The icon.* An entry's `FSlateIcon` is drawn in a box of its own **after** the
checkmark column (`SMenuEntryBlock.cpp:581-600`), so an icon on one row pushes
that row's label `Menu.MenuIconSize + 10` = 24 units right of every other row —
exactly the misalignment Task 2 exists to prevent. The engine's own answer to a
mixed section is "give every row an icon" (`SMenuEntryBlock.cpp:512-521`, the
all-or-none menu policy, which paints a magenta block over an offender when
`DisplayMultiboxHooks` is on); that is not available here, because the other rows
already say their state with a dot or a check box. So the arrow is drawn **in the
checkmark column instead of the icon column**: the row supplies its own contents,
and the icon is pulled left by `column + 10` units — the 8 and the 2 that the
engine puts between the column and the contents — to land on the column's left
edge. This row's own check widget is collapsed (a plain action has nothing to
check), so the space it reserves is empty and nothing is overlapped. The label
still lands on the shared column because the icon slot is given a total width of
exactly the 2 units a normal label slot indents by.

*The rhythm.* Horizontally the two row kinds differ in their indent; vertically
they differ in a button. An entry row is an `SMenuEntryButton`, and `Menu.Button`
carries `NormalPadding = FMargin(0, 2)` (`StarshipCoreStyle.cpp:2622-2629`), so
its content is inset 2 above and below on top of `Menu.Block.Padding`'s 1. A
slider row has no button and gets only the block padding's 1, so an entry→entry
gap came to 1+2+2+1 = 6 while entry→slider came to 1+2+0+1 = **4**. TrueScope's
menu style now widens `Menu.Block.IndentedPadding` vertically by exactly that
button's own `NormalPadding`, read from the style set at build time — the amount
the engine itself chose for entry rows, not a tuned constant. Left and Right are
untouched, since the 46-unit text column is derived from `IndentedPadding.Left`.
Headings (`Menu.Heading.Padding`) and the pre-reset separator
(`Menu.Separator.Padding`) are separate margins and keep their larger spacing.

*The menu with no column at all.* The parade menu holds neither a radio nor a
check box, so its checkmark column is 0 wide. Drawing the arrow "in the column"
there put it 34 units in, where its 14-unit extent ran into the label at 46 — the
arrow touching the R of "Reset to Defaults". No measurement caught it and none
could have: the checks compare rows against each other, and a menu with one entry
row has no second row to disagree with. The screenshot caught it. The pull is now
bounded so the icon never comes closer to the text than the 8 units the engine
itself leaves between check column and contents (`SMenuEntryBlock.cpp:632`), which
for a radio column coincides with the column at 20 units, so only the column-less
menu moves. The bound is guarded: with it removed the parade row measures 12 units
of clearance where 22 are required, and the test says so by name.

*How each is pinned.* `TrueScope.UI.MenuTextColumn` lays the five menus out for
real and reads the arranged geometry twice over: the label column, asserted equal
across **all** menus, and the glyph column, asserted equal **within** each menu —
the reset arrow lands where that menu's dots and boxes land, at 24 units in the
waveform and histogram menus, 14 in the vectorscope, 34 in the parade menu, each
being that menu's own `Menu.Block.Padding.Left + 2`.

The rhythm is pinned structurally instead, by `TrueScope.UI.MenuRowPadding`, and
that is a deliberate choice rather than a shortcut. Geometry cannot answer the
question: every row of a menu is as tall as that menu's shared checkmark column,
so a row holding a smaller glyph reports its content sitting further from the row
edge while being spaced exactly like its neighbours. Measured on 2026-08-22, the
vectorscope's radio rows report an inset of 4.5 against 3.0 everywhere else, for
precisely that reason — its column is sized by a check box while a radio dot is
smaller — and no measurement of the built tree distinguishes that from a real
spacing defect. So the test asserts the **padding**, in the engine's own terms
(a slider row reserves its block padding plus `Menu.Button`'s `NormalPadding`,
above and below), which is what the fix sets and what the eye reads as the gap.
The insets are still logged by the column test, marked as diagnostics, with that
reasoning next to them so the 4.5 is not later read as a bug.

Teeth proven on 2026-08-22, each half separately: moving the label indent reddens
the label column alone, moving the icon pull reddens the glyph column alone, and
reverting the padding override reddens `MenuRowPadding` alone.

## Status

**M6.4 and M6.4.1 are complete and signed off (2026-08-22).** Code complete and
green at **52/52**.

**Test counts, step by step.** The count is the stale-code tripwire, so every
change to it is recorded: a number lower than expected means stale code, not fewer
tests.

| Point | Count | What changed |
|---|---|---|
| Close of M6.3 | 40 | — |
| Close of M6.4 | 46 | +1 Task 2 (`PanelStateIndependence`), +2 Task 4, +2 Task 5, +1 Task 6 (`PanelResetToDefault`). Tasks 7 and 8 extended existing tests rather than adding new ones. |
| M6.4.1 Task 1 | 49 | +3: `TrueScope.UI.GainRedrawRequest`, `TrueScope.Analysis.GainRemapWithoutCapture`, `TrueScope.UI.RedrawRequestCoalescing`. |
| M6.4.1 Task 2 | 50 | +1: `TrueScope.UI.MenuTextColumn`. |
| M6.4.1 Task 3 | 50 | ±0: `PanelMenuEntryPoints` had two entry points to compare and one was removed, so it was repurposed into `TrueScope.UI.PanelMenuSingleEntryPoint` rather than left passing for the wrong reason. |
| CPU re-map guard | 51 | +1: `TrueScope.Analysis.HistogramGainSkipsCpuRemap`. |
| Work order 18 | **52** | +1: `TrueScope.UI.MenuRowPadding`. The label rename and the parade clearance guard changed existing tests instead of adding new ones. |

52 is the tripwire from here on.

The click-through was walked on 2026-08-22 with
the editor on the GPU path, panels at defaults, High DPI on. Five of the six steps
were driven through MCP/synthetic input and are recorded below; the two checks that
need a *dragged* Slate control are open for the human, for the reason given at the
end.

1. **Window → TrueScope carries the icon — yes.** The row is
   `generic "TrueScope"` with a 14 × 14 image in the same icon column as its
   neighbours (`Place Actors`, `Variant Manager`), and it renders as a distinct
   circled "T", not a blank slot. Screenshot taken.
2. **Each panel's hamburger — menu opens, rows are exactly the panel's type,
   radio/checkbox controls act.** Row inventory, by diffing each open menu against a
   closed-menu baseline:
   - Waveform — DISPLAY · RGB · Luma · Trace Brightness · Reset to Defaults
   - Parade — Trace Brightness · Reset to Defaults (no DISPLAY section; parade is
     always RGB)
   - Vectorscope — ZOOM · Normal · 2× Zoom · MARKERS · 75% · 100% · 75% + 100% ·
     Show Skin Tone Line · Trace Brightness · Reset to Defaults
   - Histogram — DISPLAY · Separate Channels RGB · Separate Channels YRGB ·
     RGB Overlay · Histogram Gain · Reset to Defaults

   Every radio was clicked and re-read from a reopened menu: selection moves to the
   clicked row and off its siblings, and clicking an already-selected radio leaves it
   selected. `Show Skin Tone Line` toggled both ways. All rows report their text at
   the same x in all four menus (slate 1988), which is Task 2's uniform column
   observed rather than assumed. **The two sliders are the open item** — see below.
3. **Right-click opens nothing — confirmed on all four canvases.** Right-clicking
   each of waveform, parade, vectorscope and histogram added zero popup windows and
   produced zero menu rows, against a baseline taken with nothing open.
4. **"Reset to Defaults" resets only its own panel.** With the vectorscope
   deliberately off-default (`bShowSkinToneLine=False`) and the waveform's
   `TraceBrightness` at 0.125, "Reset to Defaults" in the waveform menu took the
   waveform to 1.000 while the vectorscope stayed off-default. Read from
   `EditorPerProjectUserSettings.ini`, not from appearance.
5. **The gear — both sections present, the switch reaches all four scopes, both
   settings survive a restart.** The popup carries `ANALYSIS PATH (ALL SCOPES)`
   (GPU (performance) / CPU (compatibility)) and `CAPTURE RATE` (1 Hz / 2,5 Hz /
   5 Hz). Switching to CPU flipped `bUseGpuAnalysis` to False, the footer to
   "CPU path", and the log to CPU-path cycles carrying a ~27.7 ms staging copy — the
   readback that exists only on that path. All four scopes were screenshotted on each
   path and all four draw on both. Restart survival: the editor had been restarted by
   the human before this session, and came back on the GPU path with
   `MeasurementRateHz=5.0` — both settings as left.
6. **Panel-too-small hint — open for the human.** The threshold is
   `MinReadableCanvasWidth` 204.0 / `MinReadableCanvasHeight` 116.0
   (`TrueScopeDraw.h`), and the hint text is "Panel too small"
   (`STrueScopeWidget.cpp:72`).

### Why two checks are left for the human

`Trace Brightness` / `Histogram Gain` (step 2) and the splitter drag (step 6) are
the two controls that require a *held* drag, and synthetic input cannot drive them.
This was established here by a clean control experiment rather than inferred: a
Quartz click at the slider track produced no change, while a click 30 px below it,
computed with the *same* coordinate mapping, fired "Reset to Defaults" and moved the
value 0.125 → 1.000. So the coordinates were right and the widget is simply not
reached. A press-and-drag along the track, and keyboard navigation into the menu
followed by arrow keys, both left the value untouched as well; a drag on the panel
splitter likewise left the layout byte-identical. Three routes, same outcome.

Note this corrects the condition recorded during the benchmark: it is **not**
specific to High DPI being off. High DPI was **on** for all of the above — menus
opened, rows clicked, radios flipped — and the drag-capture widgets were still
unreachable. Menus and buttons are drivable; sliders and splitters are not.

Both were then done by the human, in one sitting, and both passed:

- Each panel menu opened and **Trace Brightness** (waveform, parade, vectorscope)
  and **Histogram Gain** (histogram) dragged: the trace changes *continuously
  during the drag*, on both analysis paths.
- A panel dragged below 204 × 116 px of canvas: "Panel too small" appears.

That closes the list. The same walkthrough was the final visual pass, and it found
**no functional fault** — the two things it did find were presentation defects, and
they are the subject of addendum item 4.

## Verification summary — M6.4.1

Covers the immediate-slider-feedback fix, the menu work, and the menu polish of
work order 18. Signed off 2026-08-22 together with M6.4.

### Acceptance

*Sliders take effect immediately (work order 17 Task 1, restated by its addendum):*

- [x] Dragging Trace Brightness changes the trace continuously in waveform, parade
      and vectorscope, and Histogram Gain the histogram — human drag, both paths.
- [x] Verified on **both** analysis paths, each selected deliberately via
      `TrueScope.AnalysisPath` and confirmed from the panel footer, never left to
      the persisted ini.
- [x] No new capture and no readback introduced — GPU re-analysis runs on the
      resident capture render target; the only staging copy in the logs is the one
      that exists on the CPU path anyway.
- [x] Coalescing: at most one re-analysis per rendered frame, pinned by
      `TrueScope.UI.RedrawRequestCoalescing`.
- [x] Condition 2 — the call never blocks. Answered by code inspection; both routes
      start on the game thread and return without waiting.
- [x] Condition 3 — measured on the M1 Mac Mini, not the RTX machine.
- [x] Double-click reset and "Reset to Defaults" are immediate by the same route.
- [x] The persisted value is still the factor, never the slider position.
- [x] A test pins that a gain change alone re-renders without a new capture; teeth
      proven.

*Menus (work order 17 Tasks 2–3, work order 18 Tasks 1–3):*

- [x] Every row of every panel menu and the settings popup starts in one common
      text column — asserted across all five menus by `TrueScope.UI.MenuTextColumn`
      and seen in a screenshot of each.
- [x] Right-clicking a scope canvas opens nothing — confirmed on all four canvases
      against a baseline with nothing open.
- [x] The reset icon is present in all four panel menus and shares the left column
      with the radio dots and check boxes.
- [x] No row is clipped or crowded — the one case where this failed (parade) is
      recorded below and is now guarded.
- [x] Rows within a group share one vertical gap in all five menus; headings and
      the pre-reset separator keep their larger spacing.
- [x] All four menus read "Reset to Defaults"; no stale wording in code or docs.

### What was automated

Everything that is a fact about the widget tree, the settings store, or a
derivation:

- Panel-menu and settings-popup **contents read out of the built menu widgets**,
  not described in a comment — `TrueScope.UI.PanelMenuContents`,
  `TrueScope.UI.SettingsPopupContents`, `TrueScope.UI.PanelMenuSingleEntryPoint`.
- Per-panel state: `TrueScope.UI.PanelStateIndependence`, and
  `TrueScope.UI.PanelResetToDefault` for reset scoping.
- Menu geometry: the label column asserted equal across all five menus and the
  glyph column asserted equal within each menu, both in
  `TrueScope.UI.MenuTextColumn`, plus the clearance guard that catches an icon
  crowding its own label.
- Menu rhythm: `TrueScope.UI.MenuRowPadding`, structurally rather than
  geometrically, for the reason given under "How both were reached".
- `TrueScope.UI.PanelSizeHint`, which re-measures both font metrics and lays the
  hint out rather than trusting a threshold constant.
- The settings popup's radio state following the **effective** analysis path in
  both directions — `TrueScope.UI.SettingsPopupEffectivePath`.
- Histogram/waveform level equality — `TrueScope.UI.ValueScale`.

### What was verified live, and how

- **MCP, `Click` + `PressKey "Enter"`** (2026-08-22): all four panel menus and the
  settings popup opened and screenshotted with no human input — row inventories,
  every radio clicked and re-read from a reopened menu, the skin-tone checkbox
  toggled both ways, right-click confirmed inert on all four canvases, reset
  confirmed to scope to its own panel by reading
  `EditorPerProjectUserSettings.ini` rather than appearance. This is what
  superseded the old "MCP cannot open Slate popup menus" rule.
- **The human's own walkthrough**, for what synthetic input cannot reach: the
  held-drag controls — the four gain sliders and the panel splitter — and the
  final visual pass over the whole UI. It found **no functional fault**.

### What was measured

Four benchmark runs on the M1 Mac Mini (idle and dragging, on each analysis path),
under the fixed benchmark editor state, with the three-phase handshake. The pass
criterion was fixed **before any number existed**: ≤1.5× the same path's own idle
frame time, with an absolute floor at 15 FPS.

| | multiple | verdict |
|---|---|---|
| GPU path | **0.73×** (28.86 → 39.65 FPS) | passes, by a wide margin |
| CPU path | **0.61×** (32.73 → 54.00 FPS) | passes, by a wide margin |

**The caveat the numbers depend on:** dragging measures *faster* than idle because
an open menu freezes the level viewport, so the 3D scene is not rendered during a
drag and the re-analysis lands in frames that are otherwise nearly empty. That is
not an artefact of the test — it is the condition every real drag happens in — but
it does mean these figures cannot be read as "re-analysis is free". They also are
**not** the tab-closed-vs-tab-open A/B of `dev-rules.md`: both baselines here have
the tab open and analysis running, so neither is a zero point, and they must never
be compared against the M5 figures. The per-thread split inside the drag window is
reported as indicative only and is carried to M7 as an open question
(`backlog.md`).

### What only a human eye could catch

Both presentation defects fixed in work order 18 were found by the human's
walkthrough, and neither could have been caught by the checks that existed — which
is a standing lesson about the limits of geometry assertions, not a gap that was
later closed by adding more of them:

- **The lost reset icon.** Every assertion was about *alignment*, and the menus
  were perfectly aligned without the icon. Nothing asserted that the icon was
  there at all, because its absence broke no relationship between rows.
- **The parade menu's glyph clearance.** The checks compare rows against each
  other. The parade menu has a single entry row, so there is no second row for it
  to disagree with, and its arrow could touch the R of "Reset to Defaults" while
  every assertion stayed green. A screenshot caught it in one look.

Both are now guarded — the icon by the per-menu glyph column, the clearance by an
explicit minimum-gap assertion — but the guards were written *after* the eye found
the defects, which is the point.

### Commits

`43d15b1` (immediate sliders), `b363208` (uniform text column), `f9e73e6`
(right-click removed), `0fb8608` (M6.4.1 documentation), `b715da3` (CPU re-map
guard), `cc33ea9` (drag benchmark), `9ceb4f6` (condition 2 and the frozen-viewport
caveat), `f74e640` (click-through results), `b162423` (reset icon and row rhythm),
`4d93651` (parade clearance bound), plus this closing documentation commit.
