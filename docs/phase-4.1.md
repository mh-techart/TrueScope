# Phase 4.1 — Paused indicator when the measured viewport enters PIE

Read `architecture.md` first ("Behaviour during PIE" and "No automatic
fallback"). This is a minimal UI phase between M4 and M5. It adds **one thing**:
a visible indication that the scopes are paused because the measured viewport is
in Play mode, and that the user can resume by focusing another viewport. No
scope, no analysis, no GPU work, no settings, no change to capture behaviour, and
**no automatic viewport fallback** (a deliberate decision — see architecture.md).

**Why this exists, and why it is small.** Investigation established there is no
capture wedge: the selection logic measures the last valid non-PIE viewport and
never adopts a PIE viewport as a source. The scopes hold their last frame in
exactly one case — the measured viewport itself enters PIE and drops out as a
source, with no auto-fallback (intended). That held frame is useful (the lighting
baseline stays visible), but it is silent and can be mistaken for a freeze. This
phase closes exactly that gap.

If a decision-level surprise appears, stop and ask per the `dev-rules.md`
Rule 3 self-check.

## Task 0 — Confirm the ground truth before drawing anything

- **Confirm the behaviour** as the test baseline, using a live change-source (a
  moving Niagara effect, viewport Realtime on) so "held" is distinguishable from
  "still updating but static": measured viewport enters PIE -> scopes freeze;
  Stop PIE -> resume; focus another valid viewport -> resume. Reproduce across at
  least in-viewport and separate-window PIE.
- **Identify the paused state internally:** determine how the selection logic can
  detect "the measured source became invalid because its viewport entered PIE"
  and hold the last frame. Read how the current selection
  (`GCurrentLevelEditingViewportClient` / `IsActiveThisFrame`) classifies this;
  do not assume.
- **Distinguish from "no viewport":** determine whether this paused state is
  distinguishable from the generic "no level-editor viewport focused at all"
  (M2's no-viewport state). This decides whether the indicator is one message or
  two.
- **Existing empty-state rendering:** find what the scope widgets currently draw
  when there is no capture (is there already a centered message on the widget, or
  is it blank?). If a message mechanism exists, the paused indicator is a second
  message on it; if not, build a minimal one. Record which.

## Task 1 — Minimal centered indicator on the base scope widget

- Add (or reuse) a **fixed-size, centered text hint** on the base scope widget
  (`STrueScopeWidget` / the shared base the four scopes derive from), shown over
  the held last frame when in the paused state. Fixed-size UI element, legible at
  any panel size, same discipline as the axis labels. The held scope values stay
  visible behind/around the hint — they are useful, not to be blanked.
- Wire up the **paused case**: measured viewport entered PIE, last frame held ->
  show the hint. The hint communicates three things: (1) paused, (2) because the
  measured viewport is in Play (PIE), (3) focus another viewport to resume.
  Suggested text: **"Paused — measured viewport is in Play (PIE). Focus another
  viewport to resume."** Single localized `LOCTEXT`; a two-line variant is fine
  if a narrow panel needs it. Final wording may be refined with the M6 status
  line.
- **Word message (3) generically** — "focus another viewport", not "focus a
  level viewport" or similar. Whether the next viewport is scope-suitable (not
  Unlit/Wireframe) is the user's call; the hint promises *resume*, not *resume
  with a meaningful image*. This matches the no-fallback decision: the choice
  stays with the user.
- If Task 0 found the paused state distinguishable from "no viewport", keep the
  two messages distinct; if not, one clear message covering both is acceptable
  for now — note the limitation.
- **Forward-compatibility (state it, do not build it):** this in-panel hint is
  **not** the M6 status line (a persistent bar showing *what* is being measured);
  the two coexist. The same base-widget hint mechanism is also what the backlog
  "minimum panel size" hint will use later. **Do not build the minimum-panel-size
  hint in this phase** — only the paused case. Staying in phase.

## Regression test

A widget-level test asserting the base scope widget shows the paused hint in the
paused state and the live scope otherwise — proven to have teeth by crossing the
states (assert it fails if the paused branch is bypassed). This sits at the
widget/state layer, not `AnalyzePixels`.

## Acceptance criteria

- [x] Builds via the Build.sh command (see host `CLAUDE.md`) with no new
      warnings.
- [x] When the measured viewport enters PIE, every scope panel shows the paused
      hint over the held last frame (values still visible) — verified via MCP
      screenshot in in-viewport and floating-window ("New Editor Window") PIE.
      (The variants cannot differ: any PIE start disables realtime on *all*
      editor viewports — see the Verification summary — so the trigger does not
      depend on which window hosts the session.)
- [x] Stopping PIE, and focusing another valid viewport, both resume live scopes
      with no hint — Stop PIE verified live via MCP; the focus-another-viewport
      resume is covered by the regression test and by the human's earlier manual
      observation (work order 4).
- [x] Focusing a PIE viewport while another valid viewport exists does **not**
      show the hint (that source keeps updating) — guarding against a too-broad
      trigger. (Verified by unit test, not live: a live check needs a second
      level viewport, i.e. a change to the human's saved layout. The human
      accepts the unit-test coverage plus the earlier manual observation; a live
      multi-viewport check is not required.)
- [x] The "no viewport" state still behaves as before (its own message or blank,
      per Task 0) — no regression. Without a play session the state is never
      paused, so the idle scope keeps its existing silent presentation.
- [x] Regression test present and proven to have teeth; full suite green, count
      checked against expectation.
- [x] No new TrueScope warnings/errors in the Output Log.
- [x] Human sign-off.

## Explicitly out of scope (do not build)

Automatic viewport fallback when the source falls away (deliberately rejected —
see architecture.md); any readback timeout/reset (no hang exists — backlog
robustness net tied to M5); actually *measuring* the PIE image; the M6 status
line; the minimum-panel-size hint (backlog); any settings UI. Ideas ->
`docs/backlog.md`.

## Verification summary

**Task 0, from the 5.8 engine source rather than assumption.** Two findings
explain the whole behaviour. A PIE viewport is never adopted as a source because
in-viewport PIE creates a *new* `FSceneViewport` whose client is the
`UGameViewportClient` (`SLevelViewport::StartPlayInEditorSession`), so the M2
check `Context.Viewport->GetClient() != GCurrentLevelEditingViewportClient`
rejects it. And all PIE variants behave identically because starting a play
session disables realtime on **every** editor viewport (`PlayLevel.cpp`: "Can't
allow realtime viewports whilst in PIE so disable it for ALL viewports here") —
so the measured viewport silently stops delivering frames no matter which window
hosts the session. There was no existing empty-state message mechanism: the
scopes are `SLeafWidget`s that simply drew an empty graticule.

**Paused is modelled as a conjunction:** a play session is running **and**
results have stopped arriving for more than three capture intervals. Neither
half suffices. PIE alone is too broad — a viewport the user focuses keeps
delivering during PIE, and while results arrive the scopes are live, so no hint
may appear. A stall alone is too broad the other way — without PIE it is the
ordinary "no viewport" idle state, which keeps its existing silent presentation
and is therefore distinct. Simulate is deliberately excluded: it drives runtime
objects while the scopes keep measuring.

**Mechanism:** `TrueScope::Draw::DrawCenteredHint` (fixed-size, centered,
translucent plate, drawn *over* the held values, silent if it would not fit) plus
`STrueScopeWidget::DrawPausedHint`, which every scope's `OnPaint` exits through —
including the no-result and too-small-plot paths, so the hint cannot be skipped.
This base-widget hint mechanism is what the backlog's minimum-panel-size hint and
the ortho-view hint will reuse; it is **not** the M6 status line, and the two
coexist.

**Self-verified:** clean build (0 warnings); MCP screenshots showing the hint on
all four scopes over still-visible held traces in in-viewport PIE and in
floating-window ("New Editor Window") PIE, and its disappearance with live scopes
after Stop PIE in both cases; no TrueScope warnings or errors in the Output Log.
The regression test `TrueScope.UI.PausedState` was proven to have teeth by two
*opposite* mutations: a too-broad trigger (paused whenever PIE runs) failed the
"another viewport still delivers" assertions, and a never-paused trigger (the
silent freeze this phase exists to cure) failed both the paused-state and the
paint assertions — the latter proving the draw branch really executes. Reverted,
rebuilt, suite green: **18/18** (17 before + `PausedState`).

Incidental finding, recorded not built (staying in phase): an **orthographic**
level viewport produces no capture at all, so the scopes stay blank with no
explanation. That is not a paused state (no play session), so the hint correctly
stays silent. It is a deferred UX gap of the same class as this phase's, and
belongs to the M6 status line — see `backlog.md`.

**Phase 4.1 signed off by the human as complete.**
