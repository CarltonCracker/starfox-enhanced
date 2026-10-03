# Star Fox Enhanced on the Steam Frame

I got Star Fox Enhanced running natively on the Valve Steam Frame. It runs on
the headset itself, with no PC and no streaming, and draws through OpenXR and
Vulkan. It reuses the OpenXR and Vulkan code in `src/vr`, built for Linux on
ARM64, plus what's needed to build, package and launch it there. The player is
only built with `STARFOX_BUILD_STEAM_FRAME`, which is off by default. As with
the rest of the project you bring your own ROM, and nothing here ships one.

## Tech stack

The game side is the same C++20 code as everywhere else. The VR side is the
OpenXR and Vulkan renderer that was already in `src/vr`, with the OpenXR loader
(release 1.1.63) and Vulkan headers pinned in `cmake/OpenXR.cmake` and SDL3
3.4.14 linked statically for audio and gamepads. On the Frame, SteamVR on
SteamOS provides the OpenXR runtime and the Vulkan driver, so the package
doesn't ship either of them.

The binary is Linux ARM64. I cross-build it on an x64 Linux machine with Clang
and LLD against the Steam Runtime "Sniper" ARM64 sysroot (snapshot
3.0.20260415.224995, checked against a pinned SHA-256). On the device it runs
natively with Steam Play turned off, under `SteamLinuxRuntime_4-arm64`. The
plain `SteamLinuxRuntime_sniper` wrapper is x86-64, so it's the wrong one. I
build against the sysroot so the binary links the same libraries the device
runtime provides, and a normal x64 CI runner can do it without an ARM machine.

## Building

On a Linux machine with CMake, Ninja, Clang, LLD, Python 3, curl, tar and
binutils:

```
tools/build_steam_frame.sh [source-root] [build-root] [package-root]
```

The script downloads and checks the sysroot, builds the flat `starfox_pc` as an
ARM64 baseline, builds `starfox_steamframe` and the `starfox_vr_runtime_check`
diagnostic, installs the `steamframe` component and then runs the package
checks. It refuses to reuse an existing package directory. macOS can't do this
cross-build, so on a Mac you only get the host VR targets and tests:

```
cmake -S . -B build/host-vr -G Ninja -DSTARFOX_BUILD_VR=ON
cmake --build build/host-vr
ctest --test-dir build/host-vr
```

`portable-builds.yml` has two Frame jobs that do the same thing in CI: the host
VR tests with a software Vulkan driver, and the ARM64 package build with its
checks. Like the other jobs in that file they run on release tags and manual
dispatch.

The Sniper archive has 38 absolute symlinks in its linker directories, so
`tools/prepare_steam_frame_sysroot.py` rewrites them as relative links in the
extracted copy (the download stays untouched). A standalone VR build also
fetches the same pinned SDL3 the flat runtime uses, instead of needing an
installed one.

## What's in the package

The runtime package has `starfox_steamframe`, `LAUNCH-STEAM-FRAME.sh`,
`STEAM-FRAME-START-HERE.txt`, a `vrpreferences.json`, `BUILD-METADATA.json`
(source revision, tool versions, pinned dependencies and a SHA-256 for every
file) and the licence files. A separate diagnostics set has the flat
`starfox_pc`, `starfox_vr_runtime_check` and a report that resolves every ELF's
dynamic dependencies against the sysroot.

There's no ROM, no `Starfox-Assets.BIN`, no soundtrack, no saves and no Vulkan
loader or driver in either set, and the validation scripts fail if any of
those turn up. `vrpreferences.json` asks SteamVR for 2160 resolution, a 90 Hz
minimum, no half framerate and motion smoothing off. Those are requests, and I
haven't checked that SteamVR applies them all.

## Installing and launching

Build `Starfox-Assets.BIN` on a desktop from your own ROM with
`starfox_asset_builder`, the same as for any other platform. Copy it to
`~/.local/share/StarFoxEnhanced/` on the Frame (`$XDG_DATA_HOME/StarFoxEnhanced`
if that's set). Saves, preferences and the shader cache live in the same
folder, so the package itself can stay read-only.

Then put the package folder on the Frame and register it as a devkit title with
the SteamOS Devkit Client. To push updates I use `rsync -a package/
steamos@<frame-address>:<install-dir>/`. Set the title's launch command to
`LAUNCH-STEAM-FRAME.sh`, which works from any directory. If you
want another bundle, data folder or MSU-1 pack, pass `--bundle PATH`,
`--data-dir PATH` or `--msu PATH`. `starfox_steamframe --help` lists the rest.

## Controls

For now the Frame uses the OpenXR bindings that were already there (the
simple, Touch and Index profiles, plus the gamepad fallback). Frame-specific
bindings aren't in yet.

## Design decisions

Why native Linux and not Proton or FEX? The game is already portable C++ and
SDL3 and already builds for Linux. The Frame is ARM64 Linux, so a native binary
is the straightforward path, and I'd rather not put a translation layer or a
Vulkan wrapper between a game and a VR compositor that needs two eyes at 90 Hz.
I haven't benchmarked the alternatives, I just didn't want to start there.

Why reuse `src/vr`? It already builds the stereo scene from the game's own
state, and the Frame is just another OpenXR runtime. So the target is a new
platform entry (`STARFOX_STEAM_FRAME`, a path layout, a package) and not a
second renderer.

## Files outside src/vr

Flat builds shouldn't change.

`include/starfox/compat/bit_cast.hpp` plus about two dozen call sites in
`src/render`, `src/simulation`, `src/app`, `src/timing` and the state archive
swap `std::bit_cast` for `starfox::bit_cast`. Where the standard library has
`std::bit_cast` that's what it calls. The builtin is only the fallback for GCC
10 headers. `tests/bit_cast_compat_tests.cpp` forces the fallback and checks
it. Two Windows-only files now define `NOMINMAX` before `windows.h`, as the
other Windows files already do. `CMakeLists.txt` enables Objective-C++ early on
Apple, which newer CMake needs before SDL's nested project, and adds the
Steam Frame option with guards, the SDL fetch for standalone VR builds and the
new tests. `.gitignore` gets `.DS_Store`.

## What I've tested

Host side, the ctest suite passes on macOS apart from a few tests that already
fail or abort there on a clean upstream checkout (more on that in the PR). The
two new CI jobs run the Linux host tests and the ARM64 package build, and the
package passes the hash, allowlist and ELF checks.

On the Frame itself I've run the flat ARM64 build (menus, audio and gameplay
are fine) and the native VR build, which gets to `FOCUSED` with two eye
swapchains and lets me navigate the menus and play.

## Known limitations

You need your own ROM and a devkit-enabled Frame, and there's no store
packaging. I haven't measured frame times on the device. The profiler is there
for that, but I haven't captured a run, and I haven't compared against Proton
or FEX.
