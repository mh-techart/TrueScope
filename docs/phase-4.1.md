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

- [ ] Builds via the Build.sh command (see host `CLAUDE.md`) with no new
      warnings.
- [ ] When the measured viewport enters PIE, every scope panel shows the paused
      hint over the held last frame (values still visible) — verified via MCP
      screenshot in in-viewport, undocked-window, and "New Editor Window" PIE.
- [ ] Stopping PIE, and focusing another valid viewport, both resume live scopes
      with no hint — verified live with a moving source.
- [ ] Focusing a PIE viewport while another valid viewport exists does **not**
      show the hint (that source keeps updating) — verified, guarding against a
      too-broad trigger.
- [ ] The "no viewport" state still behaves as before (its own message or blank,
      per Task 0) — no regression.
- [ ] Regression test present and proven to have teeth; full suite green, count
      checked against expectation.
- [ ] No new TrueScope warnings/errors in the Output Log.
- [ ] Human sign-off.

## Explicitly out of scope (do not build)

Automatic viewport fallback when the source falls away (deliberately rejected —
see architecture.md); any readback timeout/reset (no hang exists — backlog
robustness net tied to M5); actually *measuring* the PIE image; the M6 status
line; the minimum-panel-size hint (backlog); any settings UI. Ideas ->
`docs/backlog.md`.
