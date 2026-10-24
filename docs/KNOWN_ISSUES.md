# Known issues

Standard Edition is the first non-beta release; Collector's Edition is a beta. Numbers are from the reference clocks
(CPU 1428 / GPU 768 / memory 1600 MHz) unless stated.

## Open

| Area | Problem | Notes |
|---|---|---|
| Audio | A heavy race start can still crackle for a few seconds | In the latest runs 4 of 23 busy windows of 10 seconds had more than 50 audio requests without data (worst 135, median 1). The mixer needs about 3.8 ms of CPU per 5.3 ms block with the CPU saturated, so the cushion (32 frames, about 170 ms) cannot cover it. Likely worse below the reference clocks |
| Frame rate | Standard: about 30 FPS in race wars (13 to 16 cars). Collector's: about 24 to 30 FPS depending on the scene | Race wars run at about 1900 draws per frame; the densest windows reach 4000 to 5000. Open roads run 40 to 55 FPS |
| Shadows | Black patches next to a washed-out white road, for a few frames | Only while the motion blur or tunnel vision effect is on, at about 120 to 150 km/h with strong light and many cars. Not caused by bloom or by the car reflection pass (both tested). The renderer's reuse features (resolve skipping, texture and vertex rechecks) are being tested; a config that turns them off is in the test builds |
| Collector's Edition | Slower than the standard edition | Its game code is translated with every register kept in memory between calls; the standard edition keeps them in local variables. A register-local Collector's build exists and is under test |
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

## Fixed in this release

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