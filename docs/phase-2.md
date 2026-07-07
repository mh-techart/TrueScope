# Phase 2 — Viewport capture via SceneViewExtension

Read `architecture.md` first (esp. "Core measurement principle: WYSIWYG" and
the M2 row). This phase produces the capture pipeline: a downsampled,
correctly color-referenced image of the active viewport, refreshed ~2–3×/sec,
available as a render target. **No analysis, no scopes, no settings UI** — that
is M3 and later. The only visible output is a throwaway raw preview that proves
the capture works.

This is the most render-pipeline-heavy phase. Expect to consult the engine
source repeatedly; expect one or two iterations. If a decision-level surprise
appears (the post-tonemap hook behaves fundamentally differently than assumed
and a different measurement point seems necessary), **stop and ask** per the
CLAUDE.md self-check — do not silently re-architect.

## Task 0 — Research before writing code (mandatory first step)

Do not write the view extension from memory. First read, in the installed 5.8
engine source, how the engine itself does this:

- Find the OpenColorIO display-extension plugin's scene view extension and
  read how it registers and where it inserts its pass relative to tonemapping
  (`Engine/Plugins/.../OpenColorIO...`, and `FSceneViewExtensionBase` /
  `ISceneViewExtension`, `SubscribeToPostProcessingPass`,
  `EPostProcessingPass` in `Engine/Source`). This is the living reference for
  our hook point.
- Working assumption to verify (NOT to trust): the OCIO pass inserts itself
  *after* the engine tonemap. TrueScope must sample *after* OCIO, so the
  captured image is the final display-encoded viewport image.
- Decide the concrete hook point and pass ordering from what the source
  actually shows, then record the chosen point and the reasoning in a short
  note (commit message or a "Capture hook" note appended to this file).

### Capture hook (Task 0 result, from UE 5.8 source)

Chosen hook: **after-pass callback on `EPostProcessingPass::Tonemap`, extension
priority 0** (`FTrueScopeViewExtension`). Reasoning, verified in
`Renderer/Private/PostProcess/PostProcessing.cpp` and the OpenColorIO plugin:

- OCIO applies its display transform in the same after-Tonemap callback chain
  (`OpenColorIODisplayExtension.cpp`), at priority 100. Extensions are gathered
  sorted by priority *descending* (`FSceneViewExtensions::GatherActiveExtensions`)
  and `AddAfterPass` executes the callbacks sequentially, each receiving the
  previous one's output — so priority 0 < 100 guarantees TrueScope samples the
  **post-OCIO** image via the engine's intended chaining mechanism.
- `Tonemap` is the only pass whose after-callbacks run on **both** render paths:
  the full post-processing path and the minimal path used when post-processing
  is disabled (gamma-only tonemap). FXAA/VisualizeDepthOfField callbacks are
  skipped on the minimal path. The Tonemap pass also runs regardless of the
  Tonemapper show flag, so no fallback hook is needed.
- The pass is read-only: it copies/downsamples scene color into the capture RT
  and returns via `FPostProcessMaterialInputs::ReturnUntouchedSceneColorForPostProcessing`,
  which correctly fills `OverrideOutput` when the callback is the last pass.
- Known deviation (accepted, same position as OCIO itself): FXAA/SMAA and
  *After Tonemapping* post-process materials run after this point and are not
  part of the captured signal.

## Tasks

1. **SceneViewExtension** (`Source/TrueScope/Capture/`): a
   `FTrueScopeViewExtension : FSceneViewExtensionBase` that subscribes to the
   post-tonemap (post-OCIO) point determined in Task 0 and copies the view's
   color output into a plugin-owned `UTextureRenderTarget2D`, downscaled to a
   fixed working resolution. Registered/unregistered with the module
   lifecycle.
2. **Active-viewport selection:** measure the **last focused Level Editor
   viewport**. If none is available, produce no capture (leave the RT
   cleared) and let the preview show a "no viewport" state — no error, no
   crash. (Viewport-picker dropdown and Cinematic-viewport special handling
   are backlog, not M2.)
3. **Throttling:** update the RT at a fixed ~2–3 Hz (timer or frame-count
   gate on the render side), not every frame. Values as named constants.
4. **Fixed constants** (no settings UI this phase), clearly named in one
   place: working resolution (~960×540) and refresh rate (~2–3 Hz).
5. **Throwaway raw preview:** replace one of the M1 placeholder panels (or add
   a temporary debug panel) with an `SImage` that displays the capture RT via
   an `FSlateBrush`. This is explicitly temporary — M3 replaces it with real
   scopes. Its only job: prove the right image arrives and tracks viewport
   state live.
6. License headers on new files; naming conventions; commit when acceptance
   criteria are met.

## Acceptance criteria

- [ ] Builds via the CLAUDE.md Build.sh command with no new warnings.
- [ ] The raw preview in the TrueScope tab shows a live, downscaled image of
      the active level viewport, updating at roughly 2–3 Hz.
- [ ] **WYSIWYG — tonemapper:** toggling the viewport tonemapper
      (`Show → Post Processing → Tonemapper`, or exposure changes) is visibly
      reflected in the preview within an update or two. No crash or stall when
      the tonemap pass is disabled. If the chosen hook misbehaves with the
      tonemapper off, fall back to a later pass in the chain and note it.
- [ ] **WYSIWYG — OCIO:** with an OCIO viewport transform active, the preview
      includes that transform (i.e. TrueScope samples after the OCIO pass).
      *Prerequisite: the OpenColorIO plugin is enabled in the host project and
      a view transform is set on the viewport — the human has enabled OCIO for
      this test.*
- [ ] Switching the active viewport / focusing a different level viewport
      updates which viewport is measured; no viewport → clean "no viewport"
      state.
- [ ] Verified via MCP screenshots (tonemapper on vs. off; OCIO on vs. off)
      and reviewed by the human. Automated pixel-level tests are optional and
      must not block this phase.
- [ ] Chosen capture hook point + reasoning recorded (commit or note).
- [ ] Editor stable across open/close of the tab and editor restart; no
      TrueScope warnings/errors in the Output Log.

## Explicitly out of scope (do not build yet)

Any histogram/waveform/vectorscope analysis, real scope drawing, per-scope
widgets, settings UI, refresh-rate/resolution controls, viewport-picker
dropdown, Cine Camera SceneCapture source, GPU compute analysis. The preview
is raw image only. Ideas → `docs/backlog.md`.
