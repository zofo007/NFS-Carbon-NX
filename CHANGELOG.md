# Changelog

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
