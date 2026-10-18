# Changelog

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
