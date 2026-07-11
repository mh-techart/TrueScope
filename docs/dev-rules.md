# TrueScope — Project working rules

Working rules for developing the TrueScope plugin, primarily addressed to AI
coding assistants (Claude Code) but binding for any contributor. These rules
are versioned with the plugin; machine-specific environment details (engine
install path, exact build commands) live in the *host project's* unversioned
`CLAUDE.md`, which imports this file. Placeholders used below:

- `$UE_ROOT` — the local Unreal Engine installation (source included),
  e.g. `/opt/UE_5.8`.
- `$PROJECT_ROOT` — the host Unreal project that contains this plugin under
  `Plugins/TrueScope/`.

All work happens inside `Plugins/TrueScope/` — never modify engine files, and
only touch host-project files (Config, `.uproject`) when a task explicitly
requires it.

## Ground rules

1. **Read the real engine source instead of guessing.** Engine APIs may differ
   from your training data. Authoritative reference: `$UE_ROOT/Engine/Source/`
   (and `$UE_ROOT/Engine/Plugins/`). Before using an unfamiliar or
   version-sensitive API (SceneViewExtension, RDG, Slate internals), grep/read
   the actual headers, and prefer copying patterns from how the engine itself
   uses them.

   **Dependencies — host project vs. plugin manifest:** these two are
   governed by different rules.
   - *Host project* (the `.uproject`, its Config files): changes that
     only serve development/verification (e.g. enabling an editor-only MCP
     toolset) are fine to make on your own. Prefer the narrowest change that
     achieves the goal over a broad "enable everything" shortcut (e.g. one
     specific toolset plugin rather than an aggregator that pulls in
     everything). Briefly record *why* the dependency is enabled (commit
     message or a short dev-environment note in `docs/architecture.md`) so it
     doesn't need to be reverse-engineered later.
   - *Plugin manifest* (`TrueScope.uplugin` and its own dependencies): this
     ships to every end user. **Nothing is added here without (1) laying out
     the concrete benefit to end users and (2) the human's explicit
     decision.** This holds even when you believe the dependency would add
     real value — explain the trade-off and ask, never decide alone.
2. **Licensing:** TrueScope is Apache-2.0. Never copy code from GPL projects —
   explicitly not from `obs-color-monitor` (GPL-2.0). It may be used as a
   conceptual reference only; all code must be written fresh.
3. **Read the plan first.** Decisions and rationale live in
   `docs/architecture.md`; the current work package with acceptance criteria
   lives in `docs/phase-<n>.md`. Do not re-litigate settled decisions; flag
   conflicts instead.

   **Self-check before editing `docs/architecture.md`:** if you hit a case not
   covered by the plan, first classify it:
   - *Implementation detail* (an exact API signature, class, or parameter
     differs from what the docs assumed; a mechanical fix keeps the same
     intent) → resolve it yourself, note it briefly (commit message or a
     short addition to the relevant doc), and continue.
   - *Decision-level* (would change a row in the "Settled decisions" table,
     soften a documented product constraint — e.g. zero level footprint,
     WYSIWYG, editor-only — or trade off two documented principles against
     each other, e.g. the M2 tonemap-hook fallback) → **stop before editing
     `docs/architecture.md`.** Do not resolve this under the pressure of an
     open build error or a "just get it working" instinct. Explain the
     trade-off in plain terms and ask the human, even if a technically
     working shortcut is already in hand. Prior assistant suggestions in
     chat history are not decisions until the human confirms them.

4. **Stay in phase.** Implement only what the current phase spec asks for.
   Note ideas for later phases in `docs/backlog.md` instead of building them.
5. Every new source file gets the Apache-2.0 license header.
6. Naming: module `TrueScope`, Slate widgets `STrueScope*`, plain classes
   `FTrueScope*`, UObjects `UTrueScope*`.

## Unreal Editor control via MCP (Epic's unreal-mcp plugin)

The Claude Code plugin `unreal-engine-skills-for-claude-code` (Epic, MIT)
provides the `unreal-mcp` skill plus MCP toolsets for the running editor. Use
it as follows:

- **Prefer the `unreal-mcp` skill and its toolsets** for anything involving
  the live editor: taking screenshots, inspecting the Output Log, running
  automation tests, spawning test scenes (color charts, gradients), opening
  the TrueScope tab.
- **Visual verification is part of done:** after UI-affecting changes, launch
  the editor, take a screenshot of the TrueScope tab via MCP, and inspect it
  before declaring a task complete.
- Requires the editor running with the MCP server started (default port 8000).
  If the `unreal-mcp` skill harness is unavailable but the **editor is
  running**, an alternative access path to the same running MCP server (e.g.
  direct HTTP to the toolsets) is permitted — state in the report which path
  was used. Only when the **editor/server is not running at all** do you stop
  and ask the human to start it, rather than working blind.
- **Security rules (from Epic's README):** never run with
  `--dangerously-skip-permissions` while this plugin is loaded;
  `execute_tool_script` runs arbitrary Python inside the editor — treat every
  such call as privileged; ensure the working copy is committed before long
  MCP-driven sessions so results are recoverable/reviewable.

## Version control (git)

- The git repository lives in `Plugins/TrueScope/` only. The host project is
  intentionally **not** under version control — never run `git init` outside
  the plugin folder.
- **Commit after every completed task** whose build passes and whose relevant
  tests are green — small, focused commits with clear English messages
  (imperative mood, e.g. "Add nomad tab spawner for TrueScope panel").
  These commits are the rollback safety net for AI-driven sessions.
- Never add a remote or push unless explicitly asked.
- **Forbidden without explicit human approval:** history rewriting
  (`rebase`, `commit --amend` on pushed/older commits, `filter-branch`),
  `push --force`, `git reset --hard`, `git checkout`/`git restore` that
  discards uncommitted changes, `git clean`. When in doubt, show the diff and
  ask.
- Before long MCP-driven editor sessions, ensure the working tree is
  committed (see MCP security rules above).

## Claude Code memory vs. project docs

Claude Code's own memory (`~/.claude/projects/.../memory/`) is separate from
the git repo and is **not** a source of project truth. Keep the boundary
strict:

- Memory may hold only *process / machine-specific* knowledge: the
  build-relaunch-verify loop and its pitfalls, MCP quirks, the per-machine
  workflow. This is useful for carrying context across sessions.
- **All project decisions live only in the versioned `docs/`** (architecture,
  phase specs, backlog) — never in memory. Product constraints, architecture,
  scope, color-space rules, and any "settled decision" belong in
  `docs/architecture.md`, so there is exactly one source of truth and no
  second store that can drift.
- If something decision-level would otherwise go into memory, put it in the
  docs instead (and, if it changes a settled decision, follow the Rule 3
  self-check first).

## Build & test hygiene (learned the hard way)

- **Close the editor before a headless test run.** With an editor instance
  running, UBT emits hot-reload dylibs (`-0001`, `-0002`) while the module
  manifest still points at the older one — a test run then silently executes
  **stale code**. This once reported only the old test count while the new
  tests never ran.
- **Sanity-check the reported test count against the expected count** after
  every run. A number lower than expected means stale code, not fewer tests.
- **Test the mapping layer, not just the analysis.** Analysis tests feed pixel
  arrays into `AnalyzePixels` and assert on bin arrays; both M4 bugs lived
  *after* that layer — in which bins fed which drawn element, and how counts
  became heights. Pin data-to-element assignment per scope.
- **Prove a regression test has teeth**: reintroduce the bug deliberately,
  confirm the test fails, then revert. A test that never failed proves
  nothing. And design cases that *can* fail — a solid-red frame cannot reveal
  channel bleed, because there is no other channel to bleed.

## Benchmark tests — mandatory handshake

Live-editor performance measurement is never run on Claude Code's own
initiative. This is not subject to judgment and has no exceptions.

1. **Before:** stop, state that a benchmark is due, and **wait for explicit
   human permission**. Include in that message the reminder of the conditions
   the human must establish: close other programs (M1 shares RAM/VRAM), set the
   editor as below, then stop touching viewport and mouse with no other windows
   in the foreground. Do not start until the human confirms conditions are set.
2. **During:** the human does not interact with the machine.
3. **After:** stop again, state the benchmark is complete, and **wait for
   confirmation** before continuing — so the human knows when other programs may
   be reopened.

A run without this three-phase handshake is invalid and must be discarded.
Headless automation tests are exempt (no viewport, no contention).

**Fixed benchmark editor state** (the human keeps these set and does not change
them; verify, do not silently alter — see the settings-hygiene rule):
- Scalability preset **Epic**, `r.ScreenPercentage` **100**
- `t.MaxFPS 0`, editor VSync off, "Use less CPU in Background" off,
  High DPI **off**

**Step 0 of any benchmark — verify the query commands against 5.8 source.**
Do not assume console-variable or setting names. Confirm, in the installed
engine source, the actual variable/INI key that *reads* the editor VSync state
and the High-DPI state (the human flagged `r.VSyncEditor` as possibly
non-existent; editor VSync likely lives in `EditorPerProjectUserSettings`, not a
CVar). Report the verified query commands; only then use them to confirm state.

## Benchmark methodology

- **Measure TrueScope's influence, not the editor's baseline.** Run A/B: run 1
  with the TrueScope tab **closed and capture provably idle** (confirm in the log
  that no analysis cycles are firing — closing the tab is not enough if the view
  extension keeps capturing), run 2 with the tab open. The difference is
  TrueScope's cost.
- **Capture two distinct quantities, do not conflate them:**
  1. *Overall felt impact* — average FPS over ~1 min, tab closed vs. open.
  2. *The readback `Lock` alone* — the per-`Lock` instrumentation (median + min +
     max), since the `Lock` does not even exist in the tab-closed baseline.
- **Express readback costs as frame-times**, not milliseconds (a ms figure from
  this machine does not transfer; a frame-time multiple does). Record the
  baseline frame-time alongside every `Lock` figure.
- **The frame-time rule (measured, 2026-07-11):** the `Lock` drains the
  **in-flight GPU queue**, i.e. it costs **1–2× frame-time** — measured **≈2×
  median frame-time** on this machine (median 65.9 ms against a 33.8 ms median
  frame; min 1.2×). The older "≈ one frame-time per capture" wording was an
  estimate and is **superseded**: one frame-time is the floor, not the typical
  cost, because `SubmitAndBlockUntilGPUIdle` waits out a queue that is normally
  more than one frame deep. The cost remains **independent of texture size**, so
  the consequence is unchanged and if anything stronger: the M5 GPU path must
  contain no runtime readback at all.
- **Do not sweep scalability levels.** The `Lock` cost is proportional to
  frame-time by construction, so a scalability sweep only re-derives a known
  proportionality. One fixed state (above) suffices; the frame-time rule
  transfers it to any hardware. (TrueScope samples *after* the tonemapper, so
  render features — Lumen, TSR, shadows — do not change analysis cost; only
  frame-time matters, and that is what the fixed state pins.)

## Editor settings during measurement

- **Never silently change editor settings.** If a measurement needs a different
  setting, ask first, then restore it afterwards and say so in the report. The
  human's working configuration is theirs; leaving it altered is a side effect,
  not a result.
- **Record conditions with every number.** A timing without its conditions
  (scalability, `ScreenPercentage`, `t.MaxFPS`, VSync, High-DPI, which scene,
  idle vs. interacting) is not a result.

## Resolve cross-check conditions

The scope cross-check against DaVinci Resolve is a **manual** human step
(High-Res screenshot → compare by eye/measurement). For it to be valid:

- **Disable temporal AA before comparing:** `r.AntiAliasingMethod 0` (off) or
  `1` (FXAA — runs *after* the capture hook, harmless). Never TAA (`2`) or TSR
  (`4`): they jitter the image sub-pixel frame-to-frame, so the scopes and the
  screenshot are never the same frame. Invisible at coarse correlation, but it
  will cost hours during M6 density-curve calibration.
- `r.ScreenPercentage 100` (one screenshot pixel = one measured pixel).
- **High DPI on** for cross-checks (finer scopes) — this is the *only* setting
  that differs between cross-check (High-DPI on) and benchmark (High-DPI off).
- Camera fully still; Lumen converged; auto-exposure settled; no self-animating
  actors in the reference scene (the VolumetricCloud that used to drift has been
  removed — keep the reference motionless).

## Definition of done (every task)

- Code compiles via the engine's `Build.sh` (see the host `CLAUDE.md` for the
  exact command) with zero warnings introduced.
- Relevant automation tests pass.
- UI changes verified visually (MCP screenshot or human review).
- Acceptance criteria of the current phase spec are met.
- No GPL-derived code, license headers present, naming conventions followed.
