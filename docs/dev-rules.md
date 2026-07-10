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
- Requires the editor running with the MCP server started
  (`ModelContextProtocol.StartServer` in the editor console; default port
  8000). If `/mcp` shows `unreal-mcp` disconnected, ask the human to start
  the editor/server instead of working blind.
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

## Definition of done (every task)

- Code compiles via the engine's `Build.sh` (see the host `CLAUDE.md` for the
  exact command) with zero warnings introduced.
- Relevant automation tests pass.
- UI changes verified visually (MCP screenshot or human review).
- Acceptance criteria of the current phase spec are met.
- No GPL-derived code, license headers present, naming conventions followed.
