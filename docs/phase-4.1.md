# Phase 4.1 — Fix the PIE-window capture wedge

Read `architecture.md` first (the "Image source" settled decision and the M2
capture chain), and `backlog.md` → "PIE in a separate editor window". This is a
small, self-contained fix phase between M4 and M5. It adds **no scope, no
analysis, no GPU work** — it makes the M2 capture pipeline robust against
viewport/world lifecycle transitions so it can never wedge.

**Why now, before M5:** the pre-M5 readback experiment found that starting PIE
as a **separate editor window** ("New Editor Window (PIE)") freezes the scopes
the instant PIE starts, and they do not recover on stop, refocus, or viewport
interaction — only an editor restart brings capture back. That affects users who
never intended to measure PIE at all, so it is a robustness bug, not a missing
feature. The M5 GPU path builds on exactly this capture chain; the pipeline must
be sound before more is layered on it.

If a decision-level surprise appears, stop and ask per the `dev-rules.md`
Rule 3 self-check.

## Goal

The capture pipeline must **never wedge**. Starting and stopping PIE in a
separate editor window, entering and leaving PIE in-viewport, and losing or
regaining the target viewport must all leave the scopes in one of two valid
states — measuring a valid viewport, or cleanly idle — and never permanently
frozen requiring an editor restart.

This phase does **not** make the scopes *measure* the separate-window PIE image
(that stays the backlog feature). It only guarantees the pipeline recovers.

## Task 0 — Research first (mandatory): reproduce and diagnose

Do not fix from the current hypothesis alone. First reproduce the wedge and
establish its true mechanism:

- **Reproduce:** TrueScope tab open and capturing; start PIE as "New Editor
  Window"; observe the scopes freeze; confirm they stay frozen after Stop PIE,
  on refocusing the level viewport, and on interacting with it. (This much was
  seen in the experiment; reproduce it deliberately as the test baseline.)
- **Diagnose:** confirm whether `bReadbackInFlight` (or the equivalent
  single-buffer "skip-if-busy" state in `FTrueScopeAnalyzer`) is stuck because a
  readback was enqueued for a view/RDG that never completes once PIE takes over
  the render path — or whether the cause is elsewhere. Instrument the state
  machine (which flag is stuck; when `PollReadback_RenderThread` last ran; does
  `IsReady()` ever return true again) rather than guessing.
- **Read the engine:** how does an `FSceneViewExtension` see world/PIE
  transitions, and how do the view family / RDG signal teardown when a world
  switches? Find where the never-completing readback's fence ends up. Record the
  true cause in a short note (commit or appended here) **before** writing the
  fix.

## Task 1 — Cure the class: give the readback state machine a recovery path

The single-buffer, skip-if-busy readback has no way out of a stuck in-flight
state: if a readback never completes, capture is frozen forever. Add a recovery
path — a **timeout** (a readback in flight longer than N capture intervals is
abandoned and the buffer reset) and/or an **explicit reset** on
world/PIE/viewport transition — so a never-completing readback cannot wedge
capture permanently. This cures the whole class of "in-flight readback never
lands", of which the PIE window is only one instance.

## Task 2 — Cure the symptom too: handle the separate-window PIE viewport

The separate-window PIE viewport is **not** a Level Editor viewport, so the M2
"last focused Level Editor viewport" selection should handle its appearance and
disappearance gracefully: either keep measuring a valid level viewport if one
remains, or drop to the clean "no viewport" idle state. **Decide which, and
record the decision** here and in `architecture.md`. Filtering the PIE view out
at the `IsActiveThisFrame` selection is the likely mechanism (it already filters
on `GCurrentLevelEditingViewportClient`); verify it actually prevents the wedge
rather than only hiding the symptom while the stuck readback remains.

## Regression test with teeth

A test that **fails on the reintroduced wedge and passes after the fix** — proven
to fail first (reintroduce the bug, watch it fail, revert), per the
build-and-test-hygiene rule. This is the hard part: the bug lives in the
lifecycle/state machine, not in `AnalyzePixels`, so the test must exercise the
readback state machine directly (e.g. drive it into an in-flight state whose
readback never completes, then assert the recovery path resets it and capture
resumes) rather than feeding pixels. Design the test against the layer where the
bug lives — no existing test caught this, precisely because they all sit at the
analysis layer.

## Acceptance criteria

- [ ] Builds via the Build.sh command (see host `CLAUDE.md`) with no new
      warnings.
- [ ] Starting and stopping separate-window PIE leaves the scopes either
      measuring a valid viewport or cleanly idle — never frozen. **No editor
      restart is ever required to recover capture.**
- [ ] The readback state machine provably recovers from a stuck in-flight
      readback (recovery path exercised and verified, not just observed once).
- [ ] In-viewport PIE and ordinary viewport focus changes still work as before —
      no regression in the M2/M3/M4 capture path.
- [ ] Regression test present and **proven to have teeth** (fails on the
      reintroduced wedge, passes after the fix); the full automation suite still
      green, count checked against expectation.
- [ ] Verified live via MCP: screenshot evidence of the scopes recovering after
      a separate-window PIE start/stop cycle.
- [ ] No new TrueScope warnings/errors in the Output Log.
- [ ] Human sign-off.

## Explicitly out of scope (do not build)

Actually **measuring** the separate-window PIE image (stays the backlog feature —
this phase only prevents the wedge); the GPU compute path and any M5+ work;
double-buffered readback as a performance/throughput feature (the recovery path
here is for correctness, not throughput); settings UI. Ideas →
`docs/backlog.md`.
