# STALKER Anomaly Modded Exes — PiP Scopes + DLSS/FSR3 (X-Ray Monolith MT fork)

This repository is a **combined modded exes build** for S.T.A.L.K.E.R. Anomaly 1.5.3 / GAMMA,
merged from three X-Ray Monolith development branches:

| Component | Source | What it gives you |
|---|---|---|
| Base — X-Ray Monolith **MT** | [themrdemonized/xray-monolith](https://github.com/themrdemonized/xray-monolith), branch `all-in-one-vs2022-wpo-mt` (based on the 2026.9.12 release) | All modded exes features + MT optimizations + **Shader Bus** (required for Glossy Puddles and other new shader mods) |
| **PiP scopes** (objective-camera) | [asuparabekon/xray-monolith-bodycam](https://github.com/asuparabekon/xray-monolith-bodycam), branch `bodycam-mt` | True picture-in-picture: a second world render through the scope's objective lens, optics physics, tunneling, parallax, bodycam camera module |
| **Native DLSS / FSR 3.1** | [brodrigz/xray-monolith](https://github.com/brodrigz/xray-monolith), release 1.0.0 | DLSS and FSR3 upscaling natively in DX11, quality presets, CAS sharpening, native-resolution PDA/wearable screens |

> [!NOTE]
> The build is based on the **MT version** of the engine (what official releases call MT-TEST) —
> both source modifications (bodycam and brodrigz) are themselves based on the MT branch.
> The engine version string is **`2026.9.12 DLSS/FSR3 MT + Immersive Bodycam`** (shown in the
> log and returned by `get_modded_exes_name()`).

## What's Inside

* **Picture-in-Picture scopes** — instead of a shader "zoom", a second view of the world is
  rendered from the scope's objective lens position:
  * a dedicated entrance-pupil camera with physically modeled eye relief, exit pupil and objective diameter;
  * tunneling and vignetting driven by actual eye alignment instead of a fixed overlay;
  * typed optics profiles (LPVO, reflex sights, "figure-eight" scopes, night vision devices),
    weapon-relative reticle positioning as the weapon moves in the deadzone;
  * configuration via an MCM menu and ImGui, `bodycam_user.ltx` for user presets.
* **Native DLSS / FSR 3.1** (from brodrigz):
  * DLSS (NVIDIA NGX) and FSR 3.1 directly in DX11, no injectors and no DX12 bridge;
  * quality presets and DLSS preset selection, CAS sharpening;
  * PDA and wearable device screens rendered at native resolution after upscaling;
  * **requires SSS23** (Screen Space Shaders) for the motion vectors buffer;
  * `r__upscaler_jitter 0/1` (default 1) — Halton raster jitter toggle. If geometry
    visibly shifts/wobbles at upscaled resolutions (e.g. FSR3 Quality) because the
    active shader pack does not apply the jitter to every deferred shader, set it
    to `0`: the raster, motion vectors and the temporal reconstruction stay in
    agreement and the artifact disappears, at the cost of some edge AA.
* **Shader Bus** (from upstream 2026.9.12) — named lanes for script ↔ shader communication,
  required for **Glossy Puddles**. Check: `shader_bus.version()` returns `2`.
* All other modded exes and MT-branch features.

## Installation (GAMMA)

1. Download or build the release archive.
2. Copy the contents of `bin/` into the Anomaly game root (replacing files).
3. Copy `db/mods/00_modded_exes_gamedata.db0` into `<game>/db/mods/`.
4. **Delete the shader cache** in the launcher before the first launch (one time).
5. PiP scope and Bodycam settings: the MCM menu in the game settings + the ImGui panel.
6. For DLSS/FSR3 (optional):
   * install **SSS Update 23+** (its motion vectors are required);
   * in the video settings, disable MSAA and SSS TAA;
   * enable DLSS/FSR3 in the video menu;
   * place the matching `nvngx_dlss.dll` version next to the game (see NVIDIA DLSS releases).

> [!IMPORTANT]
> The exe, the gamedata archive and third-party mods must come from matching revisions.
> Mixing versions may cause scopes to fall back to the legacy shader path or break optics behavior.

## Known Limitations

* Night vision / game overlays are not applied **inside the scope image** when DLSS is enabled
  for the main view at the same time (in the main view, NVG works as usual). This is a
  consequence of DLSS rendering overlays in the native-resolution post phase, which the
  SVP pass skips.
* DLSS-SR inside the scope image itself — the contract is stubbed (`SvpDlss.h`, the
  `r__svp_dlss` command), but the actual evaluation is not implemented: the scope is rendered
  at render-extent and scaled by the lens.
* MT version: the full list of known MT-TEST issues is in the original [README](https://github.com/themrdemonized/xray-monolith/blob/all-in-one-vs2022-wpo/README.md).

## Merge Technical Details

Base: `origin/all-in-one-vs2022-wpo-mt` (7b79d2be). Merged on top:
* `brodrigz/all-in-one-vs2022-wpo-mt` (00093f65) — 12 DLSS/FSR3 commits, merged cleanly;
* `asuparabekon/xray-monolith-bodycam` `bodycam-mt` (2c4420b6) — 10 PiP/Bodycam commits,
  conflicts resolved in 38 files (render, UI, vcxproj), key decisions:
  * the `renderGBuffer`/`renderSceneLighting` structure (SVP passes) was kept, with DLSS gates
    (`dlss::Configured()/Active()/SceneDepth()/BeginScene()/EndScene()`) ported on top;
  * `CRenderTarget` main-target dimensions are DLSS-aware (`dlss::RenderWidth/Height()`),
    the SVP target uses its own scale factor;
  * upstream `PIX_EVENT_C` markers + bodycam `svp_stats` timings combined;
  * native UI (PDA/wearable screens) renders under the `hasNativeHudAttachments` condition in
    a single place (the end of the deferred pass) to avoid drawing twice;
  * UI tab scrolling taken from upstream (the newer implementation), the scope overlay custom
    UI clipping (`SetHitClip`, `HasCustomClip`) — from bodycam.
