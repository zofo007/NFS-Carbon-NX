<div align="center">

<img src="docs/banner/carbon-x-collectors-beta.png" alt="Need for Speed Carbon X Collector's Edition (Beta)" width="80%">

![standard](https://img.shields.io/badge/Standard%20Edition-v1.0.0-brightgreen) ![collectors](https://img.shields.io/badge/Collector's%20Edition-v1.0.0--collectors--beta.1-orange) ![platform](https://img.shields.io/badge/platform-Nintendo%20Switch%20(ARM64)-lightgrey) ![license](https://img.shields.io/badge/license-GPL--3.0-blue) [![Support me on Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-FF5E5B?logo=ko-fi&logoColor=white)](https://ko-fi.com/ojojoj)

[Status](#status) · [What's new](#whats-new) · [Install](#install) · [Overclocking](#overclocking) · [Known issues](#known-issues) · [Send logs](#help-improve-it-send-your-logs) · [Docs](#documentation) · [Credits](#credits)

</div>

# NFS Carbon NX

Need for Speed: Carbon (Xbox 360, 2006) running natively on the Nintendo Switch. Two editions are supported:

- **Standard Edition**: the first non-beta release.
- **Collector's Edition**: beta. It shares the renderer and almost everything else with the standard edition, but it
  runs slower (see Status).

This is not an emulator. The PowerPC code of the game's `default.xex` is translated to C++ with
[ReXGlue](https://github.com/rexglue/rexglue-sdk) and compiled for the Switch. The game's GPU commands are drawn
by a native Vulkan renderer running on NVK (Mesa). It runs the **Xbox 360 release** of the game, not the original
Xbox version. You need your own copy of the Xbox 360 game.

> [!WARNING]
> **It needs an overclock.** It boots, plays and saves. At stock handheld clocks it is too slow and the audio
> stutters. At the reference overclock (CPU 1428 MHz, GPU 768 MHz, memory 1600 MHz) the standard edition plays at about
> 30 FPS in the heaviest races (13 to 16 cars) and 40 to 50 FPS elsewhere. The Collector's Edition beta is slower: about
> 24 to 30 FPS in the heaviest races. Read [Overclocking](#overclocking) and [Known issues](#known-issues) first.

## Status

| Area | Standard Edition | Collector's Edition (beta) |
|---|---|---|
| Boot, menus, career, saves | Work (saves persist across launches) | Boots and plays; fewer hours of testing |
| Races | Heaviest races (race wars, 13 to 16 cars) at the reference clocks: median 31 FPS, 62% of the time at 30 FPS or more, lowest 18.7. Open roads and lighter races 40 to 55 FPS ([details](docs/PERFORMANCE.md)) | Heaviest races: median 24 to 30 FPS depending on how many cars are on screen (two runs), 0% and 42% of the crowded windows at 30 FPS or more, lowest 15.5. Lighter scenes 45 to 55 FPS |
| Cutscenes | Work, decoded with FFmpeg and checked against the game's own decoder at the start of each movie | Same |
| Audio | Clean in most windows; the race start can still crackle (see Known issues) | Same code; slightly worse in the heaviest races because the game runs slower |
| Controller rumble | On by default (`input_vibracion`) | Same |
| Long sessions | A 90-minute session no longer runs out of memory (texture memory is capped) | Same code, less testing |
| DLC packages | Load. Up to **30** packs at once ([DLC](#dlc)) | Same |
| Achievements | Not confirmed to work | Same |
| Other editions / regions | Not supported | Not supported |

Measured at CPU 1428 MHz, GPU 768 MHz, memory 1600 MHz. Stock handheld clocks are CPU 1020 MHz and GPU 460 MHz.
All numbers are from one console; see [docs/PERFORMANCE.md](docs/PERFORMANCE.md) for what was measured and how.

## What's new

Changes since 0.1.0-beta.8 (full list in [CHANGELOG.md](CHANGELOG.md)):

- **Collector's Edition support (beta).** Runs from its own folder next to the standard edition. The scheduling and
  audio hooks of the standard edition were ported with every address checked against the Collector's code, and the
  same renderer is used.
- **Audio stutter reduced a lot.** The audio output asked the game for exactly one frame per 5.3 ms, so once the 32-frame
  cushion emptied it never refilled and every small hiccup of the mixer became silence. It now refills the cushion after
  a gap. The mixer thread also runs at a higher priority than the XMA and GPU threads. In race wars the requests that
  found no data went from about 50 to 410 per 10 s in nearly every race window to none in most windows (24 of 34
  windows in one run; worst window 117, 135 in another).
- **Bloom is on by default** (it looks much better). It can still be turned off for about 4 ms of GPU time per frame,
  and the pause menu no longer shows an empty background in that mode.
- **Car reflection pass refreshed every 30 frames** instead of being skipped for good (skipping it is a suspect for the
  flashing shadows, and the refresh keeps almost all of the saving).
- **DLC works again after leftover install data.** A stale install journal made every DLC install fail and hid all DLC
  from the game; that is fixed, duplicate packs are installed once, and install work folders are cleaned up with a
  fallback that works on the SD card. At most 30 packs are listed to the game, because 36 and 52 crashed the profile load.
- **New diagnostics** for performance reports: per-pass render timings, a per-10-second audio queue line and the audio
  statistics in `rex_perfil.log`. They only appear in `nfscarbon.report.toml`.

Tried and dropped (they did not help): running the audio mixer at 44.1 kHz, skipping silent voices (only about 7% of
the mixing), and lowering the model detail of the other racers' cars (little effect, off by default).

## Screenshots

Captured on a Switch in handheld mode at the reference clocks. The number in the corner is the FPS at that moment.

<table>
<tr>
<td width="50%"><img src="docs/screenshots/standard-freeroam-chinatown-60fps.jpg" alt="Standard Edition, free roam in Historic Chinatown, 60 FPS"><br><sub>Standard Edition: free roam, Historic Chinatown (60 FPS)</sub></td>
<td width="50%"><img src="docs/screenshots/standard-race-skyline-32fps.jpg" alt="Standard Edition, race on the mountain road at 219 km/h, 32 FPS"><br><sub>Standard Edition: race at 219 km/h (32 FPS)</sub></td>
</tr>
<tr>
<td width="50%"><img src="docs/screenshots/standard-race-aston-43fps.jpg" alt="Standard Edition, night race with motion blur, 43 FPS"><br><sub>Standard Edition: night race with the speed effects (43 FPS)</sub></td>
<td width="50%"><img src="docs/screenshots/standard-career-menu.jpg" alt="Standard Edition, career menu in the garage"><br><sub>Standard Edition: career menu</sub></td>
</tr>
<tr>
<td width="50%"><img src="docs/screenshots/collectors-race-old-quarter-25fps.jpg" alt="Collector's Edition beta, race in the Old Quarter, 25 FPS"><br><sub>Collector's Edition (beta): race in the Old Quarter (25 FPS, one of the heavy scenes)</sub></td>
<td width="50%"><img src="docs/screenshots/collectors-freeroam-night-43fps.jpg" alt="Collector's Edition beta, free roam at night, 43 FPS"><br><sub>Collector's Edition (beta): free roam at night (43 FPS)</sub></td>
</tr>
</table>

## Overclocking

The game's code runs on three Switch cores and the CPU is the limit, so this port **is meant to be used with a CPU
overclock**. The reference settings are CPU 1428 MHz, GPU 768 MHz and memory 1600 MHz. In the maintainer's tests with them
(handheld, scene at 80% upscaled with FSR):

- the standard edition in race wars (13 to 16 cars, about 1900 draws per frame) ran at a median of 31 FPS, with 62% of
  the time at 30 FPS or more and 8% under 25;
- open roads and lighter races run 40 to 55 FPS;
- the beta.7 sessions in [docs/PERFORMANCE.md](docs/PERFORMANCE.md) (different scenes) had medians between 32 and 41 FPS;
- audio drop-outs are rare and short, but they still happen around the start of a race (see Known issues).

Use a clock manager of your choice (for example Horizon OC or sys-clk) and set the profile for the game you launch it
from. Overclocking is at your own risk: it can make the console unstable, hot, or corrupt data. Back up your SD card
and NAND, start with small steps and test stability. This project does not provide overclocking tools.

## What you need

- A Switch running custom firmware (Atmosphere) that can launch homebrew with title override.
- The Xbox 360 files of the game, extracted: `default.xex`, `NFS/` and `Movies/`. The tested editions are:

  | Edition | `default.xex` SHA-256 | Title / media |
  |---|---|---|
  | Standard | `947f95c7306eabf5bc72972938df57a922798bafbc02edf1a9107f0462c721e1` | 454107EC / 51D2B53F |
  | Collector's | `922c7a1a2fc77405c43aac5159ad67fb0414c872084abc3335c68dbb4bec1815` | 454107EC / 1F8BEB88 (version 7) |

  Other editions may crash or misbehave.
- The release files from the Releases page (loose `.nro` and `.toml` files, no archive).

The game files are not included and never will be. Use dumps of original discs you own.

## Install

Standard Edition: put the files so that the SD card looks like this:

```text
sdmc:/switch/nfscarbon/
  nfscarbon.nro
  nfscarbon.toml          (quiet default settings)
  nfscarbon.report.toml   (full logging, for performance reports)
  game-data/
    default.xex
    NFS/
    Movies/
  dlc/                (optional, see DLC)
```

Collector's Edition (beta) goes in its **own** folder; both can be installed side by side:

```text
sdmc:/switch/nfscarbon-collectors/
  nfscarbon-collectors.nro
  nfscarbon.toml
  nfscarbon.report.toml
  game-data/              (the Collector's Edition dump)
  dlc/                (optional, see DLC)
```

Use a fresh `user/` folder for the first Collector's test: saves of the two editions are separate.

Launch it with **title override**: hold **R** while starting an installed game, and pick a game that leaves the
application about 3.2 GB of memory (the log prints `proceso X/Y MB` and `[limits]` at startup; a host that gives less
makes cutscene threads fail).

**Forwarder:** a forwarder made with [Sphaira](https://github.com/NaGaa95/sphaira) works. When you generate it,
set the address space to **39-bit**: the default is too small and the game aborts while creating threads. Use the
forwarder's own settings for launching; nothing else changes. Keep in mind that a forwarder gets its own memory
budget, so check the `[limits]` lines in the log after the first run.

The first time you play, shaders and pipelines are compiled as they are needed and cached on the SD card, so the first
races can stutter for a few seconds. Do not delete `user/cache`.

## Controls

Each Switch button acts as the Xbox 360 button with the same letter. `+` is Start, `-` is Back, ZL/ZR are the
triggers, L/R the bumpers. Set `input_xbox_layout = true` in `nfscarbon.toml` to swap the face buttons by position.
Controller rumble is on; set `input_vibracion = false` to turn it off.

Shortcuts (hold **L + R** and press):

| Button | Action |
|---|---|
| Up | Debug overlay (FPS and counters) |
| Right | Settings overlay |
| Left | Achievements list |

## Settings

`nfscarbon.toml` next to the NRO holds the settings. The ones that matter are described in
[docs/CONFIG.md](docs/CONFIG.md), including the quality switches (render scale, upscaling filter, texture detail,
anisotropic filtering, bloom). Every setting has a default; the file only lists what differs.

The shipped file is **quiet**: it logs warnings only and the statistics are off, because logging costs frame rate
(most at the race start). The scene is drawn at 80% and upscaled with FSR; set `present_effect = "bilinear"` and
`carbon_gpu_render_scale = 90` for the older plain scaling. Bloom is on; `carbon_gpu_bloom = false` saves about 4 ms of
GPU time per frame in a crowded race.

## DLC

Put Xbox 360 DLC packages (the STFS files) anywhere under `sdmc:/switch/nfscarbon/dlc/` (or
`sdmc:/switch/nfscarbon-collectors/dlc/`). At startup they are extracted into the content folder and the game finds
them through its normal content API. The log shows `[dlc] N package(s) installed, M failed`. The same file in two
folders is installed once.

**Keep to 30 packs or fewer.** Hardware results: 30 packs ran a long session without a crash; 36 and 52 packs crashed
while the profile was loading (a call to the garbage address `0xEEEEEEEE` right after the last pack's token file was
read). The game now lists at most 30 packs (`content_max_marketplace_items`) and logs a warning when it drops some. The
packs are listed in the order the game's content folder lists them, so the ones that are left out are not chosen by you.

Removing a source package from `dlc/` does **not** uninstall its extracted copy in `user/`. When changing packs, back
up `user/` and follow the cleanup steps below to avoid mixing old and new installations. Which items a pack unlocks is
mostly untested: the Tier 3 performance packs and the car bundles were loaded, but not every unlock was checked in the
career.

Packages verified working on the Switch port. All 11 were installed together and a 57-minute career session ran on
them (boss races, cutscenes, saves) without a DLC-related crash. "Token" is the file the game opens inside the pack,
seen in the log:

| Package (folder name in `dlc/`) | Token the game opened | Status |
|---|---|---|
| Dump Truck (Addon) | `Dump Truck.txt` | Verified working |
| Fire Truck (Addon) | `Fire Truck.txt` | Verified working |
| 1971 Dodge Challenger | `1971 Dodge Challenger.txt` | Verified working |
| 1987 Toyota Corolla GTS AE86 | `1987 Toyota Corolla GTS AE86.txt` | Verified working |
| 2006 Pagani Zonda | `zonda.txt` | Verified working |
| 2006 Porsche 911 Turbo | `2006 Porsche 911 Turbo.txt` | Verified working |
| Autosculpt Tuner Body Kit Bundle | `AS_Tuner_Body.txt` | Verified working |
| Exotic Car Kit Bundle | `Integrated_Kit_Exotic.txt` | Verified working |
| Ultimate Performance Bundle | `Uber_Performance_T1.txt`, `T2`, `T3` | Verified working |
| Exotic Base Car Bundle | `snips.txt`, `william.txt` (shared with the next row) | Verified working |
| Need for Speed Carbon CE Bundle | `snips.txt`, `william.txt` (shared with the previous row) | Verified working |

Verified means the pack loaded and the game ran; it does not mean every item inside the pack was checked in the game.
A later 30-pack run also loaded more bundles (performance tiers, custom and base car bundles, the Maximum Carbon
Bundle) and ran fine. Add packs a few at a time and tell us what happens.

**If the game crashes while loading the profile after you changed your DLC** (the log ends right after a line like
`[dlc] el juego abre 'save:\...'`, and `rex_crash.log` reports a call to an invalid guest address):

1. Back up `sdmc:/switch/nfscarbon/user/` first.
2. Delete everything inside these three folders (only the extracted DLC and its install work data, never the other
   folders in `user/`, those hold your profile and saves):
   - `sdmc:/switch/nfscarbon/user/0000000000000000/454107EC/00000002/`
   - `sdmc:/switch/nfscarbon/user/0000000000000000/454107EC/Headers/00000002/`
   - `sdmc:/switch/nfscarbon/user/0000000000000000/454107EC/.dlc-install/` (install work data; builds before this
     release could leave a stale journal here and then install **no** DLC at all)
3. Start the game. The packages in `dlc/` are extracted again at launch. If it still crashes, move most packages out of
   `dlc/` and add them back a few at a time (30 at most).

## Known issues

- **Below the reference clocks**, audio drop-outs and low frame rates are likely: the game's software mixer needs more
  CPU than three slow cores give.
- **Audio is much better but not perfect.** In race wars it is clean in most windows, but a heavy race start can still
  crackle: in the latest runs 4 of 23 busy windows of 10 seconds had more than 50 audio requests without data (worst
  135, median 1). The mixer needs about 3.8 ms of CPU per 5.3 ms block there, with no headroom, and the CPU is saturated.
- **Frame rate** in race wars is about 30 FPS on the standard edition and about 24 to 30 on the Collector's beta. Open roads
  run much better.
- **Flashing shadows.** For a few frames, car shadows can turn into black patches next to a washed-out white road. It
  appears only while the motion blur or tunnel vision effect is on, at speed (around 120 to 150 km/h) with strong light
  and many cars. It is not caused by bloom or by the car reflection pass (both tested). Cause still unknown.
- **Collector's Edition is a beta.** It is slower than the standard edition because its game code is translated more
  conservatively (the registers are kept in memory between calls). A faster translation is being tested.
- **Achievements** are not confirmed. The SDK handles the unlock call, but no unlock has been seen in the logs.
- A forwarder needs the 39-bit address space option (see Install). Without it the game aborts while creating threads.
- A title-override host with a small memory budget can still make a cutscene fail (the game then waits for a thread that
  was never created and appears frozen); pick a host that leaves about 3.2 GB.

More detail in [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md) and the measurements in
[docs/PERFORMANCE.md](docs/PERFORMANCE.md).

## How it works

- **Translated, not emulated.** ReXGlue turns every function of `default.xex` into C++ and gives the game the Xbox
  360 kernel, files, audio and controllers it expects. This project adds the Switch side: memory, threads, clocks,
  audio output and presenting frames.
- **Native renderer.** The game's GPU command stream is read and drawn with Vulkan (`renderer/`), with its own shader
  translation (Xenos microcode to GLSL to SPIR-V) cached on disk.
- **Driver.** Vulkan comes from NVK, via [mesa-switch](https://github.com/danfromtico/mesa-switch).
- **Upscaling.** The scene is drawn at 80% of 1280x720 and scaled up with FSR (or CAS or bilinear, your choice).
- **Hand-written fast paths** where profiling showed the recompiled code is slow: audio mixer helpers (some with
  NEON), the cutscene decoder (FFmpeg WMV3, verified against the game's own output), texture untiling, vertex
  and constant uploads. Each one has an off-switch in `nfscarbon.toml`.
- **Two editions, one runtime.** The Collector's Edition is translated from its own `default.xex` and uses the same
  renderer, kernel and hooks; the hooks take their addresses from a per-edition table (`src/edition.h`).

## Help improve it: send your logs

Where it runs well and where it does not depends on the console, the clocks and the scene, and every run teaches us
something. Higher frame rates (60 FPS is the long-term wish, but it is far away) will come from finding what
limits real setups. If you play it, please send a report in the same shape every time:

0. The default `nfscarbon.toml` is quiet (it logs warnings only) because statistics cost frame rate. For a report,
   copy `nfscarbon.report.toml` over `nfscarbon.toml` for one session, then put your normal file back.
1. Play for at least one race and note the clocks you used and which edition.
2. Copy `sdmc:/switch/nfscarbon/logs/` (or `nfscarbon-collectors/logs/`) to your PC.
3. Run `powershell -File scripts/pack-logs.ps1 -Logs <that folder>`. It writes a zip and prints the summary
   (FPS spread, draws, GPU time, audio drop-outs, clocks).
4. Open an issue with the **Performance / audio report** template and attach the zip.

The logs contain your save profile names and SD paths; remove what you do not want to share. Never attach game files.

## Documentation

- [docs/HOW_IT_WORKS.md](docs/HOW_IT_WORKS.md): the whole path from `default.xex` to a frame, in order.
- [docs/PERFORMANCE.md](docs/PERFORMANCE.md): measured sessions, explained.
- [docs/CONFIG.md](docs/CONFIG.md): every setting.
- [docs/BUILD.md](docs/BUILD.md): building from source.
- [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md): problems we know about, and what was fixed.
- [CHANGELOG.md](CHANGELOG.md): what changed in each version.
- `scripts/summarize_run.py <logs folder>`: turns your logs into FPS, draws, GPU time and audio drop-out numbers
  for a bug report.

## Building

You build it yourself from your own `default.xex`; the translated game code is never distributed.
[docs/BUILD.md](docs/BUILD.md) has the full procedure. Dependencies are pinned in `dependencies.json`; changes to the
upstream source trees are kept as patches in `patches/`. The verified host is Windows with PowerShell.

## Support

This is a free-time project. Testing on real hardware takes many hours, and the AI assistants used for development are not free. If you like the work, you can kindly leave a tip on [Ko-fi](https://ko-fi.com/ojojoj). It is never required, and the port stays free.

[![Support me on Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-FF5E5B?logo=ko-fi&logoColor=white)](https://ko-fi.com/ojojoj)

## Credits

- **[zofo007](https://github.com/zofo007)**: made and maintains this port (Switch runtime, hooks, testing, releases).
- **Electronic Arts / EA Black Box**: Need for Speed: Carbon. This is an unofficial fan project.
- **[Sampreeth004](https://github.com/Sampreeth004/nfs-carbon-recomp)**: the Carbon recompilation and the native
  Vulkan renderer this project is built on. Without this work the Switch port would not exist; it is the foundation
  of everything here. Thank you.
- **[StevensND](https://github.com/StevensND/nfsmw-nx)**: the Most Wanted Switch port. Its Horizon layer of the SDK is
  used here, and many ideas (audio packet wait, frame hand-off, FFmpeg cutscenes, overlays, memory handling) were
  studied from it. No Most Wanted assets, addresses or game code are used.
- **[madelrandel-blip](https://github.com/madelrandel-blip/NFSMW-Recompiled)** and
  **[victorgbd](https://github.com/victorgbd/NFSMW-Recompiled-Mobile)**: the Most Wanted recompilation projects whose
  hooks Carbon's code was ported from.
- **Tom Clay** and the **[Xenia](https://xenia.jp)** team: [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk).
- **[danfromtico](https://github.com/danfromtico/mesa-switch)**, **NaGaa95** and the Mesa project: NVK on Horizon.
- **[devkitPro](https://devkitpro.org)** and **[switchbrew](https://github.com/switchbrew/libnx)**: devkitA64, libnx.
- **FFmpeg**, glslang, SPIRV-Tools, Vulkan Memory Allocator, fmt, spdlog, SIMDe and the other libraries listed in
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

### Related projects

- [StevensND/nfsmw-nx](https://github.com/StevensND/nfsmw-nx): Need for Speed: Most Wanted on the Switch, the closest
  relative of this port.
- [codepdbh/NFS-CARBON-360-ANDROID-RECOMP](https://github.com/codepdbh/NFS-CARBON-360-ANDROID-RECOMP) and
  [codepdbh/nfsmw-android](https://github.com/codepdbh/nfsmw-android): Carbon and Most Wanted for Android, from the
  same family of recompilation projects. Nothing from them is used here.

Development used AI coding assistants (OpenAI Codex and Anthropic Claude Code) under the maintainer's direction;
hardware testing and every release decision are the maintainer's.

## Legal

**This project does not modify any game files.** It reads your game files and never writes to them: saves, caches
and installed DLC go to separate folders (`user/`). It is meant to be used **only with dumps of original game discs
that you own**, made by you. It is not a way to obtain the game: do not
ask for game files and do not share them.

This repository contains no code or assets from the original game: no executable, archives, movies, shaders or
generated guest code. You must supply your own legally obtained copy. "Need for Speed" is a trademark of Electronic
Arts Inc.; Nintendo Switch is a trademark of Nintendo; Xbox 360 is a trademark of Microsoft. This project is not
affiliated with or endorsed by any of them. Running homebrew needs custom firmware, which Nintendo's terms forbid and
which can get a console banned. Use it at your own risk.

License: see [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).