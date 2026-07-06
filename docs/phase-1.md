# Phase 1 — Plugin skeleton with dockable TrueScope tab

Read `architecture.md` first. This phase produces the smallest possible
working plugin: it exists, it loads, its window docks. No capture, no
analysis, no scopes yet.

## Goal

A code-only editor plugin `TrueScope` inside the host project
(`Plugins/TrueScope/`) that adds an empty, dockable "TrueScope" tab to the
Unreal Editor.

## Tasks

1. Create `TrueScope.uplugin`:
   - one module `TrueScope`, `"Type": "Editor"`,
     `"LoadingPhase": "PostConfigInit"` (needed later for shader registration
     — set it correctly now to avoid churn),
   - plugin metadata: FriendlyName "TrueScope", Description
     "Viewport Scopes for Unreal Engine Editor — Waveform, RGB Parade, Vectorscope
     and Histogram in a dockable editor window.", Category "Editor",
     CanContainContent false, Installed false (for now).
2. Module `FTrueScopeModule` (`StartupModule`/`ShutdownModule`):
   - register a Nomad tab spawner "TrueScope" with `FGlobalTabmanager`
     (proper display name + tooltip via `LOCTEXT`, menu group: the standard
     editor Tools/Window category so it appears under **Window → TrueScope**),
   - unregister cleanly in `ShutdownModule`.
3. Tab content widget `STrueScopePanel`:
   - 2×2 layout built from nested `SSplitter`s,
   - four placeholder panels labeled **Waveform**, **Parade**, **Vectorscope**,
     **Histogram** (centered `STextBlock` on a dark background is enough).
4. Apache-2.0 `LICENSE` file in the plugin root; license header in every
   source file.
5. Minimal `README.md` stub in the plugin root (name, claim, one-paragraph
   description, "work in progress").
6. `.gitignore` for the plugin repo (`Binaries/`, `Intermediate/`).

## Acceptance criteria

- [ ] Project builds via the `Build.sh` command from CLAUDE.md with no new
      warnings.
- [ ] Editor starts; **Window → TrueScope** opens the tab.
- [ ] The tab shows the 2×2 splitter with the four labeled placeholders and
      splitters can be dragged.
- [ ] The tab can be docked next to other editor tabs; after closing and
      restarting the editor, the docked position is restored (Slate layout
      persistence — comes for free if the tab is registered correctly).
- [ ] The tab can be closed and reopened repeatedly without errors; no
      warnings/errors from TrueScope in the Output Log.
- [ ] Disabling the plugin in a packaged-game context is a non-issue by
      construction (Editor module) — verified simply by the module type in
      the .uplugin.
- [ ] Screenshot of the docked tab taken (via MCP plugin or manually) and
      reviewed by the human.

## Explicitly out of scope (do not build yet)

Scene capture, timers, analysis code, settings, toolbar buttons, icons,
shader files, any per-scope drawing beyond the text placeholders. Ideas go
to `docs/backlog.md`.
