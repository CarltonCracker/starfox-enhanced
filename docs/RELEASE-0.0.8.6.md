# Star Fox Enhanced 0.0.8.6

A visual-effects cleanup following 0.0.8.5.

- Removed Soft Blur/Soft Focus, RGB Phosphor Mask, Stage Weather and Camera
  Banking from the options and gameplay. Old settings and save states clear
  their retired fields without changing the other controls.
- Improved warm halation around narrow bright highlights without blurring
  the underlying scene.
- Made Heat Haze smooth and coherent across RGB channels, including dark
  scenery.
- Strengthened localized Exhaust Heat Distortion and nearby Weapon Lighting.
- Improved Impact Shake for actual small shield-meter damage steps, and
  made Weapon Recoil's kick and recovery more visible.
- Improved one-effect-at-a-time comparison captures with matching gameplay,
  appropriate scenes/events, and clearly labeled paused details.

All other 0.0.8.5 features remain. CRT Phosphor Persistence is retained;
it is separate from the removed RGB mask. Android and Quest retain the
existing signing certificate. iOS remains an unsigned IPA for sideloading.

Nintendo 3DS development remains paused, with no 3DS release package. This
is still an alpha: build/package checks do not replace hardware testing,
and optional effects and performance vary by platform.
