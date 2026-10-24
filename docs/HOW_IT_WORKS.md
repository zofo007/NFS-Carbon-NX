# How it works

A tour of what happens between "launch the NRO" and "a frame appears", written for people who want to understand,
debug or reuse the port. Numbers come from our own logs and are measured on one Switch; treat them as examples.

## 1. From the game file to an NRO

1. **Your `default.xex`** is the game's program for the Xbox 360's PowerPC CPU.
2. **ReXGlue** reads it and writes one C++ function for every PowerPC function it finds (about 72,000 here). The files
   are large and are never stored in this repository: you generate them locally (`generated/`).
3. **devkitA64** compiles those functions together with the runtime, the renderer and our hooks into one ARM64
   program, packed as an `.nro`. The startup table of all those functions is compiled without optimisation on purpose
   (`-O0`): optimising 72,000 registrations only costs build time.
4. At run time the NRO needs the rest of your game files (`NFS/`, `Movies/`) next to it.

Nothing is emulated: the game's own logic runs as native code. What it asks the console for (files, threads, sound,
a GPU) is answered by the runtime.

## 2. The runtime around the game

- **Memory.** The Xbox 360 gives the game a 512 MB address space. The runtime maps it inside the Switch process, which
  is limited by the title you launch from (hold R on a game). The log prints `[limits]` at startup. We observed that the
  process sits within a few MB of its limit all the time, so large temporary allocations (for example a cutscene
  decoder kept alive after its movie) can make the game fail to create a thread and appear frozen.
- **Threads.** Game threads become Horizon threads. Priorities matter: the audio threads are above the game's, the GPU
  command thread above the mixer, and two game threads that hand frames to each other are lifted one step so they wake
  promptly. Raising the mixer further made things worse in testing.
- **Kernel services.** Files (read-only), saves, DLC content folders, timers, events, XMA audio and controllers are
  provided by the SDK. Saves and caches go to `user/`, never to `game-data/`.

## 3. Drawing a frame

The Xbox 360 GPU is driven by a stream of command packets. The renderer (`renderer/`) is a small "GPU" that consumes
that stream:

1. A **command thread** reads packets, keeps a copy of the GPU registers, and turns each draw into Vulkan calls.
2. **Shaders** are Xenos microcode. They are translated to GLSL, compiled to SPIR-V and cached on the SD card; the list
   of pipelines used is saved so later launches build them while loading.
3. The Xbox's **EDRAM** (the small fast render memory) is imitated with render targets; "resolves" that copy EDRAM to
   a texture become image copies. About 15-18 resolves and 30-36 passes happen in a typical frame.
4. A **record thread** replays the recorded commands into the Vulkan command buffer so the command thread does not
   wait on the driver. One submit per frame.
5. The driver is **NVK** from Mesa, linked into the NRO.

In our heaviest scene (dense city, about 4000 draws per frame) the command thread, the GPU and the game's own render
thread each need roughly 40-50 ms, so all three limit the frame at once. That is why single fixes rarely moved the
frame rate by more than a few percent.

## 4. Audio

- The game mixes its own sound in software. One thread produces 256-frame, six-channel packets (a packet every
  5.33 ms) and hands them to a source voice.
- The host side takes the packets, downmixes to stereo and queues them for the Switch's audio output. The queue holds
  about 170 ms.
- Sounds stored as XMA are decoded with FFmpeg on a separate worker thread.
- The mixer needs about 2.5 ms of CPU per packet in a calm race and about 5 ms at a race start with many cars, against
  the 5.33 ms available. When it cannot keep up the output runs dry. A higher CPU clock is the only thing that fixed
  it completely; shaving small pieces off the mixer helped little because its cost is spread over many functions.

### The audio output queue

The game renders one 256-sample frame (5.33 ms at 48 kHz) each time the output asks for it. The output thread asks once
per tick and keeps up to 32 frames queued before the device (about 170 ms). Earlier builds asked for exactly one frame per
tick, which only replaces what the device consumes: after any gap the queue stayed near empty and every later mixer
hiccup was heard as silence. It now asks for extra frames while the queue is below its target, so a heavy block of
mixing is absorbed by the queue. The queue depth is counted from the output thread (which does not write the log) and
appears in `rex_perfil.log` as `audio queue`.

The game's mixer thread (`RWAudioCore Dac`) runs at Horizon priority 0x2A, above the XMA decoder and the GPU ring thread.
## 5. Cutscenes

Movies are WMV3. The game's own decoder is the recompiled Xbox code and reached about 22 FPS here, so the picture fell
behind its sound. The port hooks the point where the game asks for a frame, feeds the same bytes to FFmpeg and writes
the result into the game's picture buffers. Every movie starts with the game decoding as usual and FFmpeg comparing;
only when the first frames match does FFmpeg take over, and any difference keeps the original decoder.

## 6. Hand-written fast paths

Profiling showed a few functions the generated code runs slowly. Each replacement keeps the original as a fallback and
has a switch in `nfscarbon.toml`:

| Area | What | How it is checked |
|---|---|---|
| Audio mixer | sample unpack, gain copy, gain sum, resamplers, filters as register-local code; unpack and gain also in NEON | Differential tests against the generated original: memory, host FP state and registers must match (tens of thousands of cases, with NaNs and denormals) |
| Textures | word-wise endian swap for linear and tiled copy formats | Bit-exact test over all formats and endians |
| Uploads | only changed constant blocks, one vertex check per frame, descriptor sets kept across frames | Counters in the log; same output |
| Cutscenes | FFmpeg WMV3 | Compared with the game's decoder at the start of each movie |

Where a replacement could not be made bit-exact (for example a mixer function whose NaN handling differs), it was left
alone.

## 7. What limits it

Three cores, a CPU at 1428 MHz in our tests, and a game written for a console with three dual-threaded cores and a
much faster memory path. Most of the remaining time is spread over many small pieces (no function above about 3% of a
thread), which is why we recommend a CPU overclock for the heaviest scenes and publish the measurements in the README.
`scripts/summarize_run.py` turns a log folder into the numbers quoted there.
