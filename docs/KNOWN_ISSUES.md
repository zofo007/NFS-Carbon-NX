# Known issues

Standard Edition 1.1.0; Collector's Edition 1.1.0-collectors-beta.1 is a beta. Numbers are from the reference clocks
(CPU 1428 / GPU 768 / memory 1600 MHz) unless stated.

## Open

| Area | Problem | Notes |
|---|---|---|
| Audio | A heavy race start can still crackle for a few seconds | In the latest runs 4 of 23 busy windows of 10 seconds had more than 50 audio requests without data (worst 135, median 1). The mixer needs about 3.8 ms of CPU per 5.3 ms block with the CPU saturated, so the cushion (32 frames, about 170 ms) cannot cover it. Likely worse below the reference clocks |
| Frame rate | About 30 FPS in race wars (13 to 16 cars) on both editions | Race wars run at about 1900 draws per frame; the densest windows reach 4000 to 5000. Open roads run 40 to 55 FPS. The GPU is the limit in crowded scenes (about 25 to 30 ms per frame) |
| Shadows | Flickering shadows in crowded scenes with many AI cars (race wars): for a few frames they can turn into black patches next to a washed-out white road | Seen at about 120 to 150 km/h with strong light and the motion blur or tunnel vision effect on. Both editions. Not caused by bloom or by the car reflection pass (both tested); stale vertex or index data was checked for and ruled out. Cause still unknown |
| Quick settings | The wording of some rows is hard to understand; Save to config drops the comments of nfscarbon.toml | New in 1.1.0. The maintainer opened and used the page on a console and found some of the settings unclear. Back up `nfscarbon.toml` before using Save to config. Tell me which rows confuse you and I will reword them |
| Pause | A rare pause of about one second in the middle of a race | Seen once in 48 minutes of testing (Standard, 1.1.0). The log shows no texture upload, pipeline build or disk stall in that frame; the 64 MB GPU memory steps are the main suspect. ulkan_vma_block_mb = 0 in 
fscarbon.toml restores the old step size at the price of about 260 MB more memory. Not seen in a 34-minute Collector's session |
| Collector's Edition | Beta, can still run behind the Standard Edition; recommended only for people willing to test | 1.1.0-collectors-beta.1 translates the game code like the Standard Edition (registers in local variables). It has run for one 34-minute session on one console: the four intro movies were opened and read without errors, six races, a save. Long sessions, other cutscenes (the game's own video decoder, not FFmpeg) and every car and event have had few hours of play. The old translation is still in the repository (-Edition collectors) |
| Long sessions | The longest session tested with 1.1.0 is 48 minutes | Process memory levelled off at about 2.4 GB of about 3.2 GB in that session; with the 1.0.0 memory behaviour a 73-minute session reached 2.7 GB |
| DLC | At most 30 packs | 36 and 52 packs crashed while the profile loaded (call to `0xEEEEEEEE` after the last pack's token file). The game is shown 30 and logs a warning |
| DLC | Which pack unlocks what is mostly untested | The 11 packs in the README were verified loading and playing; a 30-pack set ran for about an hour. Individual unlocks were not checked |
| DLC removal | Removing files from `dlc/` leaves their extracted copies installed | Back up `user/` and follow the README cleanup steps when changing packs |
| Achievements | Not confirmed | The unlock call is handled, but none has been seen in the logs |
| Cutscenes | Can fail on a launch host with a small memory budget | The video thread cannot be created and the game waits forever. Pick a host that leaves about 3.2 GB |
| Forwarder | Needs the 39-bit address space option | Without it the game aborts while creating threads |
| Editions | Two tested: Standard and Collector's | See the README for the `default.xex` hashes. Other editions and regions are not supported |
| First launch | Shaders and pipelines compile as needed | Stutters for a few seconds the first races; cached on the SD card afterwards |
| Debug console | L+R+Down log console is off | Opening it froze the whole console. Enable it with `switch_log_console_shortcut = true` at your own risk |
| Shutdown | Nonreturning guest code, audio callbacks or decoder work can prevent shutdown from completing | Shutdown waits for live work instead of treating unsupported cancellation as completion |

## Fixed or improved in 1.1.0

| Problem | What changed |
|---|---|
| The game's code ran slower than it needed to | Plain memory reads in the translated game code (the 543 waiting functions keep the strict reads): 13 to 17% less render-thread CPU time, 5 to 10% less CPU in total, 1 to 3.5% more FPS in paired tests |
| Collector's Edition 5 to 10 FPS behind the Standard Edition in crowded races | Register-local translation: 14% fewer instructions in the hottest functions, 20% less code; the same FPS as the Standard Edition at the same scene load in a 34-minute test |
| Memory use growing by about 260 MB at a time | The GPU allocator grows in 64 MB steps: about 2.4 GB instead of 2.7 GB in a long session |

## Fixed in 1.0.0

| Problem | What it was |
|---|---|
| Audio stutter in race wars | The audio pump never refilled its cushion after a gap, so every mixer hiccup became silence. It refills now; most race windows have no missing audio data |
| No DLC after an interrupted or hand-cleaned install | A stale install journal made recovery fail and hid all DLC. Recovery clears it, and one stuck pack no longer hides the others |
| DLC install failing at every launch | The install work folder could not be deleted on the SD card. A fallback deletes it entry by entry |
| Same DLC pack installed twice | Packs with the same file name in two folders are installed once |
| Crash with 36 or 52 DLC packs | Capped at 30 packs listed to the game |
| Empty pause menu background (bloom off) | The pause menu builds its background with the same blur shader as the bloom at other resolutions; only the bloom resolution is drawn black now |
| Hiccup every 5 seconds, freezes after about 90 minutes, cutscene freezes | Fixed in 0.1.0-beta.6 and 0.1.0-beta.7 (statistics report, texture memory cap, host stack size) |

Measurements behind these entries: [PERFORMANCE.md](PERFORMANCE.md).