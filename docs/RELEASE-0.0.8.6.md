# Star Fox Enhanced 0.0.8.6

This cumulative release supersedes 0.0.8.5 and includes its visual options,
stereoscopic display improvements, rendering fixes, startup improvements and VR
resource handling, together with the visual-effects cleanup below. Supply your
own supported retail ROM; no ROM or private game data is included. Nintendo 3DS
development is paused and no 3DS package is included.

## Visual options

- Added a Global Enhancements submenu with independent Low/Medium/High controls:
  light shafts, anamorphic flare, halation, radial blur, heat haze, sharpening,
  vignette, film grain, CRT scanlines, CRT curvature and lens ghosts.
- Added scene enhancements for explosion shockwaves, weapon lighting and
  exhaust trails, plus ambient occlusion, depth of field, impact sparks/debris,
  exhaust heat distortion, CRT phosphor persistence, adaptive exposure, water
  caustics, impact shake, weapon recoil, volumetric fog and motion blur.
  Availability depends on the renderer. HUD text and menu overlays remain
  protected from scene effects.
- Removed Soft Blur/Soft Focus, RGB Phosphor Mask, Stage Weather and Camera
  Banking from the options and gameplay. Old settings and save states clear
  their retired fields without changing the other controls. CRT Phosphor
  Persistence is retained; it is separate from the removed RGB mask.
- Improved warm halation around narrow bright highlights without blurring
  the underlying scene.
- Made Heat Haze smooth and coherent across RGB channels, including dark
  scenery.
- Strengthened localized Exhaust Heat Distortion and nearby Weapon Lighting.
- Improved Impact Shake for actual small shield-meter damage steps, and
  made Weapon Recoil's kick and recovery more visible.
- Improved one-effect-at-a-time comparison captures with matching gameplay,
  appropriate scenes/events, and clearly labeled paused details.
- Added ray-tracing quality levels; renamed the former HDR control to Contrast.
  Removed 3D smoothing from the selectable options.
- Reworked enhanced terrain, transparent water and animated lava, including
  palette gradients, source-layer alignment and model/explosion occlusion.
  Automatic lava selection is restricted to EX 6-6; manual selection remains
  available. Added backdrop coverage and the low hills for that stage.
- Expanded anti-aliasing choices and fixed integer-scaling behavior. Separated
  DLSS and DLSS 4.5, removed forced 6x rendering, and improved temporal preview
  inputs and protected text. ReShade is no longer part of the runtime.

## Displays, menus and controls

- Expanded stereoscopic output with separation, convergence and reticle-depth
  controls, depth-aware backgrounds, and additional output formats.
- Improved direct Leia SR Platform compatibility and optional DisplayXR
  detection, diagnostics, texture presentation and redistribution notices.
  Unsupported hardware/runtime combinations remain unavailable.
- Added Fit to Screen across builds with its own configurable layout,
  fullscreen startup and an in-game fullscreen toggle.
- Improved pre-game preview startup and rendering feedback. With Preview off,
  the menu avoids preparing and rendering the gameplay scene.
- Improved touch-layout editing, menu controller remapping, percentage displays
  and level-select sound-bank initialization.

## VR and packaging

- Fixed VR cockpit visibility and steering, interpolation and frame-resource
  ownership. Improved PCVR startup instructions and crash diagnostics.
- Added PS5 homebrew and Steam Frame ARM64 release packaging alongside Windows,
  Xbox UWP, Linux, macOS, iOS, Android, Quest, PCVR, Switch and Vita.
- iOS uses a proper unsigned IPA for sideloading and retains the 1x/2x render
  limit. Android and Quest retain the existing release-signing certificate.
- Packages include required third-party notices and checksums. Optional device
  runtimes still require compatible hardware and, where applicable, separate
  vendor installation.

## Known limits

This remains an alpha. Build and package validation is not comprehensive
hardware acceptance. VR, PS5 and Steam Frame are experimental; performance and
optional effects vary by device. Broader scene/device testing remains ongoing.
The New 3DS port is not included because physical testing found low frame rates,
long pauses and rendering defects. Experimental source-reflection validation is
not enabled in ordinary gameplay.
