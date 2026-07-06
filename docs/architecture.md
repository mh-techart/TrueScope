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
- Measures the **active viewport** (WYSIWYG). Exact camera measurement is
  achieved by piloting a Cine Camera or using the Cinematic Viewport.
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
- Values are display-referred (0–1 → scope scale 0–1023). With the tonemapper
  disabled, scenes clip exactly as the viewport shows them clipping.
- **Calibration note:** percentages and vectorscope graticule targets are
  calibrated for Rec.709/sRGB-encoded output. With OCIO view transforms
  targeting other displays (P3, PQ/HDR), TrueScope still measures the actual
  display signal (WYSIWYG), but its colorimetric interpretation is only
  guaranteed for the SDR Rec.709 case. Scene-referred / HDR / OCIO-aware
  modes are explicitly deferred (see backlog) and documented as a V1
  limitation. Related user-facing note for the README: Movie Render Queue
  exports with their own OCIO output transform are a different signal than
  the viewport — TrueScope measures the viewport.

## Settled decisions

| Topic | Decision | Rationale |
|---|---|---|
| License | **Apache-2.0** | Permissive like MIT, plus explicit patent grant and contribution terms — better default for a community project. Fab-compatible. |
| Engine version | **UE 5.8** (Launcher build) | Current version; best MCP/ModelContextProtocol support for AI-assisted development. Port to ±1 version only after M6. |
| Primary dev platform | macOS (Metal); Windows/Linux verified later (M7) | Developer's machine. Metal quirks surface early as a side benefit. |
| UI framework | **Slate**, dockable **Nomad Tab** (registered via `FGlobalTabmanager`), menu entry under *Window → TrueScope* | Native editor integration, docking + layout persistence for free, anti-aliased line/polygon drawing solves the "no pixelated look" requirement. No content assets needed → clean code-only plugin. |
| UMG / Editor Utility Widgets | Rejected for the scopes UI | Would still need a C++ backend, and lacks crisp vector drawing; a Slate tab already docks next to any Editor Utility Widget. |
| Common UI plugin | Not used | Runtime/game UI framework (input routing, gamepad); irrelevant for editor tools. |
| Image source | **Active Viewport via `FSceneViewExtensionBase`**, subscribed after the tonemap pass; the post-tonemap image is copied/downsampled into a plugin render target. Single source in V1. | Zero level footprint; WYSIWYG for every viewport type incl. Cinematic Viewport and PIE; camera measurement via piloting. Engine reference for the pattern: the OpenColorIO plugin (post-tonemap view-extension pass) and other in-engine `SceneViewExtension` users. |
| Cine Camera via SceneCapture | **Deferred to backlog** (optional convenience: measure a camera without piloting it) | Original MVP path; superseded as primary source by the decision above. Would reintroduce a scene-side component — acceptable only as an opt-in extra, never a requirement. |
| Viewport overlays | Not in the scopes window scope of V1, **but planned (M8)**: Zebra, over/under-exposure warning (red/blue), False Color — implemented as a *writing* pass on the same SceneViewExtension, per-viewport toggle from the TrueScope tab | Reuses M2 (view extension) + M5 (plugin shader) infrastructure; thresholds are display-referred % and therefore consistent with the waveform. No PostProcessVolume, no material asset, no level changes. |
| Analysis, stage 1 (MVP) | **CPU**: async GPU readback (`FRHIGPUTextureReadback`) of the downsampled capture, C++ histogram building in a background task | No shaders → fully debuggable and unit-testable in text; at 2–3 Hz and ~0.5 MPix per update the cost is negligible. Becomes the ground truth for the GPU path. |
| Analysis, stage 2 | **Compute shaders** (plugin `.usf`, virtual shader dir via `AddShaderSourceDirectoryMapping`, module loading phase `PostConfigInit`), RDG passes, `InterlockedAdd` binning, second pass maps counts → intensity texture | Full-resolution precision, Resolve-grade density look, headroom for higher refresh rates. Must produce results matching the CPU path on test images. |
| Color math | **BT.709** R'G'B' → Y'CbCr for vectorscope and luma waveform; measurement point = display-referred output (sRGB/Rec.709 case) | Matches what Resolve shows for SDR delivery. OCIO / HDR / working-color-space variants deferred (backlog). |
| Scales | Waveform/Parade: 10-bit (0–1023); Histogram: 0–100 %; Vectorscope: standard graticule with 75 % targets (R/Mg/B/Cy/G/Yl) | Matches the DaVinci Resolve reference. |
| Anti-pixelation recipe | (1) density accumulation with log intensity mapping — no binary on/off pixels; (2) scope textures at display resolution or higher; (3) graticules/scales as anti-aliased Slate lines (`FSlateDrawElement::MakeLines`, `MakeCustomVerts`) | This is why Resolve/FCP scopes look smooth and most free solutions don't. |
| Layout | 2×2 `SSplitter` grid — Waveform | Parade / Vectorscope | Histogram — each panel switchable via dropdown | Resolve reference layout. |
| Data interface | `FTrueScopeAnalysisResult` (bin arrays and/or textures) decouples analysis from UI | Lets CPU → GPU migration happen without touching the UI layer. |
| Repo scope | This repo contains **only** `Plugins/TrueScope/`; the host project stays local to each developer | Users clone straight into their own project's `Plugins/` folder. |
| Menu entry | Window menu only in V1; toolbar button deferred (backlog) | Toolbar space is contested; Window menu is where editor panels are expected. |

## Architecture overview

```
Active editor viewport (any type: Level, Cinematic, PIE)
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
│ AA graticules, log density  │
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
    ├── UI/                      (STrueScopePanel, per-scope widgets)
    └── Tests/                   (automation tests)
```

## Phase plan (milestones)

| Phase | Deliverable |
|---|---|
| M0 | Environment ready: build loop, VS Code, CLAUDE.md, git repo, MCP plugin |
| M1 | Plugin skeleton: empty dockable "TrueScope" tab, Window-menu entry, 2×2 splitter with placeholders |
| M2 | Capture via **SceneViewExtension**: post-tonemap image of the active viewport copied/downsampled into a render target at 2–3 Hz. Acceptance includes: (a) toggling the viewport tonemapper / show flags is reflected in the captured image; no crash or stall when the tonemap pass is disabled (verify hook behavior — fallback: hook a later pass in the chain); (b) with an OCIO viewport transform active, the captured image includes that transform — TrueScope's pass runs after the OCIO extension's pass |
| M3 | CPU MVP: async readback, histogram + parade drawn anti-aliased in Slate, automation tests on synthetic images |
| M4 | All four scopes complete (BT.709 math, vectorscope graticule, log mapping, scales) |
| M5 | GPU compute path (validated against CPU ground truth on test images) |
| M6 | Polish: settings, persistence, per-panel switching, perf check, **measurement status line** in the TrueScope tab showing what is being measured (viewport type/name; display encoding, e.g. "sRGB/Rec.709" or the active OCIO view-transform name) — makes the V1 calibration scope visible in the UI instead of only in the README |
| M7 | Release: `RunUAT BuildPlugin` for Win64+Mac+Linux (Win/Linux built on a Windows machine — Linux via Epic's cross-compile toolchain), README, binaries; optional Fab |
| M8 | **Overlays: Zebra, over/under-exposure warning, False Color** — writing SceneViewExtension pass, thresholds consistent with waveform %, per-viewport toggles in the TrueScope tab (details: backlog.md) |

## Testing strategy

Scopes are mathematically verifiable — exploit that:

1. **Automation tests on synthetic images** (headless): solid red frame → one
   histogram peak in R at max bin; horizontal gradient → diagonal waveform
   line; SMPTE color bars → vectorscope points land in the six graticule
   targets. CPU results later serve as ground truth for GPU results.
2. **WYSIWYG tests (M2+):** change viewport state (tonemapper on/off, exposure
   compensation, show flags) and assert/inspect that captured image and scopes
   follow.
3. **Editor screenshots** via Epic's Claude Code MCP plugin for layout/visual
   checks during development.
4. **Human sign-off — external cross-check workflow** (acceptance ritual for
   M4, repeat after major analysis changes): compare TrueScope against DaVinci
   Resolve's scopes on the same frame.
   1. Use a test scene with **fixed exposure** (manual exposure / fixed
      EV100) — auto eye adaptation would make screenshot and scope moment
      differ.
   2. Take a viewport **High Resolution Screenshot at multiplier 1.0**
      (higher multipliers alter screen-space effects and no longer match the
      viewport) as a **standard LDR PNG** — *not* the HDR/EXR option, which
      captures linear pre-tonemap data and is not our display-referred
      measurement point.
   3. Open the PNG in DaVinci Resolve; set clip data levels to **Full**
      (not Video — the 64–940 vs 0–1023 mismatch is the classic false alarm)
      and color management to sRGB/Rec.709. Photoshop's histogram works as a
      second opinion without setup, but only Resolve provides waveform and
      vectorscope.
   4. Compare histogram shape, waveform/parade, and vectorscope positions
      (color bars!). Minor deviations from the PNG's 8-bit quantization are
      expected; structural deviations are bugs.

## Licensing red line

`norihiro/obs-color-monitor` (GPL-2.0) and ffmpeg scripts are *behavioral
references only* (what should scopes show for a given input). No code may be
copied or transliterated from GPL sources. All math here is textbook-standard
(BT.709 matrices, histogram binning) and is implemented from scratch.

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
