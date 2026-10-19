# Changelog

## 0.1.0-beta.7

Fixes the long-session freezes and the periodic hiccups, makes the default quiet and adds FSR and rumble.

### Fixed
- **Freezes and the Atmosphere fatal screen after a long session (about 90 minutes).** The texture cache had a 3 GiB budget
  on the Switch, so nothing was ever freed and it grew (13 to 963 MiB in 85 minutes) until the process ran out of memory;
  the game then failed to create threads (C0000017) and the console eventually hung. The budget is now 704 MiB, with the least
  recently used textures freed above it.
- **A hiccup every 5 seconds.** The renderer's statistics report (about 60 lines) stalled the draw thread for 130-215 ms
  every 5 s. The detailed report now appears once a minute, a short one every 5 s.
- Freeze when a cutscene starts after a long session (fixed in beta.6, kept): 8 MiB host stacks per game thread.

### Changed
- **Quiet by default.** The shipped `nfscarbon.toml` logs warnings only and the statistics are off, which costs less FPS,
  most at the race start. `nfscarbon.report.toml` (full logging) is included for performance reports.
- **FSR upscaling is the default**: the scene is drawn at 80% and upscaled with FSR (`present_effect`, `carbon_gpu_render_scale`).
  A 15-minute session at the reference clocks ran at a median of 40.8 FPS, 95% of the time at 30 FPS or more (earlier sessions
  at 90% with plain scaling: median 32 and 38, different scenes). Plain bilinear at 90% is one setting away.
- **Controller rumble is on** (`input_vibracion = true`); it was off by default before.

### Known issues added
- DLC: a very large set with leftover extracted folders crashed profile loading; the README lists the verified packs and the
  recovery steps.

## 0.1.0-beta.6

- Fixes the freeze when a cutscene starts after a long session (thread creation failed, error C0000017): the host stack of each
  game thread is now 8 MiB instead of 16, which frees about 260 MB of the process memory. Setting `thread_host_stack_mib`.
- Unjoined host threads are detached so their stacks are freed.
- The log has a build line and a memory/thread/heap line every 10 seconds.

## 0.1.0-beta.5

First release with a downloadable NRO.

- README thanks Sampreeth004 explicitly for the Carbon recompilation this port is built on.

## 0.1.0-beta.4

- Ko-fi support button in the README.
- The banner shows the release version, stamped automatically when a release is built.

## 0.1.0-beta.3

- Docs use CPU 1428 / GPU 768 / RAM 1600 MHz as the reference clocks, with results from two measured sessions.
- Support section (Ko-fi) and funding button.
- Release scripts: version comes from this file, only numbered headings, `-beta` suffix required.
- The log's first line shows the version and author; a startup log line moved out of the DLC code.
- Swapchain-timeout error is logged at most every 30th time.

## 0.1.0-beta.2

- Presenter waits at most 2 s for a swapchain image, then recreates it (picture-freeze mitigation, unconfirmed).
- The NRO shows the author and version in the launcher and the first log line.
- Build guide explains how to add DLC.

## 0.1.0-beta.1

First public beta. Runs the Xbox 360 version of Need for Speed: Carbon on the Switch.

- Native Vulkan renderer on NVK, with disk-cached shader and pipeline compilation.
- Cutscenes decoded with FFmpeg (WMV3), checked against the game's own decoder on the first frames of every movie.
- Audio: game mixer helpers as register-local code and NEON where bit-exact; XMA decoding on a worker thread; the
  host waits for the mixer's packet instead of dropping it.
- Renderer: cheaper texture untiling, vertex and constant uploads, texture descriptor sets kept across frames.
- DLC packages (STFS) found in `dlc/` are installed at startup.
- Fixes: cutscene freeze when the process ran out of memory for the video thread; freeze when opening the log console.
- Quality switches: render scale, anisotropy, texture LOD bias, trilinear off, bloom.

Known problems are listed in the README.
