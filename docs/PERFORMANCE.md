# Performance: one measured session

This page describes one play session, not a benchmark. It shows what a user with an overclocked Switch can expect and
how we read the numbers. Other consoles, scenes and clocks will differ, which is why the README asks for reports.

## Setup

| | |
|---|---|
| Mode | Handheld (1280x720 display) |
| CPU / GPU / memory | 1428 / 768 / 1600 MHz |
| Scene resolution | 90% of 1280x720 (1152x648) |
| Anisotropic filtering | 4x |
| Launch | Title override, host game memory budget 3189 MB |
| Session | 5.7 minutes of in-game windows: menus, a race, four cutscenes, DLC packages installed |
| Version | 0.1.0-beta build `dfc85f62a0ac` |

Numbers come from `scripts/summarize_run.py` on the game log and `rex/rex_perfil.log`. Each "window" is the 5 seconds
between two statistics lines of the renderer.

## Results

| Measure | Value |
|---|---|
| FPS (in-game windows) | median 38.2, 10th percentile 31.1, 90th 50.4, lowest 21.0, highest 59.6 |
| Windows at or above 30 FPS | 93% |
| Windows below 25 FPS | 2% (two windows) |
| Draws per frame | median 1046, 90th percentile 1701, highest 3429 |
| GPU time per frame | median 25.3 ms, 90th percentile 28.0 ms |
| Audio packets from the game's mixer per 10 s | median 1876 of 1875 needed, lowest 1790 |
| Audio requests that found no data (per 10 s, busy windows) | median 0, highest 55; one window over 50 |
| Process CPU in busy windows | median 208% of 300% |

### By scene weight

The renderer reports the number of draws per frame. More draws means a busier scene (more cars, props and lights):

| Draws per frame | Windows | Median FPS | Lowest FPS | Median GPU ms |
|---|---|---|---|---|
| under 800 | 13 | 50.0 | 37.0 | 16.2 |
| 800 - 1500 | 36 | 38.0 | 29.1 | 25.6 |
| 1500 - 2500 | 6 | 31.2 | 29.0 | 26.9 |
| over 2500 | 2 | 24.6 | 21.0 | 29.1 |

## How to read it

- **The frame rate follows the scene.** It is about 50 FPS when little is on screen and about 30 when the screen is
  crowded. The two windows with more than 2500 draws are the only ones under the 30 FPS mark by a wide margin.
- **Audio was almost clean.** The mixer delivered at least 1800 of the 1875 packets it owes per 10 seconds in 32 of 33
  windows, and the drop-outs that did occur
  were small (at most 55 data-less requests in 10 seconds, against 400-750 in the worst windows at 1224 MHz).
- **This session had few extreme moments.** The dense city centre and the race countdown stress the console far more
  than most of what is measured here. Earlier sessions at 1224 MHz saw about 4000 draws per frame there and 18-25 FPS.

## Compared with lower clocks

Measured earlier on the same console and scenes (different sessions, so only the trend counts):

| CPU clock | Heavy scenes |
|---|---|
| 1224 MHz | 18-25 FPS, audio drop-outs of 100-750 data-less requests per 10 s |
| 1428 MHz | 21-28 FPS in the two heaviest windows of the session above, about 31 FPS with 1500-2500 draws, drop-outs rare |
| 1785 MHz (earlier session) | about 30 FPS in the heaviest places, drop-outs almost gone |

## Caveats

One console, one session, one resolution setting (90%). Temperatures stayed under 40 C in these tests. The log's FPS
is the renderer's own count of presented frames. If your numbers are very different, that is useful: follow the steps
in the README to send them.
