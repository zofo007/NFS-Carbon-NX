# Performance

This page describes play sessions, not benchmarks. It shows what a user with an overclocked Switch can expect and how we
read the numbers. Other consoles, scenes and clocks will differ, which is why the README asks for reports. The newest
measurements come first; the earlier sessions follow and use different scenes.

## Race wars, October 2026 builds (standard and Collector's)

Race wars are the heaviest scenes: 13 to 16 cars, about 1900 draws per frame (up to 4500 to 5600 in the worst windows).
Handheld, CPU 1428 / GPU 768 / memory 1600 MHz, scene at 80% upscaled with FSR, anisotropic filtering 4x, bloom on, 30 DLC
packs installed. Numbers come from `scripts/summarize_run.py` and the renderer's own statistics; a "window" is the 5 s
between two renderer statistics lines.

| Windows with more than 300 draws | Standard Edition | Collector's Edition (beta) |
|---|---|---|
| Windows | 52 | 38 |
| FPS median / 10th / 90th percentile | 31.5 / 26.8 / 38.0 | 26.3 / 20.3 / 46.6 |
| Lowest FPS | 18.7 | 15.5 |
| At 30 FPS or more | 62% | 29% |
| Under 25 FPS | 8% | 42% |
| Draws per frame, median | 1864 | 1781 |

Only the crowded windows (more than 1200 draws, race wars):

| | Standard | Collector's |
|---|---|---|
| Windows | 44 | 26 (run 1) / 26 (run 2) |
| FPS median / lowest | 30.9 / 18.7 | 23.6 / 15.5 (run 1), 29.7 / 18.3 (run 2) |
| At 30 FPS or more | 55% | 0% (run 1), 42% (run 2) |
| Draws per frame, median | 1929 | 1984 (run 1), 1669 (run 2) |

Both editions use the same renderer and draw about the same number of draws, so the gap comes from the game's own code:
the Collector's translation keeps the registers in memory between calls, which makes its render thread slower (the main
render pass takes about 7 to 9 ms against about 5.5 to 7 ms). Run 2 of the Collector's had fewer cars on screen than run 1, which is why its figures are better; it is
the same normal build (code size 52.97 MB in the log). A register-local Collector's build is being tested.
Lighter scenes (menus, open roads) run 45 to 55 FPS on both.

The game's own render thread, standard edition, per frame: visibility prep about 3.5 to 4 ms, reflections 0.8, main pass
5.5 to 7, whole frame 10.5 to 13 ms. The car cube-reflection pass costs about 0.1 ms with the 30-frame refresh (2.4 ms every frame).

## Audio before and after (standard, race wars)

The audio output used to ask the game for exactly one frame per 5.33 ms, so a cushion of 32 frames emptied after the first
gap and stayed empty. With the refill, the output keeps about 31 frames queued (measured from the output thread's own
counters in `rex_perfil.log`).

| Per 10 s window in race wars | Before the refill | After |
|---|---|---|
| Audio requests that found no data | about 50 to 410 in nearly every race window | 0 in most windows (24 of 34 in one run); worst windows 117 and 135 |
| Windows with more than 50 such requests | most race windows | 4 of 23 busy windows |
| Mixer, longest single block | 10 to 17 ms | 4 to 9 ms |
| Mixer thread CPU in races | 60 to 70% of one core | 50 to 75% |

What is left sits at heavy race starts and the busiest stretches: there the mixer needs about 3.8 ms of CPU per 5.33 ms
block, the three cores are saturated (about 280% of 300%), and its hand-offs with the audio worker take the rest.

## What was tried and did not help

| Idea | Result |
|---|---|
| Mixer at 44.1 kHz (the game's rate fields set to 44100) | Sounded normal, but the mixer made the same number of blocks and used the same CPU (1876 mixes per 10 s at idle, a real 8% cut would give about 1725). Dropped |
| Skip the mixing of voices with zero or near-zero gain | Only about 7% of the gain-sum calls have zero gain, 0.1% are below one 16-bit step. Zero gain is skipped; no more |
| Hand-written versions of single mixer functions | The recompiled code is already compiled native code; the mixer's cost is spread over hundreds of small functions with no hot spot (the biggest leaf is 5%) |
| Lower model detail for the other racers (`carbon_ai_car_lod_bias`) | Little FPS effect on Switch; level 3 and above draws no car body. Off by default |
| Bloom off | Saves about 4 ms of GPU time per frame in a crowded race; with the GPU not the only limit the frame rate felt about the same, and the picture is much worse. Bloom is on by default |
| Skipping the car cube-reflection pass for good | Works as a saving, but it is a suspect for the flashing shadows; the pass now runs every 30 frames |

## Beta.8 era session: one measured session, explained
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
