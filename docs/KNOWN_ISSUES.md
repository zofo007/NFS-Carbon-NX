# Known issues (beta)

| Area | Problem | Notes |
|---|---|---|
| Audio | Drop-outs at the race countdown, close packs of cars and the dense city | Worst at CPU 1224 MHz; almost gone at 1428-1785 MHz. The game's software mixer needs more CPU than the cores give; its cost is spread over many functions |
| Frame rate | 18-25 FPS in the heaviest scenes at 1224 MHz | About 4000 draws per frame there; the command thread, GPU and game thread each need 40-50 ms |
| Shadows | A frame of black car shadows now and then | Cause unknown. Not related to the texture or constant caches, the mirror or the reflection passes |
| Achievements | Not confirmed | The unlock call is handled, but none has been seen in the logs |
| Cutscenes | Can fail on a launch host with a small memory budget | The video thread cannot be created and the game waits forever. A cutscene decoder kept in memory caused it once; that is fixed. Pick a host that leaves about 3.2 GB |
| Forwarder | Needs the 39-bit address space option | Without it the game aborts while creating threads |
| Editions | One edition tested | See the README for its `default.xex` hash |
| First launch | Shaders and pipelines compile as needed | Stutters for a few seconds the first races; cached on the SD card afterwards |
| Debug console | L+R+Down log console is off | Opening it froze the whole console. Enable it with `switch_log_console_shortcut = true` at your own risk |
| DLC | Packages load; which ones unlock what is untested | Tell us what you tried |

Measurements behind these entries: [PERFORMANCE.md](PERFORMANCE.md).
