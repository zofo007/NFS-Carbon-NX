# Performance: one measured session

This page describes one play session, not a benchmark. It shows what a user with an overclocked Switch can expect and
how we read the numbers. Other consoles, scenes and clocks will differ, which is why the README asks for reports.

## Setup

| | |
|---|---|
| Mode | Handheld (1280x720 display) |
| CPU / GPU / memory | 1428 / 768 / 1600 MHz |
| Scene resolution | 90% of 1280x720 (1152x648), plain bilinear scaling. The shipped default is now 80% with FSR; not measured at these clocks yet |
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
  were small (at most 55 data-less requests in 10 seconds).
- **This session had few extreme moments.** The dense city centre and the race countdown stress the console far more
  than most of what is measured here. The second session below reached about 4000 draws per frame there.

## Beta.7 defaults, 15 minutes

Reference clocks (1428 / 768 / 1600 MHz), the shipped settings of 0.1.0-beta.7 (scene at 80% upscaled with FSR, quiet log, rumble on), 88 in-game windows of 10 seconds from the game's own profiler:

| Measure | Value |
|---|---|
| FPS | median 40.8, 10th percentile 31.6, 90th 46.1, lowest 21.3, highest 53.2 |
| Windows at or above 30 FPS | 95% (84 of 88) |
| Windows below 25 FPS | 2 |
| Audio requests without data per 10 s | 0 in the median window; 7 windows around a race start had 24 to 482 (worst 482) |

This is a different scene mix from the two sessions above, so it is not a controlled comparison with 90% plain scaling; it shows that the new defaults keep the frame rate high. The audio drop-outs sit in the race start, where the process CPU was at 280-290% of 300%.

## A longer session

Same clocks (1428 / 768 / 1600 MHz), 22.5 minutes of racing and city driving, 261 in-game windows:

| Measure | Value |
|---|---|
| FPS | median 31.9, 10th percentile 29.6, 90th 45.4, lowest 13.8, highest 48.5 |
| Windows at or above 30 FPS | 53% |
| Windows below 25 FPS | 2% |
| Draws per frame | median 1782, 90th percentile 3293, highest 4206 |
| GPU time per frame | median 30.2 ms |

This session spent much more time in busy city scenes, which is why its median is lower than the first one.

## Caveats

One console, one session, one resolution setting (90%). Temperatures stayed under 40 C in these tests. The log's FPS
is the renderer's own count of presented frames. If your numbers are very different, that is useful: follow the steps
in the README to send them.
