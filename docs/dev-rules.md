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

## Verification methodology

- **Measure or derive before you build; don't guess target values.** Anything that
  follows an external standard or reference (color-space math, marker positions,
  readback cost, calibration curves) has a *correct* value — derive it from the
  standard or measure it from a reference, and pin it in a test. Guessing a
  constant and tuning by eye has repeatedly been wrong here (vectorscope circle
  calibration, readback cost). If a value looks like a matter of taste but isn't,
  treat it as math.
- **Mark every claim by evidence status.** A statement derived from a standard,
  measured, or verified in the UE 5.8 source is **settled**; anything else —
  including assumptions carried over from a previous chat or agent — must be
  labelled **Hypothesis / Unverified assumption** until tested or proven from a
  safe source, and must not be built upon as if settled.
- **Verify "is it live?" with a moving source.** A static subject makes "not
  updating" indistinguishable from "unchanged". To confirm a scope actually
  measures live, use a visibly moving source (e.g. a never-still particle effect)
  with the viewport in Realtime — then "frozen" and "updating" are distinguishable.
- **Two-part acceptance: automated test AND human eye.** The automated test catches
  what the eye misses (a single wrong bin, an off-by-one); the human visual check
  catches what the test doesn't model (gamma/brightness, stroke width, upscaling
  softness). Neither alone is sufficient for a visual feature. Bin-equivalence
  compares counts; appearance is a separate check.
- **Prove a regression test has teeth.** Don't just assert a test passes — perturb
  the thing it guards (off-by-one, swapped channel, reverted constant), confirm it
  fails, then revert. A test never seen to fail proves nothing.
- **Separate what is measured from the host state it samples.** Some costs are the
  plugin's own; others are properties of the editor state the plugin merely samples
  (e.g. on Metal the readback Lock cost is ~1–2× the host frame-time, not a plugin
  cost). Label host-dependent numbers as such and record their conditions; never
  compare two such numbers taken under different conditions.
- **Document deliberate "looks wrong" choices next to the code.** When something is
  intentionally counter-intuitive (ticks not aligning with targets; per-hue marker
  radii not sharing one circle; 100% magenta/green sitting on the rim; skin line at
  123° un-rotated), write the derivation in a comment so a future reader (or agent)
  doesn't "fix" it back into a bug.
- **Measure both sides with known-population targets — never assume one.** To
  calibrate a scope against a reference tool (Resolve), build synthetic targets
  whose pixel populations are *exactly known* (neutral bars of known per-column
  pixel count; full-height strips of known area per luma), feed them through
  *both* tools, and fit the reference's transfer/gain from its rendered output.
  Measure TrueScope's own transfer too (headless, through the shipping code) —
  don't assume it from a prior note. The density/histogram calibration (2026-07-20)
  was settled this way; a carried-over "log" assumption was nearly built on until
  both sides were actually measured. Pin the fitted result in a test.
- **The shipped path is the maintained path.** Whatever ships as default is what
  gets exercised and stays correct; a path only used in tests rots. Choose defaults
  accordingly, and keep non-default paths covered by tests that actually run.

## Work-order process

This project is developed in two layers, and contributors should keep them
separate:
- **Planning layer** (a chat/agent used for design): drafts phase specs, weighs
  options, records decisions, maintains the backlog and architecture. It does not
  edit the repo directly — it produces **work orders** (self-contained instructions,
  with any new doc content embedded as text so the implementer writes the file).
- **Implementation layer** (Claude Code in the repo): reads the plan, researches
  the engine source first for risky work, implements, builds, runs the tests and
  MCP verification, commits, and reports back.
- **Decision gate:** phase specs and decision-level questions go to the human before
  code. Implementation details do not — they belong in the implementation layer,
  iterated to completion, not litigated in planning. Risky phases start with a
  Task-0 research step (read the engine source), report findings, then code.
- **Round trip:** implementation reports results to planning, which reviews against
  the spec, records what was learned in the docs, and issues the next work order.
  Findings that would help future contributors go into the docs, not just the chat.

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

## Resolve cross-check — conditions & procedure

The scope cross-check against DaVinci Resolve is a manual human step (Unreal
High-Res screenshot → import into Resolve → compare scope values). The *principle
and what to compare* live in `architecture.md` (testing strategy); the *conditions
and procedure* are here.

**Repeatable plate — console route (verified 2026-07-20):**

- Pilot a level **CineCameraActor** — its filmback fixes framing + aspect, so the
  artist's viewport size/aspect is irrelevant and its letterbox bars do not enter
  the capture (measured — `architecture.md`, capture geometry).
- **Game View on** (`G`) — hides gizmos/billboards, which `HighResShot` does not
  auto-hide and which never appear in TrueScope's capture.
- **Realtime off** (`Ctrl+R`) — freezes temporal effects (particles, weather,
  self-animating actors) so the scope's captured frame and the PNG are the *same*
  still; the frame still renders. Truth signal: the `Capture pass ran` log
  timestamp stops advancing.
- **AA off**: `r.AntiAliasingMethod 0`. **Never FXAA (1), TAA (2) or TSR (4).**
  TrueScope's capture hook runs *before* FXAA (after-tonemap, priority 0; FXAA is
  later in the chain), so with FXAA on the PNG is post-FXAA while the scopes
  measured pre-FXAA — an edge mismatch that costs hours in density calibration.
  TAA/TSR jitter sub-pixel frame-to-frame. Off is the only condition where PNG and
  capture agree. *(This supersedes the earlier "FXAA … harmless" note — it is not
  harmless for pixel-exact work.)*
- `r.ScreenPercentage 100`.
- **`HighResShot <W>x<H> 0 0 0`** in the console → exact resolution independent of
  window size and retina/High-DPI scale (no viewport resizing needed), rendered at
  the camera's filmback aspect (no bars). Written to `Saved/Screenshots/MacEditor/`.
  **Do not impose the comparison resolution as a framing constraint** — pick any
  W×H with **height ≥ 1080 px** (TrueScope analyses at 540 px, so ≥1080 loses no
  detail; more is fine), then match Resolve's timeline to the PNG. Claude Code may
  produce these plates itself; the human manual route is the fallback.
- Subject & exposure: engine **ColorCalibrator**, **fixed exposure** (manual
  EV100) so the still and the scope moment agree. Camera fully still; Lumen
  converged; auto-exposure settled; no self-animating actors in the reference
  scene (the VolumetricCloud that used to drift has been removed — keep the
  reference motionless).

**Resolve side:** import the PNG on a timeline of **the same pixel dimensions**,
8-bit, **sRGB**, clip **Data Levels = Full** (the 64–940 vs 0–1023 mismatch is the
classic false alarm). Undock the Scopes into a floating window; compare **values on
each scale**, not raw appearance.

**High DPI:** with the explicit-resolution `HighResShot` route the PNG is
unaffected by the editor's High-DPI setting; it only affects the crispness of
TrueScope's *on-screen* panels for the visual side-by-side — set it as preferred
for that. *(Replaces the earlier "High-DPI on is the only cross-check/benchmark
difference" claim, now moot for the PNG.)*

**Pixel-exact option:** `HighResShot 960x540` matches the capture's pixel count
(hence its density normalisation) but is a *native* render, not the downscaled
image the scopes actually see; true 1:1 needs dumping the capture RT — see
`backlog.md`.

**Pin the ViewRect for density calibration:** the capture target is fixed 960×540
but the source ViewRect varies with window size, so the downscale ratio (and fine
density readings) vary — read the ViewRect off `Capture pass ran (WxH)` and keep it
fixed across a calibration run, or drive off the dumped capture RT.

## Definition of done (every task)

- Code compiles via the engine's `Build.sh` (see the host `CLAUDE.md` for the
  exact command) with zero warnings introduced.
- Relevant automation tests pass.
- UI changes verified visually (MCP screenshot or human review).
- Acceptance criteria of the current phase spec are met.
- No GPL-derived code, license headers present, naming conventions followed.
