# Changelog

## 1.1.0 (Standard Edition)

The game's own code runs faster and the process uses less memory in long sessions. There is nothing to change in your
settings. The numbers are hardware measurements on one console at CPU 1428 / GPU 768 / memory 1600 MHz; see
docs/PERFORMANCE.md.

### Added
- **Quick settings page** in the in-game settings overlay (hold L+R, press Right): a short **Quick settings** page with Video (render resolution, upscaler, anisotropic filtering, bloom, texture detail, VSync), Audio (mute, audio buffer, buffer refill) and Controls (rumble, Xbox button layout) rows, each with a one-line help text; a `[restart]` tag marks the ones that apply after a restart. "All settings (advanced)" opens the old full list. "Save to config" writes `nfscarbon.toml` with only the settings that differ from the defaults; comments in the file are not kept, so back it up first.

### Changed
- **The game's code runs faster.** The translated game code read the game's memory with strict "volatile" reads, which forbid
  the compiler from combining or dropping reads. They are plain reads now, except in the 543 functions that wait for another
  thread or use barriers and atomics: those keep the strict reads. In two paired hardware tests on the same race (old and new
  build, same scene load) the game's render thread used 13 to 17% less CPU time per frame and the console used 5 to 10% less
  CPU in total. The frame rate rose by 1 to 3.5% only, because the GPU is the limit in crowded scenes. Windows with more than
  50 audio requests without data went from 7 of 53 to 2 of 55. The code is 9% smaller (46.5 MB to 42.5 MB).
- **Less memory in long sessions.** The GPU memory allocator now grows in 64 MB steps instead of 256 MB steps. In a 48-minute
  session the process memory levelled off at about 2.4 GB instead of about 2.7 GB (the limit is about 3.2 GB), at the same speed.
  `vulkan_vma_block_mb = 0` in `nfscarbon.toml` brings the old step size back.
- Every log starts with a `[build]` line that names the edition and the kind of memory reads, so a report shows which build
  wrote it.
- The developer probes (audio channel probe, gain statistics, I/O cache and memory lines) are off or at info level, so the quiet
  log stays quiet.

### Known problems
- **Flickering shadows.** In crowded scenes with many AI cars (race wars), shadows can flicker or turn into black patches next to a
  washed-out white road for a few frames. Seen at speed (about 120 to 150 km/h) with the motion blur or tunnel vision effect on.
  Not caused by bloom or by the car reflection pass. Cause still unknown; both editions.
- Audio can still crackle for a few seconds at a heavy race start (mixer at about 3.8 ms of CPU per 5.3 ms block).
- **A rare one-second pause.** Once in 48 minutes of testing the game stopped for about 1.2 seconds in the middle of a race.
  The log does not show the cause; the new 64 MB memory steps are the main suspect. `vulkan_vma_block_mb = 0` restores the old
  behaviour at the price of about 260 MB more memory.
- The longest session tested with this build is 48 minutes (73 minutes with the 1.0.0 memory behaviour).
- Achievements are still not confirmed.

## 1.1.0-collectors-beta.1 (Collector's Edition)

The Collector's Edition now runs its game code the way the Standard Edition does, which removes most of its speed gap, and it
gets everything from 1.1.0 above (faster memory reads, 64 MB GPU memory steps, the `[build]` log line). Beta.

### Changed
- **Much faster game code.** In 1.0.0-collectors-beta.1 the translated game code kept every PowerPC register in memory
  between calls and made more than 31,000 calls to register save and restore helpers. The new translation keeps the registers
  in local variables, as the Standard Edition does. Compiled with the same flags, the 15 hottest game functions now run 14% fewer
  instructions, 17% fewer memory reads and 22% fewer memory writes, and the code is 20% smaller (53.0 MB to 42.4 MB). The old translation is still in the repository (`-Edition collectors`).
- **On the console** (one 34-minute session: the four intro movies, six races, a save): no crash, freeze or stall. At the same
  number of draw calls per frame the Collector's Edition came close to the Standard Edition in our test (36.1 FPS
  against 35.3 at 1500 to 2000 draws, 31.1 against 31.9 at 2000 to 2500, 26.9 against 25.5 above 2500). The 1.0.0 beta was 5 to
  10 FPS slower in the same kind of scenes; it can still be slower than the Standard Edition in other scenes. Median 46.7 FPS over the session, 93% of the time at 30 FPS or more.
- The memory reads and the GPU memory steps are the same as in 1.1.0 above. The process memory in that session went from
  1.3 GB to 2.4 GB.

### Known problems
- **Beta, recommended only for people willing to test.** The Standard Edition is the edition to use; the Collector's Edition can
  still run behind it in some scenes. The new translation has run for one 34-minute session on one console.
- The cutscenes use the game's own video decoder (the Standard Edition uses FFmpeg). The four intro movies were opened and
  read without errors; reports on other cutscenes are welcome.
- Flickering shadows, audio crackle at heavy race starts, the rare one-second pause and unconfirmed achievements are the same
  as in 1.1.0 above.
- The video hook, the mixer diagnostics and the level-of-detail hook of the Standard Edition are not used.
## 1.0.0 (Standard Edition, first non-beta release)

Audio, DLC and visual fixes, and Collector's Edition support. The numbers below are hardware measurements on one console
at CPU 1428 / GPU 768 / memory 1600 MHz; see docs/PERFORMANCE.md.

### Added
- **Collector's Edition support (beta).** Own install folder (`switch/nfscarbon-collectors/`), own NRO
  (`nfscarbon-collectors.nro`). The ring-space wait, frame hand-off, audio, render-pass and car-detail hooks of the
  standard edition were ported with every address matched instruction by instruction against the Collector's code
  (`src/edition.h`), with differential tests for the audio helpers. About 24 to 30 FPS in race wars against about 31 on the
  standard edition, because its translated game code keeps registers in memory between calls.
- `audio_switch_recuperar` (default 1): the audio pump refills its frame queue after a gap.
- `carbon_cube_interval` (default 1, shipped 30): run the car cube-reflection pass once every N frames.
- `carbon_gpu_bloom` is now on by default; `carbon_gpu_bloom_black_f84b` is an experiment (off).
- `carbon_audio_gain_cull_below`, `carbon_audio_mixer_rate`: experiments, off. They did not help (see PERFORMANCE.md).
- `content_max_marketplace_items` (default 30): how many DLC packs the game is shown.
- `carbon_ai_car_lod_bias` and `carbon_ai_car_lod_min_cars`: lower the model detail of the other racers (off; little effect).
- Diagnostics (report config only): per-pass render timings (`[render-pass]`, `[render-thread]`, `[main-pass]`), the audio
  queue depth in `rex_perfil.log`, a gain-sum statistics line, a hotspot analyzer (`scripts/analyze_hotspots.py`).

### Fixed
- **Audio stutter in race wars.** The pump asked for exactly the frames the output consumed, so the 32-frame cushion never
  refilled after the first gap and every mixer hiccup became silence. In race wars the audio requests without data went
  from about 50 to 410 per 10 s in nearly every race window to none in most windows (24 of 34 in one run; worst window 117).
- The game's mixer thread runs at Horizon priority 0x2A (was 0x2D), above the XMA decoder (0x2B) and the GPU ring (0x2C).
- **Empty pause menu background** when bloom is off: only the quarter-resolution blur chain is drawn black now, not the
  same shader at the resolutions the pause menu uses.
- **No DLC at all after an interrupted or hand-cleaned install.** A stale install journal made recovery fail forever:
  every install returned access denied and the game was shown no DLC. Recovery now clears such entries, a work folder
  that cannot be deleted is logged and no longer blocks anything, and one stuck pack no longer hides the others.
- Install work folders are deleted entry by entry when the standard delete fails on the SD card (it left
  `.dlc-install/<pack>` behind at every launch).
- The same DLC package in two folders under `dlc/` is installed once.
- The DLC list is capped at 30 packs: 36 and 52 crashed the profile load (call to `0xEEEEEEEE`), 30 ran a long session.

### Changed
- The shipped configs keep the car cube-reflection pass (refreshed every 30 frames) instead of skipping it for good.
- Bloom on: about 4 ms more GPU time per frame in a crowded race.

### Known problems
- Black patches next to a washed-out road at 120 to 150 km/h while the motion blur or tunnel vision effect is on. Not caused by
  bloom or the cube pass. Still open.
- Audio can still crackle for a few seconds at a heavy race start (mixer at about 3.8 ms of CPU per 5.3 ms block).
- Collector's Edition is slower and has had fewer hours of testing.
- Achievements are still not confirmed.
## 1.0.0-collectors-beta.1 (Collector's Edition)

First release of the Collector's Edition port. Beta. It uses the same renderer, audio output, DLC handling and settings as
1.0.0 above (everything under "Added", "Fixed" and "Changed" there applies), with these differences:

- Own install folder `switch/nfscarbon-collectors/` and own NRO `nfscarbon-collectors.nro`; saves are separate from the
  standard edition's. `default.xex` SHA-256 `922c7a1a2fc77405c43aac5159ad67fb0414c872084abc3335c68dbb4bec1815` (USA / NTSC-U release; the standard edition tested is the USA release too).
- Slower than 1.0.0: its translated game code keeps every register in memory between calls. At the reference clocks race
  wars run at 24 to 30 FPS (about 31 on the standard edition), lighter scenes at 45 to 55. A faster register-local
  translation exists but has not been run on hardware, so it is not part of this release.
- Fewer hours of testing: boot, career, races, pause and DLC loading were checked on hardware; cutscenes, long sessions
  and every car and event were not.
- The video hook, the mixer diagnostics and the level-of-detail hook of the standard edition are not used.
## 0.1.0-beta.8

Stability and data-safety fixes for the Switch port. Keeps the beta.7 release settings.

### Changed
- Completed guest threads are joined and their retained objects and host stacks are released, including threads the game never waits for.
- GPU uploads stop safely on allocation failure. Vertex data and draw constants stay associated with the correct upload buffer; copied vertex ranges follow the shader's index rules.
- Audio client teardown waits for callbacks already in progress before freeing or reusing their state.
- Shutdown waits for threads and decoder work to finish. Unsupported external cancellation no longer reports success or frees live thread state.
- Switch thread registration, priority and suspend/resume bookkeeping now include threads created by other threads.
- DLC files and metadata are staged and checked before replacing an installation. Failed reads, writes, flushes, closes or publication steps preserve the previous install; interrupted replacements can recover on a later launch or content access.
- Achievement history writes check file operations and preserve the previous file when replacement fails. This does not confirm in-game achievement unlocks.
- Normal builds now retain the beta.7 release preset: 80% resolution with FSR, warning-only logs, diagnostics off and rumble on.

### Validation and limits
- The maintainer reports the combined fixes running successfully on Switch. Longer sessions and individual failure paths still need hardware coverage; no new FPS or endurance claim is made.
- 118 host regression tests and the separate file-replacement check passed. The Switch Release build and ARM64 ELF/NRO validation passed.
- Removing a package from `dlc/` still does not uninstall its extracted copy. Mixed or large DLC sets can still cause profile-loading problems; see the README cleanup steps. The 55-pack set is unverified.
- Nonreturning guest code, audio callbacks or decoder work can still prevent shutdown from completing.
- Collector's Edition support is not included.

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
